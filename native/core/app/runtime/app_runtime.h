#ifndef DINGOO_PIE_APP_RUNTIME_APP_RUNTIME_H
#define DINGOO_PIE_APP_RUNTIME_APP_RUNTIME_H

#include "config/settings/emulator_options.h"
#include "app/runtime/app_runtime_state.h"

#include <stdint.h>
#include <string>
#include <vector>

// Starts the guest app on a background native runtime thread.
// clearRecentOnStartupFailure only clears recent.last_app if it still points here.
// enableResourceMonitor arms capture before the runtime thread starts.
bool appRuntimeStart(
    const char* appPath,
    const EmulatorOptions& options,
    bool clearRecentOnStartupFailure,
    bool enableResourceMonitor,
    const std::vector<std::string>& enabledCheatFeatureKeys);
void appRuntimeApplySettings(void);
bool appRuntimeStop(void);
void appRuntimeSuppressRecentGameSave(void);
bool appRuntimeCaptureState(AppRuntimeState* out, std::string* error);
bool appRuntimeRestoreState(const AppRuntimeState& state, std::string* error);
void appRuntimeNotifyPauseRequested(void);
uint32_t appRuntimeActiveThreadCount(void);

#endif
