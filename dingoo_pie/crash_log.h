#ifndef DINGOO_PIE_CRASH_LOG_H
#define DINGOO_PIE_CRASH_LOG_H

#include "execution_backend.h"
#include "native_runtime.h"

#include <string>

struct CrashLogContext
{
    const char* appPath;
    const char* appMainPath;
    const char* appSha256;
    const char* compatProfile;
    ExecutionBackend backend;
    uint32_t appEntry;
    uint32_t bootEntry;
    uint32_t origin;
    uint32_t appSize;
};

bool crashLogWriteGuestFailure(
    NativeRuntime* runtime,
    RuntimeError err,
    const CrashLogContext& context,
    std::string* outFileName);

#endif
