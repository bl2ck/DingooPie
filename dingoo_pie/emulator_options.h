#ifndef DINGOO_PIE_EMULATOR_OPTIONS_H
#define DINGOO_PIE_EMULATOR_OPTIONS_H

#include "execution_backend.h"

struct EmulatorOptions
{
    bool ignoreQuit;
    bool profile;
    bool compatTrace;
    ExecutionBackend backend;
};

bool emulatorEnvEnabled(const char* name);
bool emulatorEnvDisabled(const char* name);
EmulatorOptions loadEmulatorOptions(void);

#endif
