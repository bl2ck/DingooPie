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
// Releases cached idle-screen textures once gameplay owns the renderer.
void frontendReleaseIdleResources(void);
bool frontendGamePaused(void);
bool frontendUserGamePaused(void);
void frontendSetGamePaused(bool paused);
void frontendToggleGamePaused(void);
void frontendBeginModalPause(void);
void frontendEndModalPause(void);
bool frontendWaitForRuntimePaused(uint32_t timeoutMs);
bool frontendWaitForRuntimePausedWaiters(uint32_t timeoutMs, uint32_t minimumWaiters);
uint32_t frontendRuntimePausedWaiterCount(void);
void frontendClearPauseRequests(void);
void frontendResetInputAfterStateRestore(void);
void frontendApplyVideoSettings(const EmulatorSettings& settings);
void frontendApplyAudioSettings(const EmulatorSettings& settings);
void frontendApplyInputSettings(const EmulatorSettings& settings);
void frontendOpenInputMappingWindow(void);
void frontendOpenResourceMonitorWindow(void);
void frontendOpenMemorySearcherWindow(void);
void frontendOpenDebuggerWindow(void);
void frontendBeginControllerMapping(uint32_t controlBit);
void frontendResetControllerMapping(void);
std::string frontendControllerSourceForControl(uint32_t controlBit);
bool frontendSaveScreenshot(const char* path);
bool frontendSaveScreenshotThumbnail(const char* path, int maxWidth, int maxHeight);
bool frontendSaveAutoScreenshot(void);

void updateFb(void);

#endif
