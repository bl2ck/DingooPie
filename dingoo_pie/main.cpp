#include "emulator_core.h"
#include "app_paths.h"
#include "cheat_runtime.h"
#include "debug_console.h"
#include "emulator_options.h"
#include "emulator_settings.h"
#include "frontend_menu.h"
#include "sdl_frontend.h"
#include "platform_win32.h"

#include <stdio.h>
#include <stdlib.h>
#include <chrono>
#include <string>
#include <string.h>
#include <thread>

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

static void waitForInitialCheatLoad(uint32_t previousRevision)
{
    for (int i = 0; i < 100; ++i)
    {
        if (cheatRuntimeRevision() != previousRevision)
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    frontendMenuRefreshCheats();
}

static void applyStartupDebugSettings(EmulatorSettings* settings, bool externalDebugLog)
{
    if (!settings)
    {
        return;
    }

    if (settings->debugProfile || externalDebugLog)
    {
        if (!debugLogOpen() && settings->debugProfile)
        {
            settings->debugProfile = false;
            printf("main: performance log disabled for this run because DingooPie-debug.log could not be opened\n");
        }
    }
    if (settings->showDebugConsole && !debugConsoleOpen())
    {
        settings->showDebugConsole = false;
        printf("main: debug console disabled for this run because the console could not be opened\n");
    }
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
    applyStartupDebugSettings(&settings, externalDebugLog);
    printf("main: settings loaded last_app=%s debug.show_console=%u debug.profile=%u external_log=%u\n",
        settings.lastAppPath.empty() ? "(empty)" : settings.lastAppPath.c_str(),
        settings.showDebugConsole ? 1u : 0u,
        settings.debugProfile ? 1u : 0u,
        externalDebugLog ? 1u : 0u);
    if (settings.showDebugConsole || settings.debugProfile || externalDebugLog)
    {
        emulatorTraceSettings("loaded", settings);
    }
    emulatorApplyRuntimeSettings(settings);
    cheatRuntimeSetEnabled(settings.cheatsEnabled || emulatorEnvEnabled("DINGOO_PIE_CHEATS"));
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
        uint32_t cheatRevisionBeforeStart = cheatRuntimeRevision();
        bool gameStarted = startDingooPie(
            selectedAppPath.c_str(),
            options,
            selectedAppSource == STARTUP_APP_RECENT,
            emulatorCheatFeatureKeysForApp(settings, selectedAppPath));
        frontendMenuSetGameRunning(gameStarted);
        if (gameStarted)
        {
            waitForInitialCheatLoad(cheatRevisionBeforeStart);
        }
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

