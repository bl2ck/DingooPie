#ifndef DINGOO_PIE_APP_RUNTIME_APP_RUNTIME_CONTEXT_H
#define DINGOO_PIE_APP_RUNTIME_APP_RUNTIME_CONTEXT_H

#include <stdint.h>

struct GuestPackage;

struct AppRuntimeProgramImage
{
    uint32_t address;
    uint32_t size;
    void* data;
    GuestPackage* package;
};

AppRuntimeProgramImage appRuntimeProgramImage(void);

#endif
