#ifndef DINGOO_PIE_SAVE_STATE_MANAGER_UI_H
#define DINGOO_PIE_SAVE_STATE_MANAGER_UI_H

#include "emulator_settings.h"

#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

typedef bool (*SaveStateManagerSlotAction)(int slot, std::string* error, void* userData);
typedef void (*SaveStateManagerNotify)(void* userData);

struct SaveStateManagerCallbacks
{
    SaveStateManagerSlotAction saveSlot;
    SaveStateManagerSlotAction loadSlot;
    SaveStateManagerNotify changed;
    void* userData;
};

void saveStateManagerOpenWindow(
    HWND owner,
    UiLanguage language,
    const std::string& appPath,
    bool gameRunning,
    const SaveStateManagerCallbacks& callbacks);
#endif

#endif
