#ifndef DINGOO_PIE_STARTUP_COMMAND_LINE_H
#define DINGOO_PIE_STARTUP_COMMAND_LINE_H

#include <stdio.h>
#include <string>
#include <vector>

enum StartupCommandLineAction
{
    STARTUP_COMMAND_RUN = 0,
    STARTUP_COMMAND_SHOW_HELP,
    STARTUP_COMMAND_SHOW_VERSION,
    STARTUP_COMMAND_CORE_REGRESSION,
    STARTUP_COMMAND_ERROR
};

struct StartupCommandLineOptions
{
    StartupCommandLineAction action;
    std::string gamePath;
    std::string settingsPath;
    bool disableRecentStartup;
    std::string error;
};

StartupCommandLineOptions startupCommandLineParse(
    const std::vector<std::string>& arguments);
void startupCommandLinePrintUsage(FILE* output);
bool startupCommandLineRunRegressionTests(void);

#endif
