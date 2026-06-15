#ifndef DINGOO_PIE_COMPAT_PROFILE_H
#define DINGOO_PIE_COMPAT_PROFILE_H

#include <stdint.h>

#include "execution_backend.h"

// Central registry for content-hash keyed compatibility rules.
// File names are intentionally not used because users often rename .app files.
const char* compatProfileName(const char* appSha256);
double compatDefaultHostDelayScale(const char* appSha256);
bool compatShouldPromoteTaskStopToGuestExit(const char* appSha256, uint32_t returnAddress);
bool compatShouldUseBinResourceView(const char* appSha256);
bool compatForcedBackend(const char* appSha256, ExecutionBackend* backend);

#endif
