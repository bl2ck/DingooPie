#ifndef DINGOO_PIE_DEBUGGER_UI_H
#define DINGOO_PIE_DEBUGGER_UI_H

#include "emulator_settings.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

void debuggerUiOpenWindow(HWND owner, UiLanguage language);
#endif

#endif
