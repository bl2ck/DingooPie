#include "debug_console.h"

#include <stdio.h>
#include <string>
#include <wchar.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <io.h>
#include <windows.h>
#endif

static bool g_debugConsoleOpen = false;
static FILE* g_debugLogFile = NULL;

#ifdef _WIN32
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

static void configureDebugConsoleEncoding(void)
{
    // Keep stdio in narrow mode. The codebase writes UTF-8 through printf, and
    // switching to _O_U8TEXT would make narrow printf calls unstable on MinGW.
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
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
#endif

bool debugLogOpen(void)
{
#ifdef _WIN32
    static bool attempted = false;
    if (attempted)
    {
        return g_debugLogFile != NULL;
    }
    attempted = true;

    std::wstring logPath = pathNearExe(L"DingooPie-debug.log");
    FILE* out = _wfreopen(logPath.c_str(), L"w", stdout);
    if (out)
    {
        _dup2(_fileno(stdout), _fileno(stderr));
        g_debugLogFile = out;
        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);
        printf("debug-log: opened path=DingooPie-debug.log\n");
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

        FILE* fp = NULL;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
        freopen_s(&fp, "CONIN$", "r", stdin);
        configureDebugConsoleEncoding();
        SetConsoleTitleW(L"DingooPie debug console");
        g_debugConsoleOpen = true;
    }
#endif
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    printf("debug-console: opened encoding=utf-8\n");
    return true;
}

void debugConsoleClose(void)
{
#ifdef _WIN32
    if (g_debugConsoleOpen)
    {
        printf("debug-console: closing\n");
        FreeConsole();
        g_debugConsoleOpen = false;
    }
#endif
}

bool debugConsoleIsOpen(void)
{
    return g_debugConsoleOpen;
}
