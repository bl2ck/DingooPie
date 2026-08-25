#ifndef DINGOO_PIE_APP_SAVE_APP_SAVE_STATE_H
#define DINGOO_PIE_APP_SAVE_APP_SAVE_STATE_H

#include "app_runtime.h"
#include "shared/save/save_slots.h"

#include <string>

std::string saveStateAppIdForPath(const std::string& appPath);
std::string saveStatePathForSlot(const std::string& appPath, int slot);
std::string saveStateThumbnailPathForSlot(const std::string& appPath, int slot);
SaveStateSlotInfo saveStateSlotInfo(const std::string& appPath, int slot);
bool saveStateWriteSlot(const std::string& appPath, int slot,
    const AppRuntimeState& state, std::string* error,
    SaveStateProgressCallback progressCallback = 0, void* progressUserData = 0);
bool saveStateReadSlot(const std::string& appPath, int slot,
    AppRuntimeState* state, std::string* error,
    SaveStateProgressCallback progressCallback = 0, void* progressUserData = 0);

#endif
