#include "emulator_core.h"
#include "app_paths.h"
#include "debug_console.h"
#include "emulator_options.h"
#include "emulator_settings.h"
#include "frontend_menu.h"
#include "sdl_frontend.h"
#include "platform_win32.h"

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

enum StartupAppSource
{
    STARTUP_APP_NONE,
    STARTUP_APP_COMMAND_LINE,
    STARTUP_APP_RECENT
};

static void clearRecentApp(EmulatorSettings* settings, const char* reason)
{
    if (!settings || settings->lastAppPath.empty())
    {
        return;
    }

    printf("main: clearing recent app after %s: %s\n", reason, settings->lastAppPath.c_str());
    emulatorRemoveRecentApp(settings, settings->lastAppPath);
    emulatorSaveSettings(*settings);
}

static bool launchDetachedSelf(const std::string& appPath)
{
#ifdef _WIN32
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring params;
    if (!appPath.empty())
    {
        params = L"\"" + platformUtf8ToWide(appPath) + L"\"";
    }
    HINSTANCE result = ShellExecuteW(NULL, L"open", exePath,
        params.empty() ? NULL : params.c_str(), NULL, SW_SHOWNORMAL);
    bool ok = (INT_PTR)result > 32;
    printf("main: deferred relaunch %s app=%s\n", ok ? "started" : "failed", appPath.c_str());
    return ok;
#else
    printf("main: deferred relaunch unsupported on this platform app=%s\n", appPath.c_str());
    return false;
#endif
}

int main(int argc, char* argv[])
{
    platformBeginHighResolutionTiming();
    bool externalDebugLog = getenv("DINGOO_PIE_LOG_FILE") != NULL;
    if (externalDebugLog)
    {
        debugLogOpen();
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    EmulatorSettings settings = emulatorLoadSettings();
    if (settings.debugProfile || externalDebugLog)
    {
        debugLogOpen();
    }
    if (settings.showDebugConsole)
    {
        debugConsoleOpen();
    }
    printf("main: settings loaded last_app=%s show_console=%u\n",
        settings.lastAppPath.empty() ? "(empty)" : settings.lastAppPath.c_str(),
        settings.showDebugConsole ? 1u : 0u);
    if (settings.showDebugConsole || settings.debugProfile || externalDebugLog)
    {
        emulatorTraceSettings("loaded", settings);
    }
    emulatorApplyRuntimeSettings(settings);
    EmulatorOptions options = loadEmulatorOptions();

    std::string selectedAppPath;
    StartupAppSource selectedAppSource = STARTUP_APP_NONE;
    selectedAppPath = platformCommandLineAppPath(argc, argv);
    if (!selectedAppPath.empty())
    {
        selectedAppSource = STARTUP_APP_COMMAND_LINE;
    }
    if (selectedAppPath.empty() && !settings.lastAppPath.empty())
    {
        if (!appPathHasAppExtension(settings.lastAppPath))
        {
            clearRecentApp(&settings, "non-app recent path");
        }
        else if (!platformFileExists(settings.lastAppPath))
        {
            clearRecentApp(&settings, "missing recent app");
        }
        else if (!platformProbeAppHeader(settings.lastAppPath))
        {
            clearRecentApp(&settings, "invalid recent app");
        }
        else
        {
            selectedAppPath = settings.lastAppPath;
            selectedAppSource = STARTUP_APP_RECENT;
            printf("main: auto-loading recent app: %s\n", selectedAppPath.c_str());
        }
    }

    if (!selectedAppPath.empty())
    {
        // Do not update settings.lastAppPath here. Recent app persistence is only
        // committed after the runtime exits normally, otherwise a crashing app can
        // poison the next startup.
        printf("main: changing working directory for app\n");
        platformChangeToAppDirectory(selectedAppPath);
        printf("main: working directory ready\n");
    }
    else
    {
        printf("main: no startup app; frontend is waiting for File/Open Game\n");
    }

    printf("main: initializing frontend\n");
    if (!frontendInit(&settings, selectedAppPath.c_str()))
    {
        printf("main: frontend initialization failed\n");
        platformEndHighResolutionTiming();
        return -1;
    }
    printf("main: frontend initialized\n");

    if (!selectedAppPath.empty())
    {
        printf("main: starting selected app\n");
        bool gameStarted = startDingooPie(selectedAppPath.c_str(), options, selectedAppSource == STARTUP_APP_RECENT);
        frontendMenuSetGameRunning(gameStarted);
    }
    frontendRunLoop(options);
    frontendMenuSetGameRunning(false);
    stopDingooPie();
    frontendShutdown();
    std::string relaunchPath;
    if (frontendMenuConsumeRelaunchPath(&relaunchPath))
    {
        launchDetachedSelf(relaunchPath);
    }

    platformEndHighResolutionTiming();
    return 0;
}

