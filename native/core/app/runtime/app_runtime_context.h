#ifndef DINGOO_PIE_APP_RUNTIME_APP_RUNTIME_CONTEXT_H
#define DINGOO_PIE_APP_RUNTIME_APP_RUNTIME_CONTEXT_H

#include <stdint.h>
#include "app/runtime/app_loader.h"


struct AppRuntimeProgramImage
{
    uint32_t address;
    uint32_t size;
    void* data;
    app* package;
};

AppRuntimeProgramImage appRuntimeProgramImage(void);

#endif