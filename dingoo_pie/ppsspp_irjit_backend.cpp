#include "ppsspp_irjit_backend.h"

#include <stdio.h>
#include <string.h>
#include <mutex>

#ifdef DINGOO_PIE_ENABLE_PPSSPP_IRJIT
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/x86/X64IRJit.h"
#endif

static bool traceIrJitBackend(void)
{
    static int enabled = -1;
    if (enabled < 0)
    {
        const char* value = getenv("DINGOO_PIE_IRJIT_TRACE");
        enabled = value && value[0] && strcmp(value, "0") != 0 ? 1 : 0;
    }
    return enabled != 0;
}

#ifdef DINGOO_PIE_ENABLE_PPSSPP_IRJIT
static std::mutex g_ppssppIrJitRunMutex;
#endif

#ifdef DINGOO_PIE_ENABLE_PPSSPP_IRJIT
static void copyRuntimeToPpssppState(NativeRuntime* runtime, MIPSState* state, uint32_t begin)
{
    state->Init();
    uint32_t* gpr = nativeRuntimeGpr(runtime);
    uint32_t* hi = nativeRuntimeHi(runtime);
    uint32_t* lo = nativeRuntimeLo(runtime);
    float* fpr = nativeRuntimeFpr(runtime);
    float* vfpu = nativeRuntimeVfpu(runtime);
    uint32_t* vfpuCtrl = nativeRuntimeVfpuCtrl(runtime);
    uint32_t* fcr31 = nativeRuntimeFcr31(runtime);
    uint32_t* fpcond = nativeRuntimeFpCond(runtime);
    if (gpr)
    {
        memcpy(state->r, gpr, sizeof(state->r));
        state->r[0] = 0;
    }
    if (fpr)
    {
        memcpy(state->f, fpr, sizeof(state->f));
    }
    if (vfpu)
    {
        memcpy(state->v, vfpu, sizeof(state->v));
    }
    if (vfpuCtrl)
    {
        memcpy(state->vfpuCtrl, vfpuCtrl, sizeof(state->vfpuCtrl));
    }
    state->pc = begin;
    if (hi)
    {
        state->hi = *hi;
    }
    if (lo)
    {
        state->lo = *lo;
    }
    if (fcr31)
    {
        state->fcr31 = *fcr31;
    }
    if (fpcond)
    {
        state->fpcond = *fpcond;
    }
    state->downcount = 100000;
    currentMIPS = state;
}

static void copyPpssppStateToRuntime(NativeRuntime* runtime, const MIPSState* state)
{
    uint32_t* gpr = nativeRuntimeGpr(runtime);
    uint32_t* pc = nativeRuntimePc(runtime);
    uint32_t* hi = nativeRuntimeHi(runtime);
    uint32_t* lo = nativeRuntimeLo(runtime);
    float* fpr = nativeRuntimeFpr(runtime);
    float* vfpu = nativeRuntimeVfpu(runtime);
    uint32_t* vfpuCtrl = nativeRuntimeVfpuCtrl(runtime);
    uint32_t* fcr31 = nativeRuntimeFcr31(runtime);
    uint32_t* fpcond = nativeRuntimeFpCond(runtime);
    if (gpr)
    {
        memcpy(gpr, state->r, sizeof(state->r));
        gpr[0] = 0;
    }
    if (fpr)
    {
        memcpy(fpr, state->f, sizeof(state->f));
    }
    if (vfpu)
    {
        memcpy(vfpu, state->v, sizeof(state->v));
    }
    if (vfpuCtrl)
    {
        memcpy(vfpuCtrl, state->vfpuCtrl, sizeof(state->vfpuCtrl));
    }
    if (pc)
    {
        *pc = state->pc;
    }
    if (hi)
    {
        *hi = state->hi;
    }
    if (lo)
    {
        *lo = state->lo;
    }
    if (fcr31)
    {
        *fcr31 = state->fcr31;
    }
    if (fpcond)
    {
        *fpcond = state->fpcond;
    }
}
#endif

static void printRuntimeMemoryMap(NativeRuntime* runtime)
{
    if (!traceIrJitBackend())
    {
        return;
    }

    size_t regionCount = nativeRuntimeMemoryRegionCount(runtime);
    printf("ppsspp-irjit: runtime memory regions=%u\n", (unsigned)regionCount);
    for (size_t i = 0; i < regionCount; ++i)
    {
        RuntimeMemoryRegion region;
        if (!nativeRuntimeGetMemoryRegion(runtime, i, &region))
        {
            continue;
        }
        printf("ppsspp-irjit: map[%u] guest=0x%08x size=0x%08x host=%p perms=0x%x\n",
            (unsigned)i, region.start, region.size, region.data, region.perms);
    }
}

bool ppssppIrJitBackendAvailable(void)
{
#ifdef DINGOO_PIE_ENABLE_PPSSPP_IRJIT
    return true;
#else
    return false;
#endif
}

RuntimeError ppssppIrJitStart(NativeRuntime* runtime, uint64_t begin, uint64_t until, uint64_t timeout, size_t count)
{
    printRuntimeMemoryMap(runtime);

#ifdef DINGOO_PIE_ENABLE_PPSSPP_IRJIT
    std::lock_guard<std::mutex> runLock(g_ppssppIrJitRunMutex);
    ppssppShimAttachRuntime(runtime);

    MIPSState state;
    copyRuntimeToPpssppState(runtime, &state, (uint32_t)begin);
    uint64_t beginTicks = 0;
    uint64_t maxTicks = 0;
    {
        // Use PPSSPP's native x64 IR backend. Dingoo SDK calls are installed as
        // per-PC hooks, so the shim disables native block linking and routes
        // block exits back through the dispatcher before SDK/HLE entry points.
        MIPSComp::X64IRJit jit(&state);
        MIPSComp::jit = &jit;

        beginTicks = CoreTiming::GetTicks();
        maxTicks = count ? (uint64_t)count * 64 : 0;
        if (timeout && (maxTicks == 0 || timeout < maxTicks))
        {
            maxTicks = timeout;
        }
        ppssppShimSetRuntimeLimit(beginTicks, maxTicks);

        if (traceIrJitBackend())
        {
            printf("ppsspp-irjit: compiling block at 0x%08x\n", (uint32_t)begin);
        }
        jit.Compile((uint32_t)begin);
        if (traceIrJitBackend())
        {
            printf("ppsspp-irjit: entering dispatcher at 0x%08x\n", state.pc);
        }
        for (;;)
        {
            jit.RunLoopUntil(maxTicks ? beginTicks + maxTicks : 0);
            if (!ppssppShimWaitForPauseResume(runtime))
            {
                break;
            }
        }
        copyPpssppStateToRuntime(runtime, &state);

        // The PPSSPP IR block cache restores emuhack markers during cleanup.
        // Keep the Dingoo runtime attached until the JIT object is destroyed so
        // those unchecked memory paths still resolve through the Dingoo shim.
        jit.SaveAndClearEmuHackOps();
    }
    MIPSComp::jit = NULL;
    ppssppShimDetachRuntime(runtime);
    if (traceIrJitBackend())
    {
        printf("ppsspp-irjit: dispatcher stopped pc=0x%08x core=%s\n", state.pc, CoreStateToString(coreState));
    }
    if (until != 0xffffffffu && state.pc == (uint32_t)until)
    {
        return RUNTIME_OK;
    }
    if (coreState == CORE_RUNTIME_ERROR)
    {
        return RUNTIME_ERROR_EXCEPTION;
    }
    return RUNTIME_OK;
#else
    printf("DingooPie: PPSSPP IR JIT backend is not compiled into this build\n");
    return RUNTIME_ERROR_BACKEND_UNAVAILABLE;
#endif
}

RuntimeError ppssppIrJitFlushCodeCache(NativeRuntime* runtime)
{
    if (!ppssppIrJitBackendAvailable())
    {
        return RUNTIME_ERROR_BACKEND_UNAVAILABLE;
    }
    ppssppShimClearJitCache(runtime);
    return RUNTIME_OK;
}

void ppssppIrJitRequestStop(NativeRuntime* runtime)
{
    (void)runtime;
#ifdef DINGOO_PIE_ENABLE_PPSSPP_IRJIT
    ppssppShimRequestStop();
#endif
}
