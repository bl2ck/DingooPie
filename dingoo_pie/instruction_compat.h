#ifndef DINGOO_PIE_INSTRUCTION_COMPAT_H
#define DINGOO_PIE_INSTRUCTION_COMPAT_H

#include "app_loader.h"
#include "emulator_options.h"

#include "native_runtime.h"

// Installs precise hooks for guest instructions handled outside the main interpreter.
RuntimeError runtimeCompatInstallHooks(NativeRuntime* runtime, app* appInfo, const EmulatorOptions& options);

#endif
