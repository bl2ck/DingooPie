#include "startup_command_line.h"

static StartupCommandLineOptions defaultOptions(void)
{
    StartupCommandLineOptions options = {};
    options.action = STARTUP_COMMAND_RUN;
    options.disableRecentStartup = false;
    return options;
}

static StartupCommandLineOptions errorOptions(const std::string& message)
{
    StartupCommandLineOptions options = defaultOptions();
    options.action = STARTUP_COMMAND_ERROR;
    options.error = message;
    return options;
}

static bool optionValue(const std::string& argument,
    const char* longName,
    std::string* value)
{
    if (!longName || !value)
    {
        return false;
    }
    std::string prefix = std::string(longName) + "=";
    if (argument.compare(0, prefix.size(), prefix) != 0)
    {
        return false;
    }
    *value = argument.substr(prefix.size());
    return true;
}

static bool setSingleValue(std::string* destination,
    const std::string& value,
    const char* optionName,
    std::string* error)
{
    if (!destination || !optionName || !error)
    {
        return false;
    }
    if (value.empty())
    {
        *error = std::string(optionName) + " requires a non-empty value";
        return false;
    }
    if (!destination->empty())
    {
        *error = std::string(optionName) + " may only be specified once";
        return false;
    }
    *destination = value;
    return true;
}

StartupCommandLineOptions startupCommandLineParse(
    const std::vector<std::string>& arguments)
{
    StartupCommandLineOptions options = defaultOptions();
    bool positionalOnly = false;
    bool actionSpecified = false;

    for (size_t index = 0; index < arguments.size(); ++index)
    {
        const std::string& argument = arguments[index];
        if (!positionalOnly && argument == "--")
        {
            positionalOnly = true;
            continue;
        }

        StartupCommandLineAction requestedAction = STARTUP_COMMAND_RUN;
        if (!positionalOnly && (argument == "--help" || argument == "-h" || argument == "/?"))
        {
            requestedAction = STARTUP_COMMAND_SHOW_HELP;
        }
        else if (!positionalOnly && (argument == "--version" || argument == "-V"))
        {
            requestedAction = STARTUP_COMMAND_SHOW_VERSION;
        }
        else if (!positionalOnly && argument == "--core-regression")
        {
            requestedAction = STARTUP_COMMAND_CORE_REGRESSION;
        }
        if (requestedAction != STARTUP_COMMAND_RUN)
        {
            if (actionSpecified || !options.gamePath.empty() ||
                !options.settingsPath.empty() || options.disableRecentStartup)
            {
                return errorOptions("help, version, and regression actions must be used alone");
            }
            options.action = requestedAction;
            actionSpecified = true;
            continue;
        }
        if (actionSpecified)
        {
            return errorOptions("help, version, and regression actions must be used alone");
        }

        std::string value;
        if (!positionalOnly && (argument == "--game" || argument == "-g"))
        {
            if (++index >= arguments.size())
            {
                return errorOptions("--game requires a path");
            }
            value = arguments[index];
            if (!setSingleValue(&options.gamePath, value, "--game", &options.error))
            {
                options.action = STARTUP_COMMAND_ERROR;
                return options;
            }
            continue;
        }
        if (!positionalOnly && optionValue(argument, "--game", &value))
        {
            if (!setSingleValue(&options.gamePath, value, "--game", &options.error))
            {
                options.action = STARTUP_COMMAND_ERROR;
                return options;
            }
            continue;
        }
        if (!positionalOnly && (argument == "--config" || argument == "-c"))
        {
            if (++index >= arguments.size())
            {
                return errorOptions("--config requires a path");
            }
            value = arguments[index];
            if (!setSingleValue(&options.settingsPath, value, "--config", &options.error))
            {
                options.action = STARTUP_COMMAND_ERROR;
                return options;
            }
            continue;
        }
        if (!positionalOnly && optionValue(argument, "--config", &value))
        {
            if (!setSingleValue(&options.settingsPath, value, "--config", &options.error))
            {
                options.action = STARTUP_COMMAND_ERROR;
                return options;
            }
            continue;
        }
        if (!positionalOnly && argument == "--no-recent")
        {
            if (options.disableRecentStartup)
            {
                return errorOptions("--no-recent may only be specified once");
            }
            options.disableRecentStartup = true;
            continue;
        }
        if (!positionalOnly && !argument.empty() && argument[0] == '-')
        {
            return errorOptions(std::string("unknown option: ") + argument);
        }
        if (actionSpecified)
        {
            return errorOptions("help, version, and regression actions must be used alone");
        }
        if (!options.gamePath.empty())
        {
            return errorOptions("only one game path may be specified");
        }
        if (argument.empty())
        {
            return errorOptions("game path must not be empty");
        }
        options.gamePath = argument;
    }

    return options;
}

void startupCommandLinePrintUsage(FILE* output)
{
    FILE* target = output ? output : stdout;
    fprintf(target,
        "Usage:\n"
        "  DingooPie.exe [options] [game.app|game.cc]\n\n"
        "Options:\n"
        "  -g, --game <path>       Open one APP or CC game file.\n"
        "  -c, --config <path>     Read and write settings using this INI file.\n"
        "      --no-recent         Do not auto-load the most recent game.\n"
        "  -h, --help              Show this help text.\n"
        "  -V, --version           Show the emulator version.\n"
        "      --core-regression   Run built-in core regression tests.\n"
        "      --                  Treat the following value as the game path.\n\n"
        "Paths containing spaces must be quoted. Exactly one game path is accepted.\n");
}

static bool optionsMatch(const StartupCommandLineOptions& options,
    StartupCommandLineAction action,
    const char* gamePath,
    const char* settingsPath,
    bool disableRecent)
{
    return options.action == action &&
        options.gamePath == (gamePath ? gamePath : "") &&
        options.settingsPath == (settingsPath ? settingsPath : "") &&
        options.disableRecentStartup == disableRecent;
}

bool startupCommandLineRunRegressionTests(void)
{
    bool passed = true;
    passed = passed && optionsMatch(startupCommandLineParse({}),
        STARTUP_COMMAND_RUN, "", "", false);
    passed = passed && optionsMatch(startupCommandLineParse({ "game.app" }),
        STARTUP_COMMAND_RUN, "game.app", "", false);
    passed = passed && optionsMatch(startupCommandLineParse(
        { "--game", "game.cc", "--config", "portable.ini", "--no-recent" }),
        STARTUP_COMMAND_RUN, "game.cc", "portable.ini", true);
    passed = passed && optionsMatch(startupCommandLineParse(
        { "--game=game.app", "--config=portable.ini" }),
        STARTUP_COMMAND_RUN, "game.app", "portable.ini", false);
    passed = passed && optionsMatch(startupCommandLineParse({ "--", "-game.cc" }),
        STARTUP_COMMAND_RUN, "-game.cc", "", false);
    passed = passed && startupCommandLineParse({ "unquoted", "game.app" }).action ==
        STARTUP_COMMAND_ERROR;
    passed = passed && startupCommandLineParse({ "--unknown" }).action ==
        STARTUP_COMMAND_ERROR;
    passed = passed && startupCommandLineParse({ "--game" }).action ==
        STARTUP_COMMAND_ERROR;
    passed = passed && startupCommandLineParse({ "--game=" }).action ==
        STARTUP_COMMAND_ERROR;
    passed = passed && startupCommandLineParse({ "--config=" }).action ==
        STARTUP_COMMAND_ERROR;
    passed = passed && startupCommandLineParse({ "--no-recent", "--no-recent" }).action ==
        STARTUP_COMMAND_ERROR;
    passed = passed && startupCommandLineParse({ "--game", "one.app", "--game", "two.cc" }).action ==
        STARTUP_COMMAND_ERROR;
    passed = passed && startupCommandLineParse({ "--config", "one.ini", "--config", "two.ini" }).action ==
        STARTUP_COMMAND_ERROR;
    passed = passed && startupCommandLineParse({ "--" }).action ==
        STARTUP_COMMAND_RUN;
    passed = passed && startupCommandLineParse({ "--help", "game.app" }).action ==
        STARTUP_COMMAND_ERROR;
    passed = passed && startupCommandLineParse({ "--version", "--config", "test.ini" }).action ==
        STARTUP_COMMAND_ERROR;
    return passed;
}
