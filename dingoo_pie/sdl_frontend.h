#ifndef DINGOO_PIE_SDL_FRONTEND_H
#define DINGOO_PIE_SDL_FRONTEND_H

#include "emulator_options.h"
#include "emulator_settings.h"

#include <stdint.h>

// Owns SDL video, event polling, and framebuffer presentation.
bool frontendInit(EmulatorSettings* settings, const char* currentAppPath);
void frontendRunLoop(const EmulatorOptions& options);
void frontendRequestQuit(void);
bool frontendQuitRequested(void);
void frontendShutdown(void);
bool frontendGamePaused(void);
void frontendSetGamePaused(bool paused);
void frontendToggleGamePaused(void);
void frontendApplyVideoSettings(const EmulatorSettings& settings);
void frontendApplyAudioSettings(const EmulatorSettings& settings);
void frontendApplyInputSettings(const EmulatorSettings& settings);
void frontendOpenInputMappingWindow(void);
void frontendBeginControllerMapping(uint32_t controlBit);
void frontendResetControllerMapping(void);
std::string frontendControllerSourceForControl(uint32_t controlBit);
bool frontendSaveScreenshot(const char* path);
bool frontendSaveAutoScreenshot(void);

void updateFb(void);

#endif

