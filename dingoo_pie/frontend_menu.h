#ifndef DINGOO_PIE_FRONTEND_MENU_H
#define DINGOO_PIE_FRONTEND_MENU_H

#include "emulator_settings.h"

#include <string>

void frontendMenuAttach(void* nativeWindow, EmulatorSettings* settings, const std::string& currentAppPath);
bool frontendMenuHandleCommand(unsigned int commandId);
void frontendMenuRefresh(void);
bool frontendMenuConsumeRelaunchPath(std::string* outPath);

#endif
