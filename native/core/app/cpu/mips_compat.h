#ifndef DINGOO_PIE_APP_CPU_MIPS_COMPAT_H
#define DINGOO_PIE_APP_CPU_MIPS_COMPAT_H

#include "app/runtime/app_loader.h"
#include "emulator_options.h"

#include "mips_runtime.h"

// Installs precise hooks for guest instructions handled outside the main interpreter.
RuntimeError runtimeCompatInstallHooks(NativeRuntime* runtime, app* appInfo, const EmulatorOptions& options);

#endif
