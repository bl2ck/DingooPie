#ifndef DINGOO_PIE_FRONTEND_MENU_H
#define DINGOO_PIE_FRONTEND_MENU_H

#include "emulator_settings.h"

#include <string>

void frontendMenuAttach(void* nativeWindow, EmulatorSettings* settings, const std::string& currentAppPath);
bool frontendMenuHandleCommand(unsigned int commandId);
void frontendMenuRefresh(void);
void frontendMenuSetGameRunning(bool running);
bool frontendMenuRequestOpenApp(const std::string& appPath);
bool frontendMenuConsumeRelaunchPath(std::string* outPath);

#endif
