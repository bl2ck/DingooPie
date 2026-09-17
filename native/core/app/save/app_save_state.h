#ifndef DINGOO_PIE_APP_SAVE_APP_SAVE_STATE_H
#define DINGOO_PIE_APP_SAVE_APP_SAVE_STATE_H

#include <string>

#include "app/runtime/app_runtime_state.h"
#include "shared/save/save_state_slots.h"

bool saveStateWriteSlot(const std::string& gamePath, SaveStateFormat format, int slot,
    const AppRuntimeState& state, std::string* error,
    SaveStateProgressCallback progressCallback = 0, void* progressUserData = 0);
bool saveStateReadSlot(const std::string& gamePath, SaveStateFormat format, int slot,
    AppRuntimeState* state, std::string* error,
    SaveStateProgressCallback progressCallback = 0, void* progressUserData = 0);
bool saveStateRunRegressionTests(void);

#endif
