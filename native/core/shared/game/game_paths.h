#ifndef DINGOO_PIE_SHARED_GAME_GAME_PATHS_H
#define DINGOO_PIE_SHARED_GAME_GAME_PATHS_H

#include <string>
#include <vector>

enum GameFileFormat
{
    GAME_FILE_FORMAT_APP = 0,
    GAME_FILE_FORMAT_CC,
    GAME_FILE_FORMAT_UNKNOWN
};

static_assert(GAME_FILE_FORMAT_APP == 0 && GAME_FILE_FORMAT_CC == 1 &&
    GAME_FILE_FORMAT_UNKNOWN == 2,
    "GameFileFormat values must stay aligned across frontends");

GameFileFormat gameFileFormatFromPath(const std::string& path);
const char* gamePathExtension(const std::string& path);
bool gamePathHasAppExtension(const std::string& path);
bool gamePathHasCcFamilyExtension(const std::string& path);
bool gamePathHasSupportedExtension(const std::string& path);
bool gamePathsRunRegressionTests(void);
std::string gamePathNormalize(const char* gamePath);
std::string gameFileNameFromPath(const std::string& path);
std::string gamePathStemFromPath(const std::string& path);
std::string gameCheatFileNameFromPath(const std::string& path);
std::string gameLegacyCheatFileNameFromPath(const std::string& path);
std::vector<std::string> gameCheatFileNamesFromPath(const std::string& path);
std::string guestMainPathFromGamePath(const std::string& path);

#endif
