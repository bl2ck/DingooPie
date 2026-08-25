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

    std::string ext = path.substr(path.size() - extensionLength);
    for (size_t i = 0; i < ext.size(); ++i)
    {
        ext[i] = (char)tolower((unsigned char)ext[i]);
    }

    return ext == expected;
}

GameFormat gameFormatFromPath(const std::string& path)
{
    if (gamePathHasAppExtension(path))
    {
        return GAME_FORMAT_APP;
    }
    if (gamePathHasCcExtension(path))
    {
        return GAME_FORMAT_CC;
    }
    return GAME_FORMAT_UNKNOWN;
}

bool gamePathHasAppExtension(const std::string& path)
{
    return pathHasExtension(path, ".app");
}

bool gamePathHasCcExtension(const std::string& path)
{
    return pathHasExtension(path, ".cc");
}

bool gamePathHasSupportedExtension(const std::string& path)
{
    return gameFormatFromPath(path) != GAME_FORMAT_UNKNOWN;
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

std::string gameCheatFileNameFromPath(const std::string& path)
{
    std::string name = gameFileNameFromPath(path);
    if (gamePathHasAppExtension(name))
    {
        name.resize(name.size() - 4);
        return name.empty() ? name : name + ".app.cht";
    }
    if (gamePathHasCcExtension(name))
    {
        name.resize(name.size() - 3);
        return name.empty() ? name : name + ".cc.cht";
    }
    return name.empty() ? name : name + ".cht";
}

std::string gameLegacyCheatFileNameFromPath(const std::string& path)
{
    std::string name = gameFileNameFromPath(path);
    if (gamePathHasAppExtension(name))
    {
        name.resize(name.size() - 4);
    }
    else if (gamePathHasCcExtension(name))
    {
        name.resize(name.size() - 3);
    }
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

std::string appGuestMainPathFromGamePath(const std::string& path)
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
