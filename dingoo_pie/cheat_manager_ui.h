#ifndef DINGOO_PIE_CHEAT_MANAGER_H
#define DINGOO_PIE_CHEAT_MANAGER_H

#include "emulator_settings.h"

#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

void cheatManagerOpenWindow(
    HWND owner,
    UiLanguage language,
    EmulatorSettings* settings,
    const std::string& currentAppPath);
#endif

#endif
