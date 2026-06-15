#include "emulator_options.h"

#include <cstdlib>
#include <stdio.h>
#include <string.h>

bool emulatorEnvEnabled(const char* name)
{
    const char* value = getenv(name);
    return value && value[0] && strcmp(value, "0") != 0;
}

bool emulatorEnvDisabled(const char* name)
{
    const char* value = getenv(name);
    return value && strcmp(value, "0") == 0;
}

EmulatorOptions loadEmulatorOptions(void)
{
    EmulatorOptions options;
    options.ignoreQuit = emulatorEnvEnabled("DINGOO_PIE_IGNORE_QUIT");
    options.profile = emulatorEnvEnabled("DINGOO_PIE_PROFILE");
    options.compatTrace = emulatorEnvEnabled("DINGOO_PIE_COMPAT_TRACE");
    bool backendRecognized = false;
    options.backend = executionBackendFromName(getenv("DINGOO_PIE_BACKEND"), &backendRecognized);
    if (!backendRecognized)
    {
        printf("DingooPie: unknown DINGOO_PIE_BACKEND, using interpreter\n");
    }
    return options;
}

