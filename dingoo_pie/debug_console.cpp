#include "debug_console.h"

#include <mutex>
#include <stdio.h>
#include <string>
#include <wchar.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <windows.h>
#endif

static bool g_debugConsoleOpen = false;
static FILE* g_debugLogFile = NULL;

#ifdef _WIN32
static std::mutex g_debugOutputMutex;
static std::mutex g_debugRoutingMutex;
static bool g_debugOutputRouting = false;
static int g_debugOutputReadFd = -1;
static HANDLE g_debugConsoleOutput = INVALID_HANDLE_VALUE;
static bool g_debugConsoleOutputOwned = false;

static std::wstring pathNearExe(const wchar_t* fileName)
{
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring path(exePath);
    size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
    {
        path.resize(slash + 1);
    }
    path += fileName;
    return path;
}

static bool setDebugConsoleFont(HANDLE output, const wchar_t* faceName)
{
    CONSOLE_FONT_INFOEX fontInfo = {};
    fontInfo.cbSize = sizeof(fontInfo);
    if (!GetCurrentConsoleFontEx(output, FALSE, &fontInfo))
    {
        fontInfo.dwFontSize.Y = 16;
    }
    if (fontInfo.dwFontSize.Y < 14)
    {
        fontInfo.dwFontSize.Y = 16;
    }
    wcscpy_s(fontInfo.FaceName, faceName);
    return SetCurrentConsoleFontEx(output, FALSE, &fontInfo) != 0;
}

static void configureDebugConsoleEncoding(HANDLE output)
{
    // Keep stdio in narrow mode. The codebase writes UTF-8 through printf, and
    // switching to _O_U8TEXT would make narrow printf calls unstable on MinGW.
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    if (output != INVALID_HANDLE_VALUE)
    {
        DWORD mode = 0;
        if (GetConsoleMode(output, &mode))
        {
            SetConsoleMode(output, mode | ENABLE_PROCESSED_OUTPUT);
        }

        // Prefer CJK-capable console fonts so Chinese app paths render when the
        // debug console is enabled. Consolas remains a fallback for non-CJK systems.
        if (!setDebugConsoleFont(output, L"NSimSun") &&
            !setDebugConsoleFont(output, L"SimSun"))
        {
            setDebugConsoleFont(output, L"Consolas");
        }
    }
}

static void closeDebugConsoleOutputLocked(void)
{
    if (g_debugConsoleOutput != INVALID_HANDLE_VALUE)
    {
        if (g_debugConsoleOutputOwned)
        {
            CloseHandle(g_debugConsoleOutput);
        }
        g_debugConsoleOutput = INVALID_HANDLE_VALUE;
        g_debugConsoleOutputOwned = false;
    }
}

static bool ensureDebugStreamOpen(FILE* stream, const char* mode)
{
    int fd = _fileno(stream);
    if (fd >= 0 && _get_osfhandle(fd) != -1)
    {
        return true;
    }

    FILE* reopened = NULL;
    if (freopen_s(&reopened, "NUL", mode, stream) != 0 || !reopened)
    {
        return false;
    }

    fd = _fileno(stream);
    return fd >= 0 && _get_osfhandle(fd) != -1;
}

static unsigned __stdcall debugOutputPump(void*)
{
    char buffer[4096];
    for (;;)
    {
        int got = _read(g_debugOutputReadFd, buffer, sizeof(buffer));
        if (got <= 0)
        {
            break;
        }

        std::lock_guard<std::mutex> lock(g_debugOutputMutex);
        if (g_debugLogFile)
        {
            fwrite(buffer, 1, (size_t)got, g_debugLogFile);
            fflush(g_debugLogFile);
        }
        if (g_debugConsoleOpen && g_debugConsoleOutput != INVALID_HANDLE_VALUE)
        {
            DWORD written = 0;
            WriteFile(g_debugConsoleOutput, buffer, (DWORD)got, &written, NULL);
        }
    }
    return 0;
}

static bool ensureDebugOutputRouting(void)
{
    std::lock_guard<std::mutex> routingLock(g_debugRoutingMutex);
    if (g_debugOutputRouting)
    {
        return true;
    }

    int pipeFds[2] = { -1, -1 };
    if (_pipe(pipeFds, 65536, _O_BINARY) != 0)
    {
        return false;
    }

    if (!ensureDebugStreamOpen(stdout, "w") ||
        !ensureDebugStreamOpen(stderr, "w"))
    {
        _close(pipeFds[0]);
        _close(pipeFds[1]);
        return false;
    }

    int stdoutFd = _fileno(stdout);
    int stderrFd = _fileno(stderr);
    int stdoutBackup = _dup(stdoutFd);
    int stderrBackup = _dup(stderrFd);
    if (stdoutBackup < 0 || stderrBackup < 0)
    {
        if (stdoutBackup >= 0)
        {
            _close(stdoutBackup);
        }
        if (stderrBackup >= 0)
        {
            _close(stderrBackup);
        }
        _close(pipeFds[0]);
        _close(pipeFds[1]);
        return false;
    }

    fflush(stdout);
    fflush(stderr);
    if (_dup2(pipeFds[1], stdoutFd) != 0 ||
        _dup2(pipeFds[1], stderrFd) != 0)
    {
        _dup2(stdoutBackup, stdoutFd);
        _dup2(stderrBackup, stderrFd);
        _close(stdoutBackup);
        _close(stderrBackup);
        _close(pipeFds[0]);
        _close(pipeFds[1]);
        return false;
    }
    _close(pipeFds[1]);

    g_debugOutputReadFd = pipeFds[0];
    uintptr_t thread = _beginthreadex(NULL, 0, debugOutputPump, NULL, 0, NULL);
    if (!thread)
    {
        _dup2(stdoutBackup, stdoutFd);
        _dup2(stderrBackup, stderrFd);
        _close(stdoutBackup);
        _close(stderrBackup);
        _close(g_debugOutputReadFd);
        g_debugOutputReadFd = -1;
        return false;
    }
    CloseHandle((HANDLE)thread);
    _close(stdoutBackup);
    _close(stderrBackup);

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    g_debugOutputRouting = true;
    return true;
}
#endif

bool debugLogOpen(void)
{
#ifdef _WIN32
    if (g_debugLogFile)
    {
        return true;
    }

    std::wstring logPath = pathNearExe(L"DingooPie-debug.log");
    FILE* out = _wfopen(logPath.c_str(), L"w");
    if (out)
    {
        {
            std::lock_guard<std::mutex> lock(g_debugOutputMutex);
            g_debugLogFile = out;
            setvbuf(g_debugLogFile, NULL, _IONBF, 0);
        }
        if (!ensureDebugOutputRouting())
        {
            fprintf(out, "debug-log: failed to route stdout/stderr\n");
            fclose(out);
            std::lock_guard<std::mutex> lock(g_debugOutputMutex);
            g_debugLogFile = NULL;
            return false;
        }
        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);
        printf("debug-log: opened path=DingooPie-debug.log routing=pipe\n");
    }
    else
    {
        printf("debug-log: failed to open path=DingooPie-debug.log\n");
    }
    return g_debugLogFile != NULL;
#else
    if (!g_debugLogFile)
    {
        g_debugLogFile = fopen("DingooPie-debug.log", "w");
        if (g_debugLogFile)
        {
            setvbuf(g_debugLogFile, NULL, _IONBF, 0);
        }
    }
    return g_debugLogFile != NULL;
#endif
}

FILE* debugLogFile(void)
{
    if (!g_debugLogFile)
    {
        debugLogOpen();
    }
    return g_debugLogFile ? g_debugLogFile : stdout;
}

bool debugConsoleOpen(void)
{
#ifdef _WIN32
    if (!g_debugConsoleOpen)
    {
        if (!AllocConsole())
        {
            HWND console = GetConsoleWindow();
            if (!console)
            {
                return false;
            }
        }

        HANDLE output = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        bool outputOwned = output != INVALID_HANDLE_VALUE;
        if (output == INVALID_HANDLE_VALUE)
        {
            output = GetStdHandle(STD_OUTPUT_HANDLE);
        }
        if (output == INVALID_HANDLE_VALUE || output == NULL)
        {
            FreeConsole();
            return false;
        }

        FILE* fp = NULL;
        freopen_s(&fp, "CONIN$", "r", stdin);
        configureDebugConsoleEncoding(output);
        SetConsoleTitleW(L"DingooPie debug console");
        {
            std::lock_guard<std::mutex> lock(g_debugOutputMutex);
            g_debugConsoleOutput = output;
            g_debugConsoleOutputOwned = outputOwned;
            g_debugConsoleOpen = true;
        }
        if (!ensureDebugOutputRouting())
        {
            std::lock_guard<std::mutex> lock(g_debugOutputMutex);
            g_debugConsoleOpen = false;
            closeDebugConsoleOutputLocked();
            FreeConsole();
            return false;
        }
    }
#endif
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    printf("debug-console: opened encoding=utf-8 log=%u\n", g_debugLogFile ? 1u : 0u);
    return true;
}

void debugConsoleClose(void)
{
#ifdef _WIN32
    if (g_debugConsoleOpen)
    {
        printf("debug-console: closing\n");
        std::lock_guard<std::mutex> lock(g_debugOutputMutex);
        closeDebugConsoleOutputLocked();
        g_debugConsoleOpen = false;
        FreeConsole();
    }
#endif
}
