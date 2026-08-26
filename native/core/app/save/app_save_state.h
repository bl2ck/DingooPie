#ifndef DINGOO_PIE_APP_SAVE_APP_SAVE_STATE_H
#define DINGOO_PIE_APP_SAVE_APP_SAVE_STATE_H

#include "app/runtime/app_runtime_state.h"
#include "shared/save/save_slots.h"

#include <string>

bool saveStateWriteSlot(const std::string& appPath, SaveStateGameFormat format, int slot,
    const AppRuntimeState& state, std::string* error,
    SaveStateProgressCallback progressCallback = 0, void* progressUserData = 0);
bool saveStateReadSlot(const std::string& appPath, SaveStateGameFormat format, int slot,
    AppRuntimeState* state, std::string* error,
    SaveStateProgressCallback progressCallback = 0, void* progressUserData = 0);
bool saveStateRunRegressionTests(void);

#endif
