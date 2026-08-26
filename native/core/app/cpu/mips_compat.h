#ifndef DINGOO_PIE_APP_CPU_MIPS_COMPAT_H
#define DINGOO_PIE_APP_CPU_MIPS_COMPAT_H

#include "shared/services/guest_package.h"
#include "config/settings/emulator_options.h"

#include "app/cpu/mips_runtime.h"

// Installs precise hooks for guest instructions handled outside the main interpreter.
RuntimeError runtimeCompatInstallHooks(NativeRuntime* runtime, GuestPackage* appInfo, const EmulatorOptions& options);

#endif
