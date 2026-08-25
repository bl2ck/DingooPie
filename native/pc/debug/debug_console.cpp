#include "debug_console.h"

#include <mutex>
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <time.h>
#include <wchar.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

static bool g_debugConsoleOpen = false;
static FILE* g_debugLogFile = NULL;
static char g_debugLogFileName[64] = "DingooPie-debug-*.log";
#ifdef _WIN32
static wchar_t g_debugLogFileNameW[64] = L"DingooPie-debug-*.log";
#endif

static void debugLogTimestamp(char* out, size_t outSize)
{
    if (!out || outSize == 0)
    {
        return;
    }

    time_t raw = time(NULL);
    struct tm localTime;
#ifdef _WIN32
    localtime_s(&localTime, &raw);
#else
    localtime_r(&raw, &localTime);
#endif

    strftime(out, outSize, "%Y%m%d-%H%M%S", &localTime);
    out[outSize - 1] = '\0';
}

static unsigned long debugLogProcessId(void)
{
#ifdef _WIN32
    return (unsigned long)GetCurrentProcessId();
#else
    return (unsigned long)getpid();
#endif
}

static void debugLogTimestampWide(const char* timestamp, wchar_t* out, size_t outSize)
{
    if (!timestamp || !out || outSize == 0)
    {
        return;
    }

    size_t i = 0;
    for (; i + 1 < outSize && timestamp[i]; ++i)
    {
        out[i] = (wchar_t)(unsigned char)timestamp[i];
    }
    out[i] = L'\0';
}

#ifdef _WIN32
static std::mutex g_debugOutputMutex;
static std::mutex g_debugRoutingMutex;
static bool g_debugOutputRouting = false;
static int g_debugOutputReadFd = -1;
static HANDLE g_debugConsoleOutput = INVALID_HANDLE_VALUE;
static bool g_debugConsoleOutputOwned = false;
static HANDLE g_debugLogMutex = NULL;
static bool g_debugLogMutexOwned = false;

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

static uint64_t hashDebugLogPath(const std::wstring& path)
{
    uint64_t hash = 1469598103934665603ull;
    for (size_t i = 0; i < path.size(); ++i)
    {
        wchar_t ch = path[i];
        if (ch >= L'A' && ch <= L'Z')
        {
            ch = (wchar_t)(ch - L'A' + L'a');
        }
        hash ^= (uint64_t)ch;
        hash *= 1099511628211ull;
    }
    return hash;
}

static bool tryAcquireDebugLogMutex(const std::wstring& primaryLogPath)
{
    if (g_debugLogMutexOwned)
    {
        return true;
    }
    if (!g_debugLogMutex)
    {
        wchar_t mutexName[96] = {};
        swprintf(mutexName, sizeof(mutexName) / sizeof(mutexName[0]),
            L"Local\\DingooPieDebugLog-%016llx",
            (unsigned long long)hashDebugLogPath(primaryLogPath));
        g_debugLogMutex = CreateMutexW(NULL, FALSE, mutexName);
        if (!g_debugLogMutex)
        {
            return false;
        }
    }
    DWORD wait = WaitForSingleObject(g_debugLogMutex, 0);
    if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED)
    {
        g_debugLogMutexOwned = true;
        return true;
    }
    return false;
}

static void buildDebugLogFileName(char* out, size_t outSize, const char* timestamp)
{
    if (!out || outSize == 0 || !timestamp)
    {
        return;
    }
    snprintf(out, outSize, "DingooPie-debug-%s-%lu.log",
        timestamp, debugLogProcessId());
    out[outSize - 1] = '\0';
}

static void buildDebugLogFileNameWide(wchar_t* out, size_t outSize,
    const char* timestamp)
{
    if (!out || outSize == 0 || !timestamp)
    {
        return;
    }

    wchar_t timestampWide[32] = {};
    debugLogTimestampWide(timestamp, timestampWide,
        sizeof(timestampWide) / sizeof(timestampWide[0]));
    swprintf(out, outSize, L"DingooPie-debug-%ls-%lu.log",
        timestampWide, debugLogProcessId());
    out[outSize - 1] = L'\0';
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

    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    if (input != INVALID_HANDLE_VALUE && input != NULL)
    {
        DWORD inputMode = 0;
        if (GetConsoleMode(input, &inputMode))
        {
            inputMode |= ENABLE_EXTENDED_FLAGS;
            inputMode &= ~(ENABLE_QUICK_EDIT_MODE | ENABLE_INSERT_MODE);
            SetConsoleMode(input, inputMode);
        }
    }

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

static HANDLE duplicateDebugOutputHandle(HANDLE output)
{
    if (output == INVALID_HANDLE_VALUE || output == NULL)
    {
        return INVALID_HANDLE_VALUE;
    }

    HANDLE duplicate = INVALID_HANDLE_VALUE;
    if (!DuplicateHandle(GetCurrentProcess(), output, GetCurrentProcess(),
        &duplicate, 0, FALSE, DUPLICATE_SAME_ACCESS))
    {
        return INVALID_HANDLE_VALUE;
    }
    return duplicate;
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

        HANDLE consoleOutput = INVALID_HANDLE_VALUE;
        {
            std::lock_guard<std::mutex> lock(g_debugOutputMutex);
            if (g_debugLogFile)
            {
                fwrite(buffer, 1, (size_t)got, g_debugLogFile);
                fflush(g_debugLogFile);
            }
            if (g_debugConsoleOpen && g_debugConsoleOutput != INVALID_HANDLE_VALUE)
            {
                consoleOutput = duplicateDebugOutputHandle(g_debugConsoleOutput);
            }
        }
        if (consoleOutput != INVALID_HANDLE_VALUE)
        {
            DWORD written = 0;
            WriteFile(consoleOutput, buffer, (DWORD)got, &written, NULL);
            CloseHandle(consoleOutput);
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

#ifndef _WIN32
static void buildDebugLogFileName(char* out, size_t outSize, const char* timestamp)
{
    if (!out || outSize == 0 || !timestamp)
    {
        return;
    }
    snprintf(out, outSize, "DingooPie-debug-%s-%lu.log",
        timestamp, debugLogProcessId());
    out[outSize - 1] = '\0';
}
#endif

bool debugLogOpen(void)
{
#ifdef _WIN32
    if (g_debugLogFile)
    {
        return true;
    }

    char timestamp[32] = {};
    wchar_t logFileNameW[64] = {};
    debugLogTimestamp(timestamp, sizeof(timestamp));
    buildDebugLogFileName(g_debugLogFileName, sizeof(g_debugLogFileName), timestamp);
    buildDebugLogFileNameWide(logFileNameW, sizeof(logFileNameW) / sizeof(logFileNameW[0]),
        timestamp);
    wcscpy_s(g_debugLogFileNameW, logFileNameW);
    std::wstring logPath = pathNearExe(logFileNameW);
    bool logMutexAcquired = tryAcquireDebugLogMutex(logPath);

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
            fprintf(out, "debug-log:route-failed stream=stdout/stderr\n");
            fclose(out);
            std::lock_guard<std::mutex> lock(g_debugOutputMutex);
            g_debugLogFile = NULL;
            return false;
        }
        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);
        // Structured diagnostics use prefix:event followed by compact key=value fields.
        printf("debug-log:opened file=%s routing=pipe mutex=%s\n",
            g_debugLogFileName,
            logMutexAcquired ? "primary" : "unavailable");
    }
    else
    {
        printf("debug-log:open-failed file=%s\n", g_debugLogFileName);
    }
    return g_debugLogFile != NULL;
#else
    if (!g_debugLogFile)
    {
        char timestamp[32] = {};
        debugLogTimestamp(timestamp, sizeof(timestamp));
        buildDebugLogFileName(g_debugLogFileName, sizeof(g_debugLogFileName), timestamp);
        g_debugLogFile = fopen(g_debugLogFileName, "w");
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

const char* debugLogFileName(void)
{
    return g_debugLogFileName;
}

#ifdef _WIN32
const wchar_t* debugLogFileNameWide(void)
{
    return g_debugLogFileNameW;
}
#endif

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
        SetConsoleTitleW(L"DingooPie Debug Console");
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
    printf("debug-console:opened encoding=UTF-8 log=%u\n", g_debugLogFile ? 1u : 0u);
    return true;
}

void debugConsoleClose(void)
{
#ifdef _WIN32
    if (g_debugConsoleOpen)
    {
        printf("debug-console:closing\n");
        std::lock_guard<std::mutex> lock(g_debugOutputMutex);
        closeDebugConsoleOutputLocked();
        g_debugConsoleOpen = false;
        FreeConsole();
    }
#endif
}
