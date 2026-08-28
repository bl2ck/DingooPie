#include "app_runtime.h"
#include "app/hle/app_hle.h"
#include "app/save/app_save_state.h"
#include "cc/runtime/cc_timing.h"
#include "config/cheats/cheat_runtime.h"
#include "debug_console.h"
#include "config/settings/emulator_options.h"
#include "config/settings/emulator_settings.h"
#include "frontend_menu.h"
#include "shared/diagnostics/runtime_log.h"
#include "sdl_frontend.h"
#include "platform_win32.h"
#include "startup_command_line.h"
#include "startup_game_selection.h"
#include "app_metadata.h"
#include "shared/game/game_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <chrono>
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

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
            runtimeLogSetProfileEnabled(false);
            printf("main: performance log disabled for this run because the debug log could not be opened\n");
        }
    }
    if (settings->showDebugConsole && !debugConsoleOpen())
    {
        settings->showDebugConsole = false;
        printf("main: debug console disabled for this run because the console could not be opened\n");
    }
}

static bool runCoreRegressionTests(void)
{
    bool semaphorePassed = bridge_run_semaphore_regression();
    bool saveStatePassed = saveStateRunRegressionTests();
    bool ccTimingPassed =
        ccCpuClockToTargetIps(336000000u, 336000000u, 15000000u) == 15000000u &&
        ccScaleTargetIps(15000000u, 0.65) == 9750000u &&
        ccScaleTargetIps(15000000u, 1.0) == 15000000u &&
        ccInstructionsToMicros(9750000u, 9750000u) == 1000000u;
    bool commandLinePassed = startupCommandLineRunRegressionTests();
    printf("core-regression: semaphore=%s save_state=%s cc_timing=%s command_line=%s result=%s\n",
        semaphorePassed ? "pass" : "fail",
        saveStatePassed ? "pass" : "fail",
        ccTimingPassed ? "pass" : "fail",
        commandLinePassed ? "pass" : "fail",
        semaphorePassed && saveStatePassed && ccTimingPassed && commandLinePassed ? "pass" : "fail");
    return semaphorePassed && saveStatePassed && ccTimingPassed && commandLinePassed;
}

int main(int argc, char* argv[])
{
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    StartupCommandLineOptions commandLine = startupCommandLineParse(
        platformCommandLineArguments(argc, argv));
    if (commandLine.action == STARTUP_COMMAND_ERROR)
    {
        fprintf(stderr, "DingooPie: %s\n\n", commandLine.error.c_str());
        startupCommandLinePrintUsage(stderr);
        return 2;
    }
    if (commandLine.action == STARTUP_COMMAND_SHOW_HELP)
    {
        startupCommandLinePrintUsage(stdout);
        return 0;
    }
    if (commandLine.action == STARTUP_COMMAND_SHOW_VERSION)
    {
        printf("%s %s\n", DINGOO_PIE_PRODUCT_NAME, DINGOO_PIE_VERSION_TEXT);
        return 0;
    }

    platformBeginHighResolutionTiming();
    if (commandLine.action == STARTUP_COMMAND_CORE_REGRESSION)
    {
        bool passed = runCoreRegressionTests();
        platformEndHighResolutionTiming();
        return passed ? 0 : 1;
    }

    std::string commandLineGameError;
    if (!startupGameValidateCommandLinePath(commandLine.gamePath, &commandLineGameError))
    {
        fprintf(stderr, "DingooPie: %s\n", commandLineGameError.c_str());
        platformEndHighResolutionTiming();
        return 2;
    }

    if (!commandLine.settingsPath.empty())
    {
        emulatorSetSettingsPathOverride(commandLine.settingsPath);
        printf("main: command-line settings path=%s\n", emulatorSettingsPath().c_str());
    }
    bool externalDebugLog = emulatorEnvEnabled("DINGOO_PIE_LOG_FILE");
    if (externalDebugLog)
    {
        debugLogOpen();
    }

    EmulatorSettings settings = emulatorLoadSettings();
    runtimeLogInitialize(settings.debugProfile, runtimeLogEnvEnabled("DINGOO_PIE_PROFILE"));
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
    emulatorApplySharedRuntimeSettings(settings);
    cheatRuntimeSetEnabled(settings.cheatsEnabled || emulatorEnvEnabled("DINGOO_PIE_CHEATS"));
    EmulatorOptions options = loadEmulatorOptions();

    StartupGameSelection startupGame = startupGameSelect(commandLine, &settings);
    const std::string& selectedAppPath = startupGame.path;

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
        bool gameStarted = gameRuntimeStart(
            selectedAppPath.c_str(),
            options,
            startupGame.source == STARTUP_GAME_RECENT,
            settings.resourceMonitorAutoOpen,
            emulatorCheatFeatureKeysForGame(settings, selectedAppPath));
        frontendMenuSetGameRunning(gameStarted);
        if (gameStarted)
        {
            waitForInitialCheatLoad(cheatRevisionBeforeStart);
        }
    }
    frontendRunLoop(options);
    frontendMenuSetGameRunning(false);
    gameRuntimeStop();
    frontendShutdown();
    std::string relaunchPath;
    if (frontendMenuConsumeRelaunchPath(&relaunchPath))
    {
        launchDetachedSelf(relaunchPath);
    }

    platformEndHighResolutionTiming();
    return 0;
}
