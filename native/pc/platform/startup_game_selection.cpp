#include "startup_game_selection.h"

#include "config/settings/emulator_settings.h"
#include "shared/game/game_paths.h"
#include "platform_win32.h"
#include "startup_command_line.h"

#include <stdio.h>

static StartupGameSelection emptySelection(void)
{
    StartupGameSelection selection = {};
    selection.source = STARTUP_GAME_NONE;
    return selection;
}

static void clearInvalidRecentGame(EmulatorSettings* settings, const char* reason)
{
    if (!settings || settings->lastAppPath.empty())
    {
        return;
    }

    printf("main: clearing recent app after %s: %s\n",
        reason, settings->lastAppPath.c_str());
    emulatorRemoveRecentApp(settings, settings->lastAppPath);
    emulatorSaveSettings(*settings);
}

bool startupGameValidateCommandLinePath(const std::string& path, std::string* error)
{
    if (!error)
    {
        return false;
    }
    error->clear();
    if (path.empty())
    {
        return true;
    }
    if (!gamePathHasSupportedExtension(path))
    {
        *error = "game path must end in .app or .cc";
        return false;
    }
    if (!platformFileExists(path))
    {
        *error = "game file does not exist: " + path;
        return false;
    }
    if (!platformProbeGameHeader(path))
    {
        *error = "game file has an invalid or unsupported package header: " + path;
        return false;
    }
    return true;
}

StartupGameSelection startupGameSelect(const StartupCommandLineOptions& commandLine,
    EmulatorSettings* settings)
{
    StartupGameSelection selection = emptySelection();
    if (commandLine.disableRecentStartup)
    {
        printf("main: recent game auto-start disabled by command line\n");
    }
    if (!commandLine.gamePath.empty())
    {
        selection.source = STARTUP_GAME_COMMAND_LINE;
        selection.path = commandLine.gamePath;
        return selection;
    }
    if (commandLine.disableRecentStartup)
    {
        return selection;
    }
    if (!settings || settings->lastAppPath.empty())
    {
        return selection;
    }
    if (!gamePathHasSupportedExtension(settings->lastAppPath))
    {
        clearInvalidRecentGame(settings, "unsupported recent game path");
        return selection;
    }
    if (!platformFileExists(settings->lastAppPath))
    {
        clearInvalidRecentGame(settings, "missing recent game");
        return selection;
    }
    if (!platformProbeGameHeader(settings->lastAppPath))
    {
        clearInvalidRecentGame(settings, "invalid recent game");
        return selection;
    }

    selection.source = STARTUP_GAME_RECENT;
    selection.path = settings->lastAppPath;
    printf("main: auto-loading recent app: %s\n", selection.path.c_str());
    return selection;
}
