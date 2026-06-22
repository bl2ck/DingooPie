#ifndef DINGOO_PIE_COMPAT_PROFILE_H
#define DINGOO_PIE_COMPAT_PROFILE_H

#include <stdint.h>

#include "execution_backend.h"

// Central registry for content-hash keyed compatibility rules.
// File names are intentionally not used because users often rename .app files.
struct CompatGuestExitDecision
{
    bool matched;
    bool shouldExit;
    const char* label;
};

struct CompatTaskStopExitContext
{
    uint32_t returnAddress;
    bool frontendQuitRequested;
    bool sawSuspiciousFileOpenFailure;
};

struct CompatRuntimeExceptionExitContext
{
    uint32_t pc;
    uint32_t returnAddress;
    uint32_t v0;
};

const char* compatProfileName(const char* appSha256);
double compatDefaultHostDelayScale(const char* appSha256);
CompatGuestExitDecision compatTaskStopGuestExitDecision(const char* appSha256, const CompatTaskStopExitContext* context);
CompatGuestExitDecision compatRuntimeExceptionGuestExitDecision(const char* appSha256, const CompatRuntimeExceptionExitContext* context);
bool compatShouldUseBinResourceView(const char* appSha256);
bool compatForcedBackend(const char* appSha256, ExecutionBackend* backend);

#endif
