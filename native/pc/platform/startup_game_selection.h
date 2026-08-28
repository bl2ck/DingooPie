#ifndef DINGOO_PIE_STARTUP_GAME_SELECTION_H
#define DINGOO_PIE_STARTUP_GAME_SELECTION_H

#include <string>

struct EmulatorSettings;
struct StartupCommandLineOptions;

enum StartupGameSource
{
    STARTUP_GAME_NONE = 0,
    STARTUP_GAME_COMMAND_LINE,
    STARTUP_GAME_RECENT
};

struct StartupGameSelection
{
    StartupGameSource source;
    std::string path;
};

bool startupGameValidateCommandLinePath(const std::string& path, std::string* error);
StartupGameSelection startupGameSelect(const StartupCommandLineOptions& commandLine,
    EmulatorSettings* settings);

#endif
