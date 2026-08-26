#include "platform_win32.h"

#include "shared/services/guest_package.h"
#include "app_runtime_debug.h"

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <sys/stat.h>
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
    ofn.lpstrFilter = filter ? filter : L"Dingoo Games (*.app;*.cc)\0*.app;*.cc\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = title ? title : L"Select Dingoo game";
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

static std::string platformParentDirectory(const std::string& path)
{
    size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? "." : path.substr(0, slash);
}

static bool platformEnsureDirectory(const std::string& path)
{
    if (path.empty())
    {
        return false;
    }
    std::wstring wide = platformUtf8ToWide(path);
    DWORD attrs = GetFileAttributesW(wide.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES)
    {
        return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }
    size_t slash = path.find_last_of("\\/");
    if (slash != std::string::npos && slash > 0 &&
        !platformEnsureDirectory(path.substr(0, slash)))
    {
        return false;
    }
    return CreateDirectoryW(wide.c_str(), NULL) != 0 || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool platformChangeToGameDirectory(const std::string& gamePath)
{
    return platformChangeToAppDirectory(gamePath);
}

FILE* platformOpenGameFile(const std::string& path)
{
    return _wfopen(platformUtf8ToWide(path).c_str(), L"rb");
}

FILE* platformOpenGameSiblingFile(const std::string& gamePath,
    const std::string& fileName)
{
    std::string path = platformParentDirectory(gamePath) + "\\" + fileName;
    return _wfopen(platformUtf8ToWide(path).c_str(), L"rb");
}

static std::string platformGetSaveDirectory(const std::string& gamePath,
    const std::string& gameIdentity)
{
    std::string identity = gameIdentity.empty() ? "default" : gameIdentity;
    std::string directory = platformParentDirectory(gamePath) + "\\saves\\" + identity;
    return platformEnsureDirectory(directory) ? directory : "";
}

std::string platformGetAppSaveDirectory(const std::string& gamePath,
    const std::string& gameIdentity)
{
    return platformGetSaveDirectory(gamePath, gameIdentity);
}

std::string platformGetCcSaveDirectory(const std::string& gamePath,
    const std::string& gameIdentity)
{
    return platformGetSaveDirectory(gamePath, gameIdentity);
}

std::string platformGetLogDirectory(void)
{
    wchar_t modulePath[MAX_PATH] = {};
    GetModuleFileNameW(NULL, modulePath, MAX_PATH);
    return platformParentDirectory(platformWideToUtf8(modulePath));
}

bool platformIsPrivateStorageDirectory(const std::string& directoryUri)
{
    return !directoryUri.empty();
}

FILE* platformOpenStorageFile(const std::string& directoryUri,
    const std::string& fileName, const char* mode)
{
    if (!platformEnsureDirectory(directoryUri))
    {
        return NULL;
    }
    std::string path = directoryUri + "\\" + fileName;
    std::wstring wideMode = platformUtf8ToWide(mode ? mode : "rb");
    return _wfopen(platformUtf8ToWide(path).c_str(), wideMode.c_str());
}

bool platformDeleteStorageFile(const std::string& directoryUri,
    const std::string& fileName)
{
    std::string path = directoryUri + "\\" + fileName;
    return DeleteFileW(platformUtf8ToWide(path).c_str()) != 0 ||
        GetLastError() == ERROR_FILE_NOT_FOUND;
}

uint64_t platformGetStorageFileModifiedTime(const std::string& directoryUri,
    const std::string& fileName)
{
    std::string path = directoryUri + "\\" + fileName;
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (!GetFileAttributesExW(platformUtf8ToWide(path).c_str(),
        GetFileExInfoStandard, &data))
    {
        return 0;
    }
    ULARGE_INTEGER value;
    value.LowPart = data.ftLastWriteTime.dwLowDateTime;
    value.HighPart = data.ftLastWriteTime.dwHighDateTime;
    return value.QuadPart;
}

FILE* platformOpenHostFile(const std::string& path, const char* mode)
{
#ifdef _WIN32
    std::wstring wideMode = platformUtf8ToWide(mode ? mode : "rb");
    return _wfopen(platformUtf8ToWide(path).c_str(), wideMode.c_str());
#else
    return fopen(path.c_str(), mode ? mode : "rb");
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
            guestPackageProbeFileHeader(file, (uint32_t)size);
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
