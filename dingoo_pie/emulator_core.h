#ifndef DINGOO_PIE_EMULATOR_CORE_H
#define DINGOO_PIE_EMULATOR_CORE_H

#include "emulator_options.h"

// Starts the guest app on a background native runtime thread.
// If clearRecentOnStartupFailure is true, an initialization failure clears
// recent.last_app only when it still points at this app.
bool startDingooPie(const char* appPath, const EmulatorOptions& options, bool clearRecentOnStartupFailure);
void stopDingooPie(void);

#endif
