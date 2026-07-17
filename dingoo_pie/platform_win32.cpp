#include "platform_win32.h"

#include "app_loader.h"
#include "runtime_debug.h"

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <mmsystem.h>
#include <shellapi.h>
#endif

#ifdef _WIN32
static UINT g_timerPeriodMs = 0;
#endif

std::string platformCommandLineAppPath(int argc, char* argv[])
{
#ifdef _WIN32
    int wideArgc = 0;
    LPWSTR* wideArgv = CommandLineToArgvW(GetCommandLineW(), &wideArgc);
    if (wideArgv)
    {
        std::string path;
        if (wideArgc >= 2)
        {
            path = platformWideToUtf8(wideArgv[1]);
            for (int i = 2; i < wideArgc; ++i)
            {
                path += " ";
                path += platformWideToUtf8(wideArgv[i]);
            }
        }
        LocalFree(wideArgv);
        return path;
    }
#endif

    if (argc < 2)
    {
        return "";
    }

    std::string path = argv[1];
    for (int i = 2; i < argc; ++i)
    {
        path += " ";
        path += argv[i];
    }
    return path;
}

std::string platformWideToUtf8(const std::wstring& text)
{
#ifdef _WIN32
    int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, NULL, 0, NULL, NULL);
    if (size <= 0)
    {
        return "";
    }

    std::string out((size_t)size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, &out[0], size, NULL, NULL);
    return out;
#else
    return WString2String(text);
#endif
}

std::wstring platformUtf8ToWide(const std::string& text)
{
#ifdef _WIN32
    int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, NULL, 0);
    if (size <= 0)
    {
        return L"";
    }

    std::wstring out((size_t)size - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &out[0], size);
    return out;
#else
    return String2WString(text);
#endif
}

unsigned long platformWin32NormalizeChildStyle(const wchar_t* className, unsigned long style)
{
#ifdef _WIN32
    if (className && lstrcmpW(className, L"STATIC") == 0 &&
        (style & SS_ELLIPSISMASK) == 0 &&
        (style & SS_TYPEMASK) == SS_LEFT)
    {
        return (style & ~SS_TYPEMASK) | SS_LEFTNOWORDWRAP;
    }
#else
    (void)className;
#endif
    return style;
}

std::string platformSelectAppPathLocalized(const wchar_t* title, const wchar_t* filter)
{
#ifdef _WIN32
    wchar_t fileName[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = sizeof(fileName);
    ofn.lpstrFilter = filter ? filter : L"Dingoo App (*.app)\0*.app\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = title ? title : L"Select Dingoo .app";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameW(&ofn))
    {
        return platformWideToUtf8(fileName);
    }
#endif

    return "";
}

std::string platformSelectAppPath(void)
{
    return platformSelectAppPathLocalized(NULL, NULL);
}

bool platformFileExists(const std::string& path)
{
    if (path.empty())
    {
        return false;
    }
#ifdef _WIN32
    std::wstring widePath = platformUtf8ToWide(path);
    DWORD attrs = GetFileAttributesW(widePath.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
    FILE* file = fopen(path.c_str(), "rb");
    if (!file)
    {
        return false;
    }
    fclose(file);
    return true;
#endif
}

bool platformProbeAppHeader(const std::string& path)
{
    if (path.empty())
    {
        return false;
    }

#ifdef _WIN32
    FILE* file = _wfopen(platformUtf8ToWide(path).c_str(), L"rb");
#else
    FILE* file = fopen(path.c_str(), "rb");
#endif
    if (!file)
    {
        return false;
    }

    bool ok = false;
    if (fseek(file, 0, SEEK_END) == 0)
    {
        long size = ftell(file);
        ok = size > 0 && (uint64_t)size <= UINT32_MAX &&
            app_probe_file_header(file, (uint32_t)size);
    }
    fclose(file);
    return ok;
}

bool platformChangeToAppDirectory(const std::string& appPath)
{
#ifdef _WIN32
    size_t pos = appPath.find_last_of("\\/");
    if (pos == std::string::npos)
    {
        return true;
    }

    std::string appDir = appPath.substr(0, pos);
    std::wstring appDirW = platformUtf8ToWide(appDir);
    if (!SetCurrentDirectoryW(appDirW.c_str()))
    {
        printf("platform: failed to set app directory: %s\n", appDir.c_str());
        return false;
    }
#else
    (void)appPath;
#endif

    return true;
}

void platformBeginHighResolutionTiming(void)
{
#ifdef _WIN32
    if (g_timerPeriodMs)
    {
        return;
    }

    TIMECAPS caps = {};
    UINT period = 1;
    if (timeGetDevCaps(&caps, sizeof(caps)) == TIMERR_NOERROR)
    {
        period = caps.wPeriodMin ? caps.wPeriodMin : 1;
    }

    if (timeBeginPeriod(period) == TIMERR_NOERROR)
    {
        g_timerPeriodMs = period;
        printf("platform: timer resolution period=%ums\n", (unsigned int)period);
    }
    else
    {
        printf("platform: failed to request high-resolution timer\n");
    }
#endif
}

void platformEndHighResolutionTiming(void)
{
#ifdef _WIN32
    if (g_timerPeriodMs)
    {
        timeEndPeriod(g_timerPeriodMs);
        g_timerPeriodMs = 0;
    }
#endif
}
