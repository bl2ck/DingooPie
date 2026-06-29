#include "app_paths.h"

#include <ctype.h>

bool appPathHasAppExtension(const std::string& path)
{
    if (path.size() < 4)
    {
        return false;
    }

    std::string ext = path.substr(path.size() - 4);
    for (size_t i = 0; i < ext.size(); ++i)
    {
        ext[i] = (char)tolower((unsigned char)ext[i]);
    }

    return ext == ".app";
}

std::string appNormalizePath(const char* appPath)
{
    std::string path = (appPath && appPath[0]) ? appPath : "";
    if (path.empty())
    {
        return path;
    }
    if (!appPathHasAppExtension(path))
    {
        path += ".app";
    }
    return path;
}

std::string appFileNameFromPath(const std::string& path)
{
    size_t pos = path.find_last_of("\\/");
    if (pos == std::string::npos)
    {
        return path;
    }

    return path.substr(pos + 1);
}

std::string appCheatFileNameFromPath(const std::string& path)
{
    std::string name = appFileNameFromPath(path);
    if (appPathHasAppExtension(name))
    {
        name.resize(name.size() - 4);
    }
    return name.empty() ? name : name + ".cht";
}

std::string appGuestMainPathFromPath(const std::string& path)
{
    std::string name = appFileNameFromPath(path);
    if (name.empty())
    {
        return name;
    }

    if (appPathHasAppExtension(name))
    {
        name.replace(name.size() - 4, 4, ".app");
        return name;
    }

    return appNormalizePath(name.c_str());
}

