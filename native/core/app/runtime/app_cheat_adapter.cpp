#include "app/runtime/app_cheat_adapter.h"

#include "app/cpu/mips_runtime.h"
#include "config/cheats/cheat_runtime.h"

static bool appCheatRead(void* userData, uint32_t address, void* out, size_t size)
{
    return nativeRuntimeReadRaw((NativeRuntime*)userData, address, out, size);
}

static bool appCheatWrite(void* userData, uint32_t address, const void* in, size_t size)
{
    return nativeRuntimeWriteRaw((NativeRuntime*)userData, address, in, size);
}

static void appCheatFlush(void* userData)
{
    nativeRuntimeFlushCodeCache((NativeRuntime*)userData);
}

void appCheatBind(NativeRuntime* runtime)
{
    cheatRuntimeBindMemory(runtime, appCheatRead, appCheatWrite, appCheatFlush);
}

void appCheatUnbind(NativeRuntime* runtime)
{
    cheatRuntimeUnbindMemory(runtime);
}

void appCheatApplyStartup(NativeRuntime*)
{
    cheatRuntimeApplyStartupBound();
}
