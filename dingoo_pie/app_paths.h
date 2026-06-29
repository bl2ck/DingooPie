#ifndef DINGOO_PIE_APP_PATHS_H
#define DINGOO_PIE_APP_PATHS_H

#include <string>

bool appPathHasAppExtension(const std::string& path);
std::string appNormalizePath(const char* appPath);
std::string appFileNameFromPath(const std::string& path);
std::string appCheatFileNameFromPath(const std::string& path);
std::string appGuestMainPathFromPath(const std::string& path);

#endif

