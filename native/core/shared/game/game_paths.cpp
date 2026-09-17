#include "shared/game/game_paths.h"

#include <ctype.h>
#include <string.h>

static bool pathHasExtension(const std::string& path, const char* expected)
{
    const size_t extensionLength = strlen(expected);
    if (path.size() < extensionLength)
    {
        return false;
    }

    const size_t extensionStart = path.size() - extensionLength;
    for (size_t i = 0; i < extensionLength; ++i)
    {
        const unsigned char actual = (unsigned char)path[extensionStart + i];
        const unsigned char wanted = (unsigned char)expected[i];
        if (tolower(actual) != tolower(wanted))
        {
            return false;
        }
    }

    return true;
}

static std::string upperExtension(const char* path)
{
    std::string result = path;
    const size_t extensionStart = result.find_last_of('.');
    if (extensionStart == std::string::npos)
    {
        return result;
    }

    for (size_t i = extensionStart; i < result.size(); ++i)
    {
        result[i] = (char)toupper((unsigned char)result[i]);
    }
    return result;
}

static const char* ccExtensionFromPath(const std::string& path)
{
    static const char* extensions[] = { ".cc", ".c2m", ".c2s", ".c3s" };
    for (const char* extension : extensions)
    {
        if (pathHasExtension(path, extension))
        {
            return extension;
        }
    }
    return NULL;
}

GameFileFormat gameFileFormatFromPath(const std::string& path)
{
    const char* extension = gamePathExtension(path);
    if (!extension)
    {
        return GAME_FILE_FORMAT_UNKNOWN;
    }
    if (strcmp(extension, ".app") == 0)
    {
        return GAME_FILE_FORMAT_APP;
    }
    return GAME_FILE_FORMAT_CC;
}

const char* gamePathExtension(const std::string& path)
{
    if (pathHasExtension(path, ".app"))
    {
        return ".app";
    }
    return ccExtensionFromPath(path);
}

bool gamePathHasAppExtension(const std::string& path)
{
    const char* extension = gamePathExtension(path);
    return extension && strcmp(extension, ".app") == 0;
}

bool gamePathHasCcFamilyExtension(const std::string& path)
{
    const char* extension = gamePathExtension(path);
    return extension && strcmp(extension, ".app") != 0;
}

bool gamePathHasSupportedExtension(const std::string& path)
{
    return gameFileFormatFromPath(path) != GAME_FILE_FORMAT_UNKNOWN;
}

bool gamePathsRunRegressionTests(void)
{
    return gameFileFormatFromPath(upperExtension("game.app")) == GAME_FILE_FORMAT_APP &&
        gameFileFormatFromPath(upperExtension("game.cc")) == GAME_FILE_FORMAT_CC &&
        gameFileFormatFromPath(upperExtension("game.c2s")) == GAME_FILE_FORMAT_CC &&
        gameFileFormatFromPath(upperExtension("game.c3s")) == GAME_FILE_FORMAT_CC &&
        gameFileFormatFromPath(upperExtension("game.c2m")) == GAME_FILE_FORMAT_CC &&
        gameFileFormatFromPath("game.bin") == GAME_FILE_FORMAT_UNKNOWN &&
        gamePathNormalize("game") == "game.app" &&
        gamePathNormalize("game.c3s") == "game.c3s" &&
        gameCheatFileNameFromPath(upperExtension("dir/game.app")) == "game.app.cht" &&
        gameCheatFileNameFromPath(upperExtension("dir/game.cc")) == "game.cc.cht" &&
        gameCheatFileNameFromPath(upperExtension("dir/game.c2m")) == "game.c2m.cht" &&
        gameCheatFileNameFromPath(upperExtension("dir/game.c2s")) == "game.c2s.cht" &&
        gameCheatFileNameFromPath(upperExtension("dir/game.c3s")) == "game.c3s.cht" &&
        gameLegacyCheatFileNameFromPath("dir/game.c2m") == "game.cht" &&
        guestMainPathFromGamePath("dir/game.app") == "game.app" &&
        guestMainPathFromGamePath("dir/game.c3s") == "game.c3s";
}

std::string gamePathNormalize(const char* gamePath)
{
    std::string path = (gamePath && gamePath[0]) ? gamePath : "";
    if (path.empty())
    {
        return path;
    }
    if (!gamePathHasSupportedExtension(path))
    {
        path += ".app";
    }
    return path;
}

std::string gameFileNameFromPath(const std::string& path)
{
    size_t pos = path.find_last_of("\\/");
    if (pos == std::string::npos)
    {
        return path;
    }

    return path.substr(pos + 1);
}

std::string gamePathStemFromPath(const std::string& path)
{
    std::string name = gameFileNameFromPath(path);
    const char* extension = gamePathExtension(name);
    if (extension)
    {
        name.resize(name.size() - strlen(extension));
    }
    return name;
}

std::string gameCheatFileNameFromPath(const std::string& path)
{
    std::string name = gameFileNameFromPath(path);
    const char* extension = gamePathExtension(name);
    if (extension && strcmp(extension, ".app") == 0)
    {
        name = gamePathStemFromPath(name);
        return name.empty() ? name : name + ".app.cht";
    }
    if (extension)
    {
        name = gamePathStemFromPath(name);
        return name.empty() ? name : name + extension + ".cht";
    }
    return name.empty() ? name : name + ".cht";
}

std::string gameLegacyCheatFileNameFromPath(const std::string& path)
{
    std::string name = gamePathStemFromPath(path);
    return name.empty() ? name : name + ".cht";
}

std::vector<std::string> gameCheatFileNamesFromPath(const std::string& path)
{
    std::vector<std::string> names;
    const std::string preferred = gameCheatFileNameFromPath(path);
    if (!preferred.empty())
    {
        names.push_back(preferred);
    }

    const std::string legacy = gameLegacyCheatFileNameFromPath(path);
    if (!legacy.empty() && legacy != preferred)
    {
        names.push_back(legacy);
    }
    return names;
}

std::string guestMainPathFromGamePath(const std::string& path)
{
    std::string name = gameFileNameFromPath(path);
    if (name.empty())
    {
        return name;
    }

    if (gamePathHasAppExtension(name))
    {
        name.replace(name.size() - 4, 4, ".app");
        return name;
    }

    return gamePathNormalize(name.c_str());
}
