#include "native_runtime.h"

#include "ppsspp_irjit_backend.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctype.h>
#include <mutex>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unordered_map>
#include <vector>

#include "framebuffer.h"
#include "pause_gate.h"

struct MemoryRegion
{
    uint32_t start;
    uint32_t size;
    uint8_t* data;
    uint32_t perms;
};

struct HookEntry
{
    RuntimeHook handle;
    int type;
    uint32_t begin;
    uint32_t end;
    void* callback;
    void* userData;
    bool active;
};

static const uint32_t kCodeHookPageShift = 12;
static const uint32_t kCodeHookPageSize = 1u << kCodeHookPageShift;
static const size_t kCodeHookPageCount = 1u << (32 - kCodeHookPageShift);
static const uint32_t kCodeHookExactSlotsPerPage = kCodeHookPageSize / 4;
static const uint8_t kCodeHookPageExact = 1;
static const uint8_t kCodeHookPageRange = 2;

struct CodeHookExactPage
{
    uint32_t listIndexBySlot[kCodeHookExactSlotsPerPage];
};

struct NativeRuntime
{
    uint32_t gpr[32];
    float fpr[32];
    float vfpu[128];
    uint32_t vfpuCtrl[16];
    uint32_t pc;
    uint32_t hi;
    uint32_t lo;
    uint32_t fcr31;
    uint32_t fpcond;
    ExecutionBackend backend;
    RuntimeError lastError;
    std::atomic<bool> stopRequested;
    std::vector<MemoryRegion> regions;
    MemoryRegion cachedRegion;
    bool cachedRegionValid;
    MemoryRegion cachedFetchRegion;
    bool cachedFetchRegionValid;
    std::mutex hookMutex;
    std::vector<HookEntry> hooks;
    std::unordered_map<uint32_t, std::vector<size_t> > codeHookIndex;
    std::vector<size_t> rangedCodeHooks;
    std::vector<uint8_t> codeHookPageMap;
    std::vector<uint32_t> exactCodeHookPageIndex;
    std::vector<CodeHookExactPage> exactCodeHookPages;
    std::vector<std::vector<size_t> > exactCodeHookLists;
    bool hasRangedCodeHooks;
    RuntimeHook nextHook;
    bool interpreterProfileEnabled;
    uint64_t interpreterProfileLastMicros;
    uint64_t interpreterProfileInstructions;
    uint64_t interpreterProfileCheckCountdown;
    uint64_t interpreterProfileHooks;
    bool interpreterPcProfileEnabled;
    uint64_t interpreterPcSampleCountdown;
};

static uint32_t signExtend16(uint32_t value)
{
    return (value & 0x8000) ? (value | 0xffff0000u) : value;
}

static uint32_t signExtend8(uint32_t value)
{
    return (value & 0x80) ? (value | 0xffffff00u) : value;
}

static uint32_t loadLe32(const uint8_t* p);

static uint32_t countLeadingZeros32(uint32_t value)
{
    if (value == 0)
    {
        return 32;
    }

    uint32_t count = 0;
    while ((value & 0x80000000u) == 0)
    {
        count++;
        value <<= 1;
    }
    return count;
}

static bool addressInRange(uint32_t address, const MemoryRegion& region, size_t size)
{
    uint64_t start = region.start;
    uint64_t end = start + region.size;
    uint64_t req = address;
    return req >= start && req + size <= end;
}

static MemoryRegion* findRegion(NativeRuntime* runtime, uint32_t address, size_t size)
{
    if (runtime->cachedRegionValid && addressInRange(address, runtime->cachedRegion, size))
    {
        return &runtime->cachedRegion;
    }

    std::vector<MemoryRegion>::iterator it = std::upper_bound(
        runtime->regions.begin(), runtime->regions.end(), address,
        [](uint32_t value, const MemoryRegion& region) {
            return value < region.start;
        });
    if (it != runtime->regions.begin())
    {
        --it;
        if (addressInRange(address, *it, size))
        {
            runtime->cachedRegion = *it;
            runtime->cachedRegionValid = true;
            return &runtime->cachedRegion;
        }
    }
    return NULL;
}

static MemoryRegion* findFetchRegion(NativeRuntime* runtime, uint32_t address, size_t size)
{
    if (runtime->cachedFetchRegionValid && addressInRange(address, runtime->cachedFetchRegion, size))
    {
        return &runtime->cachedFetchRegion;
    }

    std::vector<MemoryRegion>::iterator it = std::upper_bound(
        runtime->regions.begin(), runtime->regions.end(), address,
        [](uint32_t value, const MemoryRegion& region) {
            return value < region.start;
        });
    if (it != runtime->regions.begin())
    {
        --it;
        if (addressInRange(address, *it, size))
        {
            runtime->cachedFetchRegion = *it;
            runtime->cachedFetchRegionValid = true;
            return &runtime->cachedFetchRegion;
        }
    }
    return NULL;
}

static uint8_t* hostPtrFor(NativeRuntime* runtime, uint32_t address, size_t size)
{
    MemoryRegion* region = findRegion(runtime, address, size);
    if (!region)
    {
        return NULL;
    }
    return region->data + (address - region->start);
}

static bool fetchU32(NativeRuntime* runtime, uint32_t address, uint32_t* out)
{
    MemoryRegion* region = findFetchRegion(runtime, address, sizeof(uint32_t));
    if (!region)
    {
        return false;
    }
    *out = loadLe32(region->data + (address - region->start));
    return true;
}

static uint32_t parseHexEnv(const char* name)
{
    const char* value = getenv(name);
    if (!value || !value[0])
    {
        return 0;
    }

    uint32_t result = 0;
    if (value[0] == '0' && (value[1] == 'x' || value[1] == 'X'))
    {
        value += 2;
    }

    while (*value)
    {
        char c = *value++;
        uint32_t digit = 0;
        if (c >= '0' && c <= '9')
        {
            digit = (uint32_t)(c - '0');
        }
        else if (c >= 'a' && c <= 'f')
        {
            digit = (uint32_t)(c - 'a' + 10);
        }
        else if (c >= 'A' && c <= 'F')
        {
            digit = (uint32_t)(c - 'A' + 10);
        }
        else
        {
            break;
        }
        result = (result << 4) | digit;
    }
    return result;
}

static bool shouldTraceWrite(uint32_t address, size_t size)
{
    static bool initialized = false;
    static uint32_t traceStart = 0;
    static uint32_t traceEnd = 0;
    if (!initialized)
    {
        traceStart = parseHexEnv("DINGOO_PIE_TRACE_MEM_START");
        traceEnd = parseHexEnv("DINGOO_PIE_TRACE_MEM_END");
        initialized = true;
    }

    if (!traceStart || !traceEnd)
    {
        return false;
    }

    uint64_t begin = address;
    uint64_t end = begin + size;
    return begin < traceEnd && end > traceStart;
}

static bool shouldTracePc(uint32_t address)
{
    static bool initialized = false;
    static uint32_t traceStart = 0;
    static uint32_t traceEnd = 0;
    if (!initialized)
    {
        traceStart = parseHexEnv("DINGOO_PIE_TRACE_PC_START");
        traceEnd = parseHexEnv("DINGOO_PIE_TRACE_PC_END");
        initialized = true;
    }

    return traceStart && traceEnd && address >= traceStart && address < traceEnd;
}

static bool shouldSamplePc(void)
{
    static int enabled = -1;
    if (enabled < 0)
    {
        const char* value = getenv("DINGOO_PIE_TRACE_PC_SAMPLE");
        enabled = value && value[0] && strcmp(value, "0") != 0 ? 1 : 0;
    }
    return enabled != 0;
}

static bool shouldTraceInterpreterOps(void)
{
    static int enabled = -1;
    if (enabled < 0)
    {
        const char* value = getenv("DINGOO_PIE_TRACE_INTERPRETER_OPS");
        enabled = value && value[0] && strcmp(value, "0") != 0 ? 1 : 0;
    }
    return enabled != 0;
}

static bool runtimeEnvEnabled(const char* name)
{
    const char* value = getenv(name);
    return value && value[0] && strcmp(value, "0") != 0;
}

static uint64_t runtimeNowMicros(void)
{
    using namespace std::chrono;
    return (uint64_t)duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

static void traceInterpreterOp(const char* category, uint32_t pc, uint32_t insn)
{
    if (!shouldTraceInterpreterOps())
    {
        return;
    }

    struct Counter
    {
        const char* category;
        uint64_t count;
    };
    static Counter counters[8] = {};

    Counter* counter = NULL;
    for (size_t i = 0; i < sizeof(counters) / sizeof(counters[0]); ++i)
    {
        if (counters[i].category == category)
        {
            counter = &counters[i];
            break;
        }
        if (!counters[i].category)
        {
            counters[i].category = category;
            counter = &counters[i];
            break;
        }
    }

    if (!counter)
    {
        return;
    }

    counter->count++;
    if (counter->count <= 16 || (counter->count % 100000u) == 0)
    {
        printf("trace-interpreter-op: category=%s count=%llu pc=0x%08x insn=0x%08x\n",
            category, (unsigned long long)counter->count, pc, insn);
    }
}

static void tracePcSample(NativeRuntime* runtime, uint32_t pc)
{
    if (!shouldSamplePc())
    {
        return;
    }

    static uint32_t count = 0;
    static uint32_t lastPc = 0;
    if (pc == lastPc && (count % 20000u) != 0)
    {
        count++;
        return;
    }

    if ((count++ % 5000u) == 0 || pc != lastPc)
    {
        printf("trace-pc: pc=0x%08x ra=0x%08x t9=0x%08x v0=0x%08x a0=0x%08x\n",
            pc, runtime->gpr[31], runtime->gpr[25], runtime->gpr[2], runtime->gpr[4]);
        lastPc = pc;
    }
}

static bool codeHookPageMayContain(NativeRuntime* runtime, uint32_t address)
{
    uint32_t page = address >> kCodeHookPageShift;
    return runtime->codeHookPageMap.size() == kCodeHookPageCount &&
        runtime->codeHookPageMap[page] != 0;
}

static bool codeHookPageMayContainRange(NativeRuntime* runtime, uint32_t address)
{
    uint32_t page = address >> kCodeHookPageShift;
    return runtime->codeHookPageMap.size() == kCodeHookPageCount &&
        (runtime->codeHookPageMap[page] & kCodeHookPageRange) != 0;
}

static void ensureCodeHookPageMaps(NativeRuntime* runtime)
{
    if (runtime->codeHookPageMap.empty())
    {
        runtime->codeHookPageMap.resize(kCodeHookPageCount);
    }
    if (runtime->exactCodeHookPageIndex.empty())
    {
        runtime->exactCodeHookPageIndex.resize(kCodeHookPageCount, UINT32_MAX);
    }
}

static void markCodeHookPages(NativeRuntime* runtime, uint32_t begin, uint32_t end, uint8_t flag)
{
    ensureCodeHookPageMaps(runtime);

    uint32_t firstPage = begin >> kCodeHookPageShift;
    uint32_t lastPage = end >> kCodeHookPageShift;
    for (uint32_t page = firstPage; page <= lastPage; ++page)
    {
        runtime->codeHookPageMap[page] |= flag;
        if (page == UINT32_MAX)
        {
            break;
        }
    }
}

static void indexExactCodeHook(NativeRuntime* runtime, uint32_t address, size_t hookIndex)
{
    ensureCodeHookPageMaps(runtime);

    uint32_t page = address >> kCodeHookPageShift;
    uint32_t exactPageIndex = runtime->exactCodeHookPageIndex[page];
    if (exactPageIndex == UINT32_MAX)
    {
        CodeHookExactPage exactPage;
        for (uint32_t i = 0; i < kCodeHookExactSlotsPerPage; ++i)
        {
            exactPage.listIndexBySlot[i] = UINT32_MAX;
        }

        exactPageIndex = (uint32_t)runtime->exactCodeHookPages.size();
        runtime->exactCodeHookPages.push_back(exactPage);
        runtime->exactCodeHookPageIndex[page] = exactPageIndex;
    }

    uint32_t slot = (address & (kCodeHookPageSize - 1u)) >> 2;
    uint32_t listIndex = runtime->exactCodeHookPages[exactPageIndex].listIndexBySlot[slot];
    if (listIndex == UINT32_MAX)
    {
        listIndex = (uint32_t)runtime->exactCodeHookLists.size();
        runtime->exactCodeHookLists.push_back(std::vector<size_t>());
        runtime->exactCodeHookPages[exactPageIndex].listIndexBySlot[slot] = listIndex;
    }
    runtime->exactCodeHookLists[listIndex].push_back(hookIndex);
}

static bool findExactCodeHookListIndex(NativeRuntime* runtime, uint32_t address, uint32_t* outListIndex)
{
    uint32_t page = address >> kCodeHookPageShift;
    if (runtime->exactCodeHookPageIndex.size() != kCodeHookPageCount)
    {
        return false;
    }

    uint32_t exactPageIndex = runtime->exactCodeHookPageIndex[page];
    if (exactPageIndex == UINT32_MAX || exactPageIndex >= runtime->exactCodeHookPages.size())
    {
        return false;
    }

    uint32_t slot = (address & (kCodeHookPageSize - 1u)) >> 2;
    uint32_t listIndex = runtime->exactCodeHookPages[exactPageIndex].listIndexBySlot[slot];
    if (listIndex == UINT32_MAX || listIndex >= runtime->exactCodeHookLists.size())
    {
        return false;
    }

    *outListIndex = listIndex;
    return true;
}

static void profileInterpreterHook(NativeRuntime* runtime)
{
    if (runtime->interpreterProfileEnabled)
    {
        runtime->interpreterProfileHooks++;
    }
}

static void profileInterpreterInstruction(NativeRuntime* runtime)
{
    if (!runtime->interpreterProfileEnabled)
    {
        return;
    }

    runtime->interpreterProfileInstructions++;
    if (--runtime->interpreterProfileCheckCountdown > 0)
    {
        return;
    }
    runtime->interpreterProfileCheckCountdown = 65536;

    uint64_t nowMicros = runtimeNowMicros();
    if (!runtime->interpreterProfileLastMicros)
    {
        runtime->interpreterProfileLastMicros = nowMicros;
        return;
    }

    uint64_t elapsedMicros = nowMicros - runtime->interpreterProfileLastMicros;
    if (elapsedMicros >= 1000000)
    {
        uint64_t submittedFrames = consumeFramebufferSubmittedCount();
        uint64_t framebufferCopyMicros = consumeFramebufferCopyMicros();
        uint64_t totalFrameIntervalMicros = 0;
        uint64_t maxFrameIntervalMicros = 0;
        uint64_t over25msCount = 0;
        uint64_t over33msCount = 0;
        consumeFramebufferTimingStats(&totalFrameIntervalMicros, &maxFrameIntervalMicros,
            &over25msCount, &over33msCount);
        uint64_t avgFrameIntervalMicros = submittedFrames ? totalFrameIntervalMicros / submittedFrames : 0;
        uint64_t ips = (runtime->interpreterProfileInstructions * 1000000ull) / elapsedMicros;

        printf("profile interpreter: ips=%llu hooks=%llu/s fb_submit=%llu fb_copy_us=%llu fb_interval_us=%llu/%llu over25=%llu over33=%llu pc=0x%08x ra=0x%08x\n",
            (unsigned long long)ips,
            (unsigned long long)runtime->interpreterProfileHooks,
            (unsigned long long)submittedFrames,
            (unsigned long long)framebufferCopyMicros,
            (unsigned long long)avgFrameIntervalMicros,
            (unsigned long long)maxFrameIntervalMicros,
            (unsigned long long)over25msCount,
            (unsigned long long)over33msCount,
            runtime->pc,
            runtime->gpr[31]);

        runtime->interpreterProfileInstructions = 0;
        runtime->interpreterProfileHooks = 0;
        runtime->interpreterProfileLastMicros = nowMicros;
    }
}

static void profileInterpreterPcSample(NativeRuntime* runtime, uint32_t pc)
{
    if (!runtime->interpreterPcProfileEnabled)
    {
        return;
    }
    if (--runtime->interpreterPcSampleCountdown > 0)
    {
        return;
    }
    runtime->interpreterPcSampleCountdown = 4096;

    struct PcSample
    {
        uint32_t pc;
        uint32_t count;
    };
    static PcSample samples[1024] = {};
    static uint32_t sampleCount = 0;
    static uint64_t lastMicros = 0;

    for (uint32_t i = 0; i < sampleCount; ++i)
    {
        if (samples[i].pc == pc)
        {
            samples[i].count++;
            goto maybe_print;
        }
    }
    if (sampleCount < sizeof(samples) / sizeof(samples[0]))
    {
        samples[sampleCount].pc = pc;
        samples[sampleCount].count = 1;
        sampleCount++;
    }

maybe_print:
    uint64_t nowMicros = runtimeNowMicros();
    if (!lastMicros)
    {
        lastMicros = nowMicros;
        return;
    }
    if (nowMicros - lastMicros < 1000000)
    {
        return;
    }

    PcSample top[8] = {};
    for (uint32_t i = 0; i < sampleCount; ++i)
    {
        for (uint32_t slot = 0; slot < sizeof(top) / sizeof(top[0]); ++slot)
        {
            if (samples[i].count > top[slot].count)
            {
                for (uint32_t move = (uint32_t)(sizeof(top) / sizeof(top[0])) - 1; move > slot; --move)
                {
                    top[move] = top[move - 1];
                }
                top[slot] = samples[i];
                break;
            }
        }
    }

    printf("profile interpreter pc:");
    for (uint32_t i = 0; i < sizeof(top) / sizeof(top[0]) && top[i].count; ++i)
    {
        printf(" 0x%08x=%u", top[i].pc, top[i].count);
    }
    printf("\n");

    memset(samples, 0, sizeof(samples));
    sampleCount = 0;
    lastMicros = nowMicros;
}

static bool waitForInterpreterResume(NativeRuntime* runtime)
{
    uint32_t restoreGeneration = pauseGateRestoreGeneration();
    if (pauseGateWaitForResume() && restoreGeneration != pauseGateRestoreGeneration())
    {
        runtime->cachedFetchRegionValid = false;
        runtime->cachedRegionValid = false;
    }
    return !runtime->stopRequested.load(std::memory_order_acquire);
}

static void setReg(NativeRuntime* runtime, uint32_t index, uint32_t value)
{
    if (index != 0 && index < 32)
    {
        runtime->gpr[index] = value;
    }
}

static uint32_t loadLe32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t loadLe16(const uint8_t* p)
{
    return (uint16_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8));
}

static void storeLe32(uint8_t* p, uint32_t value)
{
    p[0] = (uint8_t)(value & 0xff);
    p[1] = (uint8_t)((value >> 8) & 0xff);
    p[2] = (uint8_t)((value >> 16) & 0xff);
    p[3] = (uint8_t)((value >> 24) & 0xff);
}

static void storeLe16(uint8_t* p, uint16_t value)
{
    p[0] = (uint8_t)(value & 0xff);
    p[1] = (uint8_t)((value >> 8) & 0xff);
}

static void callCodeHooks(NativeRuntime* runtime, uint32_t address)
{
    std::vector<HookEntry> hooksToRun;
    {
        std::lock_guard<std::mutex> lock(runtime->hookMutex);
        if (!codeHookPageMayContain(runtime, address))
        {
            return;
        }

        uint32_t indexedHookList = UINT32_MAX;
        bool hasIndexedHooks = findExactCodeHookListIndex(runtime, address, &indexedHookList);
        bool checkRangedHooks = runtime->hasRangedCodeHooks && codeHookPageMayContainRange(runtime, address);
        if (!hasIndexedHooks && !checkRangedHooks)
        {
            return;
        }

        auto collectHook = [&](size_t index) {
            if (index >= runtime->hooks.size())
            {
                return;
            }
            HookEntry& hook = runtime->hooks[index];
            if (!hook.active || !(hook.type & RUNTIME_HOOK_CODE))
            {
                return;
            }
            if (address < hook.begin || address > hook.end)
            {
                return;
            }
            hooksToRun.push_back(hook);
        };

        if (hasIndexedHooks)
        {
            size_t hookCount = runtime->exactCodeHookLists[indexedHookList].size();
            for (size_t i = 0; i < hookCount; ++i)
            {
                collectHook(runtime->exactCodeHookLists[indexedHookList][i]);
            }
        }

        if (checkRangedHooks)
        {
            for (size_t i = 0; i < runtime->rangedCodeHooks.size(); ++i)
            {
                collectHook(runtime->rangedCodeHooks[i]);
            }
        }
    }

    // Collect callbacks first so hook changes during dispatch are safe.
    for (size_t i = 0; i < hooksToRun.size(); ++i)
    {
        const HookEntry& hook = hooksToRun[i];
        uint32_t beforePc = runtime->pc;
        RuntimeCodeHookCallback cb = (RuntimeCodeHookCallback)hook.callback;
        cb(runtime, address, 4, hook.userData);
        profileInterpreterHook(runtime);
        if (runtime->pc != beforePc)
        {
            static int traceHookPcChange = -1;
            if (traceHookPcChange < 0)
            {
                const char* value = getenv("DINGOO_PIE_TRACE_HOOK_PC");
                traceHookPcChange = value && value[0] && strcmp(value, "0") != 0 ? 1 : 0;
            }
            if (traceHookPcChange)
            {
                static uint32_t pcChangeTraceCount = 0;
                printf("runtime-hook: pc changed hook=0x%08x address=0x%08x before=0x%08x after=0x%08x ra=0x%08x t9=0x%08x v0=0x%08x\n",
                    (uint32_t)hook.handle, address, beforePc, runtime->pc, runtime->gpr[31], runtime->gpr[25], runtime->gpr[2]);
                if (++pcChangeTraceCount >= 256)
                {
                    traceHookPcChange = 0;
                }
            }
        }
    }
}

static bool hasCodeHook(NativeRuntime* runtime, uint32_t address)
{
    std::lock_guard<std::mutex> lock(runtime->hookMutex);
    if (!codeHookPageMayContain(runtime, address))
    {
        return false;
    }

    uint32_t indexedHookList = UINT32_MAX;
    if (findExactCodeHookListIndex(runtime, address, &indexedHookList))
    {
        const std::vector<size_t>& indexedHooks = runtime->exactCodeHookLists[indexedHookList];
        for (size_t i = 0; i < indexedHooks.size(); ++i)
        {
            size_t index = indexedHooks[i];
            if (index < runtime->hooks.size() &&
                runtime->hooks[index].active &&
                (runtime->hooks[index].type & RUNTIME_HOOK_CODE))
            {
                return true;
            }
        }
    }

    if (!runtime->hasRangedCodeHooks || !codeHookPageMayContainRange(runtime, address))
    {
        return false;
    }

    for (size_t i = 0; i < runtime->rangedCodeHooks.size(); ++i)
    {
        size_t index = runtime->rangedCodeHooks[i];
        if (index >= runtime->hooks.size())
        {
            continue;
        }
        HookEntry& hook = runtime->hooks[index];
        if (hook.active && (hook.type & RUNTIME_HOOK_CODE) && address >= hook.begin && address <= hook.end)
        {
            return true;
        }
    }

    return false;
}

static bool callInvalidMemoryHooks(NativeRuntime* runtime, RuntimeMemoryAccess type, uint32_t address, int size, int64_t value)
{
    bool handled = false;
    std::vector<HookEntry> hooksToRun;
    {
        std::lock_guard<std::mutex> lock(runtime->hookMutex);
        for (size_t i = 0; i < runtime->hooks.size(); ++i)
        {
            HookEntry& hook = runtime->hooks[i];
            if (!hook.active || !(hook.type & RUNTIME_HOOK_MEM_INVALID))
            {
                continue;
            }
            hooksToRun.push_back(hook);
        }
    }
    for (size_t i = 0; i < hooksToRun.size(); ++i)
    {
        const HookEntry& hook = hooksToRun[i];
        RuntimeMemoryHookCallback cb = (RuntimeMemoryHookCallback)hook.callback;
        if (cb(runtime, type, address, size, value, hook.userData))
        {
            handled = true;
        }
    }
    return handled;
}

static bool callValidMemoryHooks(NativeRuntime* runtime, RuntimeMemoryAccess type, uint32_t address, int size, int64_t value)
{
    bool handled = false;
    std::vector<HookEntry> hooksToRun;
    {
        std::lock_guard<std::mutex> lock(runtime->hookMutex);
        for (size_t i = 0; i < runtime->hooks.size(); ++i)
        {
            HookEntry& hook = runtime->hooks[i];
            if (!hook.active || !(hook.type & RUNTIME_HOOK_MEM_VALID))
            {
                continue;
            }
            uint64_t accessBegin = address;
            uint64_t accessEnd = accessBegin + (uint32_t)size;
            uint64_t hookBegin = hook.begin;
            uint64_t hookEnd = (uint64_t)hook.end + 1u;
            if (accessBegin >= hookEnd || hookBegin >= accessEnd)
            {
                continue;
            }
            hooksToRun.push_back(hook);
        }
    }
    for (size_t i = 0; i < hooksToRun.size(); ++i)
    {
        const HookEntry& hook = hooksToRun[i];
        RuntimeMemoryHookCallback cb = (RuntimeMemoryHookCallback)hook.callback;
        if (cb(runtime, type, address, size, value, hook.userData))
        {
            handled = true;
        }
    }
    return handled;
}

static bool readMemRaw(NativeRuntime* runtime, uint32_t address, void* out, size_t size)
{
    uint8_t* p = hostPtrFor(runtime, address, size);
    if (!p)
    {
        return false;
    }
    memcpy(out, p, size);
    return true;
}

static bool writeMemRaw(NativeRuntime* runtime, uint32_t address, const void* in, size_t size)
{
    uint8_t* p = hostPtrFor(runtime, address, size);
    if (!p)
    {
        return false;
    }
    if (shouldTraceWrite(address, size))
    {
        printf("native trace write: pc=0x%08x a0=0x%08x a1=0x%08x a2=0x%08x addr=0x%08x size=%u data=",
            runtime->pc, runtime->gpr[4], runtime->gpr[5], runtime->gpr[6], address, (unsigned)size);
        const uint8_t* bytes = (const uint8_t*)in;
        for (size_t i = 0; i < size; ++i)
        {
            printf("%02x", bytes[i]);
        }
        printf("\n");
    }
    memcpy(p, in, size);
    trackFramebufferWrite(address, (uint32_t)size);
    return true;
}

static bool readU8(NativeRuntime* runtime, uint32_t address, uint32_t* out)
{
    uint8_t* p = hostPtrFor(runtime, address, sizeof(uint8_t));
    if (!p)
    {
        callInvalidMemoryHooks(runtime, RUNTIME_MEM_READ_UNMAPPED, address, 1, 0);
        runtime->lastError = RUNTIME_ERROR_READ_UNMAPPED;
        return false;
    }
    *out = *p;
    return true;
}

static bool readU16(NativeRuntime* runtime, uint32_t address, uint32_t* out)
{
    uint8_t* p = hostPtrFor(runtime, address, sizeof(uint16_t));
    if (!p)
    {
        callInvalidMemoryHooks(runtime, RUNTIME_MEM_READ_UNMAPPED, address, 2, 0);
        runtime->lastError = RUNTIME_ERROR_READ_UNMAPPED;
        return false;
    }
    *out = loadLe16(p);
    return true;
}

static bool readU32(NativeRuntime* runtime, uint32_t address, uint32_t* out)
{
    uint8_t* p = hostPtrFor(runtime, address, sizeof(uint32_t));
    if (!p)
    {
        callInvalidMemoryHooks(runtime, RUNTIME_MEM_READ_UNMAPPED, address, 4, 0);
        runtime->lastError = RUNTIME_ERROR_READ_UNMAPPED;
        return false;
    }
    *out = loadLe32(p);
    return true;
}

static bool writeU8(NativeRuntime* runtime, uint32_t address, uint32_t value)
{
    uint8_t* p = hostPtrFor(runtime, address, sizeof(uint8_t));
    if (!p)
    {
        callInvalidMemoryHooks(runtime, RUNTIME_MEM_WRITE_UNMAPPED, address, 1, value);
        runtime->lastError = RUNTIME_ERROR_WRITE_UNMAPPED;
        return false;
    }
    if (shouldTraceWrite(address, sizeof(uint8_t)))
    {
        uint8_t v = (uint8_t)value;
        printf("native trace write: pc=0x%08x a0=0x%08x a1=0x%08x a2=0x%08x addr=0x%08x size=%u data=%02x\n",
            runtime->pc, runtime->gpr[4], runtime->gpr[5], runtime->gpr[6], address, 1u, v);
    }
    *p = (uint8_t)value;
    callValidMemoryHooks(runtime, RUNTIME_MEM_WRITE, address, 1, value & 0xffu);
    trackFramebufferWrite(address, sizeof(uint8_t));
    return true;
}

static bool writeU16(NativeRuntime* runtime, uint32_t address, uint32_t value)
{
    uint8_t* p = hostPtrFor(runtime, address, sizeof(uint16_t));
    if (!p)
    {
        callInvalidMemoryHooks(runtime, RUNTIME_MEM_WRITE_UNMAPPED, address, 2, value);
        runtime->lastError = RUNTIME_ERROR_WRITE_UNMAPPED;
        return false;
    }
    if (shouldTraceWrite(address, sizeof(uint16_t)))
    {
        printf("native trace write: pc=0x%08x a0=0x%08x a1=0x%08x a2=0x%08x addr=0x%08x size=%u data=%02x%02x\n",
            runtime->pc, runtime->gpr[4], runtime->gpr[5], runtime->gpr[6], address, 2u,
            (unsigned)(value & 0xffu), (unsigned)((value >> 8) & 0xffu));
    }
    storeLe16(p, (uint16_t)value);
    callValidMemoryHooks(runtime, RUNTIME_MEM_WRITE, address, 2, value & 0xffffu);
    trackFramebufferWrite(address, sizeof(uint16_t));
    return true;
}

static bool writeU32(NativeRuntime* runtime, uint32_t address, uint32_t value)
{
    uint8_t* p = hostPtrFor(runtime, address, sizeof(uint32_t));
    if (!p)
    {
        callInvalidMemoryHooks(runtime, RUNTIME_MEM_WRITE_UNMAPPED, address, 4, value);
        runtime->lastError = RUNTIME_ERROR_WRITE_UNMAPPED;
        return false;
    }
    if (shouldTraceWrite(address, sizeof(uint32_t)))
    {
        printf("native trace write: pc=0x%08x a0=0x%08x a1=0x%08x a2=0x%08x addr=0x%08x size=%u data=%02x%02x%02x%02x\n",
            runtime->pc, runtime->gpr[4], runtime->gpr[5], runtime->gpr[6], address, 4u,
            (unsigned)(value & 0xffu), (unsigned)((value >> 8) & 0xffu),
            (unsigned)((value >> 16) & 0xffu), (unsigned)((value >> 24) & 0xffu));
    }
    storeLe32(p, value);
    callValidMemoryHooks(runtime, RUNTIME_MEM_WRITE, address, 4, value);
    trackFramebufferWrite(address, sizeof(uint32_t));
    return true;
}

static bool readPartialLwl(NativeRuntime* runtime, uint32_t address, uint32_t oldValue, uint32_t* out)
{
    uint32_t aligned = address & ~3u;
    uint32_t byte = address & 3u;
    uint32_t value = oldValue;
    for (uint32_t i = 0; i <= byte; ++i)
    {
        uint32_t memByte = 0;
        if (!readU8(runtime, aligned + i, &memByte))
        {
            return false;
        }
        uint32_t regByte = 3 - byte + i;
        value = (value & ~(0xffu << (regByte * 8))) | ((memByte & 0xffu) << (regByte * 8));
    }
    *out = value;
    return true;
}

static bool readPartialLwr(NativeRuntime* runtime, uint32_t address, uint32_t oldValue, uint32_t* out)
{
    uint32_t aligned = address & ~3u;
    uint32_t byte = address & 3u;
    uint32_t value = oldValue;
    for (uint32_t i = byte; i < 4; ++i)
    {
        uint32_t memByte = 0;
        if (!readU8(runtime, aligned + i, &memByte))
        {
            return false;
        }
        uint32_t regByte = i - byte;
        value = (value & ~(0xffu << (regByte * 8))) | ((memByte & 0xffu) << (regByte * 8));
    }
    *out = value;
    return true;
}

static bool writePartialSwl(NativeRuntime* runtime, uint32_t address, uint32_t value)
{
    uint32_t aligned = address & ~3u;
    uint32_t byte = address & 3u;
    for (uint32_t i = 0; i <= byte; ++i)
    {
        uint32_t regByte = 3 - byte + i;
        if (!writeU8(runtime, aligned + i, (value >> (regByte * 8)) & 0xffu))
        {
            return false;
        }
    }
    return true;
}

static bool writePartialSwr(NativeRuntime* runtime, uint32_t address, uint32_t value)
{
    uint32_t aligned = address & ~3u;
    uint32_t byte = address & 3u;
    for (uint32_t i = byte; i < 4; ++i)
    {
        uint32_t regByte = i - byte;
        if (!writeU8(runtime, aligned + i, (value >> (regByte * 8)) & 0xffu))
        {
            return false;
        }
    }
    return true;
}

static uint32_t branchTarget(uint32_t pc, uint32_t imm)
{
    return pc + 4 + (signExtend16(imm) << 2);
}

static bool executeOne(NativeRuntime* runtime, bool allowBranch, bool* branched);

static bool executeDelaySlot(NativeRuntime* runtime, uint32_t slotPc)
{
    uint32_t savedPc = runtime->pc;
    runtime->pc = slotPc;
    bool nestedBranch = false;
    bool ok = executeOne(runtime, false, &nestedBranch);
    runtime->pc = savedPc;
    return ok;
}

static bool handleBranch(NativeRuntime* runtime, uint32_t target, bool taken, bool link, uint32_t linkValue, bool allowBranch, bool* branched)
{
    uint32_t pc = runtime->pc;
    if (!allowBranch)
    {
        printf("native runtime: branch in delay slot at 0x%08x\n", pc);
        runtime->lastError = RUNTIME_ERROR_INSN_INVALID;
        return false;
    }

    if (link)
    {
        setReg(runtime, 31, linkValue);
    }

    if (!executeDelaySlot(runtime, pc + 4))
    {
        return false;
    }

    runtime->pc = taken ? target : (pc + 8);
    *branched = true;
    return true;
}

static bool executeSpecial(NativeRuntime* runtime, uint32_t insn, bool allowBranch, bool* branched)
{
    uint32_t rs = (insn >> 21) & 0x1f;
    uint32_t rt = (insn >> 16) & 0x1f;
    uint32_t rd = (insn >> 11) & 0x1f;
    uint32_t sh = (insn >> 6) & 0x1f;
    uint32_t fn = insn & 0x3f;

    switch (fn)
    {
    case 0x00:
        setReg(runtime, rd, runtime->gpr[rt] << sh);
        return true;
    case 0x02:
        setReg(runtime, rd, runtime->gpr[rt] >> sh);
        return true;
    case 0x03:
        setReg(runtime, rd, (uint32_t)((int32_t)runtime->gpr[rt] >> sh));
        return true;
    case 0x04:
        setReg(runtime, rd, runtime->gpr[rt] << (runtime->gpr[rs] & 0x1f));
        return true;
    case 0x06:
        setReg(runtime, rd, runtime->gpr[rt] >> (runtime->gpr[rs] & 0x1f));
        return true;
    case 0x07:
        setReg(runtime, rd, (uint32_t)((int32_t)runtime->gpr[rt] >> (runtime->gpr[rs] & 0x1f)));
        return true;
    case 0x08:
        return handleBranch(runtime, runtime->gpr[rs], true, false, 0, allowBranch, branched);
    case 0x09:
        return handleBranch(runtime, runtime->gpr[rs], true, true, runtime->pc + 8, allowBranch, branched);
    case 0x0a:
        if (runtime->gpr[rt] == 0)
        {
            setReg(runtime, rd, runtime->gpr[rs]);
        }
        return true;
    case 0x0b:
        if (runtime->gpr[rt] != 0)
        {
            setReg(runtime, rd, runtime->gpr[rs]);
        }
        return true;
    case 0x0c:
        printf("native runtime: syscall at 0x%08x\n", runtime->pc);
        runtime->lastError = RUNTIME_ERROR_EXCEPTION;
        return false;
    case 0x0d:
        runtime->pc += 4;
        *branched = true;
        return true;
    case 0x0f:
        return true;
    case 0x10:
        setReg(runtime, rd, runtime->hi);
        return true;
    case 0x11:
        runtime->hi = runtime->gpr[rs];
        return true;
    case 0x12:
        setReg(runtime, rd, runtime->lo);
        return true;
    case 0x13:
        runtime->lo = runtime->gpr[rs];
        return true;
    case 0x18:
    {
        int64_t value = (int64_t)(int32_t)runtime->gpr[rs] * (int64_t)(int32_t)runtime->gpr[rt];
        runtime->lo = (uint32_t)value;
        runtime->hi = (uint32_t)((uint64_t)value >> 32);
        return true;
    }
    case 0x19:
    {
        uint64_t value = (uint64_t)runtime->gpr[rs] * (uint64_t)runtime->gpr[rt];
        runtime->lo = (uint32_t)value;
        runtime->hi = (uint32_t)(value >> 32);
        return true;
    }
    case 0x1a:
        if (runtime->gpr[rt] != 0)
        {
            runtime->lo = (uint32_t)((int32_t)runtime->gpr[rs] / (int32_t)runtime->gpr[rt]);
            runtime->hi = (uint32_t)((int32_t)runtime->gpr[rs] % (int32_t)runtime->gpr[rt]);
        }
        return true;
    case 0x1b:
        if (runtime->gpr[rt] != 0)
        {
            runtime->lo = runtime->gpr[rs] / runtime->gpr[rt];
            runtime->hi = runtime->gpr[rs] % runtime->gpr[rt];
        }
        return true;
    case 0x20:
    case 0x21:
        setReg(runtime, rd, runtime->gpr[rs] + runtime->gpr[rt]);
        return true;
    case 0x22:
    case 0x23:
    case 0x2c:
    case 0x2d:
        setReg(runtime, rd, runtime->gpr[rs] - runtime->gpr[rt]);
        return true;
    case 0x24:
        setReg(runtime, rd, runtime->gpr[rs] & runtime->gpr[rt]);
        return true;
    case 0x25:
        setReg(runtime, rd, runtime->gpr[rs] | runtime->gpr[rt]);
        return true;
    case 0x26:
        setReg(runtime, rd, runtime->gpr[rs] ^ runtime->gpr[rt]);
        return true;
    case 0x27:
        setReg(runtime, rd, ~(runtime->gpr[rs] | runtime->gpr[rt]));
        return true;
    case 0x2a:
        setReg(runtime, rd, (int32_t)runtime->gpr[rs] < (int32_t)runtime->gpr[rt]);
        return true;
    case 0x2b:
        setReg(runtime, rd, runtime->gpr[rs] < runtime->gpr[rt]);
        return true;
    default:
        printf("native runtime: unsupported SPECIAL function 0x%02x at 0x%08x insn=0x%08x\n", fn, runtime->pc, insn);
        runtime->lastError = RUNTIME_ERROR_INSN_INVALID;
        return false;
    }
}

static bool executeRegImm(NativeRuntime* runtime, uint32_t insn, bool allowBranch, bool* branched)
{
    uint32_t rs = (insn >> 21) & 0x1f;
    uint32_t rt = (insn >> 16) & 0x1f;
    uint32_t imm = insn & 0xffff;
    uint32_t pc = runtime->pc;
    bool taken = false;
    bool link = false;

    switch (rt)
    {
    case 0x00:
        taken = (int32_t)runtime->gpr[rs] < 0;
        break;
    case 0x01:
        taken = (int32_t)runtime->gpr[rs] >= 0;
        break;
    case 0x10:
        taken = (int32_t)runtime->gpr[rs] < 0;
        link = true;
        break;
    case 0x11:
        taken = (int32_t)runtime->gpr[rs] >= 0;
        link = true;
        break;
    default:
        printf("native runtime: unsupported REGIMM rt=0x%02x at 0x%08x insn=0x%08x\n", rt, pc, insn);
        runtime->lastError = RUNTIME_ERROR_INSN_INVALID;
        return false;
    }

    return handleBranch(runtime, branchTarget(pc, imm), taken, link, pc + 8, allowBranch, branched);
}

static uint64_t readHiLo64(NativeRuntime* runtime)
{
    return (uint64_t)runtime->lo | ((uint64_t)runtime->hi << 32);
}

static void writeHiLo64(NativeRuntime* runtime, uint64_t value)
{
    runtime->lo = (uint32_t)value;
    runtime->hi = (uint32_t)(value >> 32);
}

static bool executeSpecial2(NativeRuntime* runtime, uint32_t insn, uint32_t pc)
{
    uint32_t rs = (insn >> 21) & 0x1f;
    uint32_t rt = (insn >> 16) & 0x1f;
    uint32_t rd = (insn >> 11) & 0x1f;
    uint32_t fn = insn & 0x3f;

    switch (fn)
    {
    case 0x00:
    {
        int64_t acc = (int64_t)readHiLo64(runtime);
        int64_t product = (int64_t)(int32_t)runtime->gpr[rs] * (int64_t)(int32_t)runtime->gpr[rt];
        writeHiLo64(runtime, (uint64_t)(acc + product));
        return true;
    }
    case 0x01:
    {
        uint64_t acc = readHiLo64(runtime);
        uint64_t product = (uint64_t)runtime->gpr[rs] * (uint64_t)runtime->gpr[rt];
        writeHiLo64(runtime, acc + product);
        return true;
    }
    case 0x02:
        setReg(runtime, rd, runtime->gpr[rs] * runtime->gpr[rt]);
        return true;
    case 0x04:
    {
        int64_t acc = (int64_t)readHiLo64(runtime);
        int64_t product = (int64_t)(int32_t)runtime->gpr[rs] * (int64_t)(int32_t)runtime->gpr[rt];
        writeHiLo64(runtime, (uint64_t)(acc - product));
        return true;
    }
    case 0x05:
    {
        uint64_t acc = readHiLo64(runtime);
        uint64_t product = (uint64_t)runtime->gpr[rs] * (uint64_t)runtime->gpr[rt];
        writeHiLo64(runtime, acc - product);
        return true;
    }
    case 0x20:
        setReg(runtime, rd, countLeadingZeros32(runtime->gpr[rs]));
        return true;
    case 0x21:
        setReg(runtime, rd, countLeadingZeros32(~runtime->gpr[rs]));
        return true;
    default:
        printf("native runtime: unsupported SPECIAL2 at 0x%08x insn=0x%08x fn=0x%02x\n", pc, insn, fn);
        runtime->lastError = RUNTIME_ERROR_INSN_INVALID;
        return false;
    }
}

static bool executeOne(NativeRuntime* runtime, bool allowBranch, bool* branched)
{
    uint32_t pc = runtime->pc;
    uint32_t insn = 0;
    *branched = false;
    profileInterpreterPcSample(runtime, pc);

    callCodeHooks(runtime, pc);
    if (runtime->pc != pc)
    {
        *branched = true;
        return true;
    }

    if (!fetchU32(runtime, pc, &insn))
    {
        runtime->lastError = RUNTIME_ERROR_FETCH_UNMAPPED;
        callInvalidMemoryHooks(runtime, RUNTIME_MEM_FETCH_UNMAPPED, pc, 4, 0);
        return false;
    }
    ppssppShimResolveEmuHack(pc, insn, &insn);

    if (shouldTracePc(pc))
    {
        printf("native trace pc: pc=0x%08x insn=0x%08x v0=0x%08x v1=0x%08x a0=0x%08x a1=0x%08x a2=0x%08x a3=0x%08x t2=0x%08x t3=0x%08x sp=0x%08x ra=0x%08x\n",
            pc, insn, runtime->gpr[2], runtime->gpr[3], runtime->gpr[4], runtime->gpr[5], runtime->gpr[6],
            runtime->gpr[7], runtime->gpr[10], runtime->gpr[11], runtime->gpr[29], runtime->gpr[31]);
    }

    uint32_t op = insn >> 26;
    uint32_t rs = (insn >> 21) & 0x1f;
    uint32_t rt = (insn >> 16) & 0x1f;
    uint32_t imm = insn & 0xffff;
    uint32_t target = insn & 0x03ffffffu;
    uint32_t addr = 0;
    uint32_t value = 0;

    switch (op)
    {
    case 0x00:
        if (!executeSpecial(runtime, insn, allowBranch, branched))
        {
            return false;
        }
        break;
    case 0x01:
        if (!executeRegImm(runtime, insn, allowBranch, branched))
        {
            return false;
        }
        break;
    case 0x02:
        if (!handleBranch(runtime, ((pc + 4) & 0xf0000000u) | (target << 2), true, false, 0, allowBranch, branched))
        {
            return false;
        }
        break;
    case 0x03:
        if (!handleBranch(runtime, ((pc + 4) & 0xf0000000u) | (target << 2), true, true, pc + 8, allowBranch, branched))
        {
            return false;
        }
        break;
    case 0x04:
        if (!handleBranch(runtime, branchTarget(pc, imm), runtime->gpr[rs] == runtime->gpr[rt], false, 0, allowBranch, branched))
        {
            return false;
        }
        break;
    case 0x05:
        if (!handleBranch(runtime, branchTarget(pc, imm), runtime->gpr[rs] != runtime->gpr[rt], false, 0, allowBranch, branched))
        {
            return false;
        }
        break;
    case 0x06:
        if (!handleBranch(runtime, branchTarget(pc, imm), (int32_t)runtime->gpr[rs] <= 0, false, 0, allowBranch, branched))
        {
            return false;
        }
        break;
    case 0x07:
        if (!handleBranch(runtime, branchTarget(pc, imm), (int32_t)runtime->gpr[rs] > 0, false, 0, allowBranch, branched))
        {
            return false;
        }
        break;
    case 0x08:
    case 0x09:
        setReg(runtime, rt, runtime->gpr[rs] + signExtend16(imm));
        break;
    case 0x0a:
        setReg(runtime, rt, (int32_t)runtime->gpr[rs] < (int32_t)signExtend16(imm));
        break;
    case 0x0b:
        setReg(runtime, rt, runtime->gpr[rs] < signExtend16(imm));
        break;
    case 0x0c:
        setReg(runtime, rt, runtime->gpr[rs] & imm);
        break;
    case 0x0d:
        setReg(runtime, rt, runtime->gpr[rs] | imm);
        break;
    case 0x0e:
        setReg(runtime, rt, runtime->gpr[rs] ^ imm);
        break;
    case 0x0f:
        setReg(runtime, rt, imm << 16);
        break;
    case 0x10:
        traceInterpreterOp("cop1", pc, insn);
        break;
    case 0x1c:
        if (!executeSpecial2(runtime, insn, pc))
        {
            return false;
        }
        break;
    case 0x20:
        addr = runtime->gpr[rs] + signExtend16(imm);
        if (!readU8(runtime, addr, &value))
        {
            return false;
        }
        setReg(runtime, rt, signExtend8(value));
        break;
    case 0x21:
        addr = runtime->gpr[rs] + signExtend16(imm);
        if (!readU16(runtime, addr, &value))
        {
            return false;
        }
        setReg(runtime, rt, (value & 0x8000) ? (value | 0xffff0000u) : value);
        break;
    case 0x22:
        addr = runtime->gpr[rs] + signExtend16(imm);
        if (!readPartialLwl(runtime, addr, runtime->gpr[rt], &value))
        {
            return false;
        }
        setReg(runtime, rt, value);
        break;
    case 0x23:
        addr = runtime->gpr[rs] + signExtend16(imm);
        if (!readU32(runtime, addr, &value))
        {
            return false;
        }
        setReg(runtime, rt, value);
        break;
    case 0x24:
        addr = runtime->gpr[rs] + signExtend16(imm);
        if (!readU8(runtime, addr, &value))
        {
            return false;
        }
        setReg(runtime, rt, value);
        break;
    case 0x25:
        addr = runtime->gpr[rs] + signExtend16(imm);
        if (!readU16(runtime, addr, &value))
        {
            return false;
        }
        setReg(runtime, rt, value);
        break;
    case 0x26:
        addr = runtime->gpr[rs] + signExtend16(imm);
        if (!readPartialLwr(runtime, addr, runtime->gpr[rt], &value))
        {
            return false;
        }
        setReg(runtime, rt, value);
        break;
    case 0x28:
        addr = runtime->gpr[rs] + signExtend16(imm);
        if (!writeU8(runtime, addr, runtime->gpr[rt]))
        {
            return false;
        }
        break;
    case 0x29:
        addr = runtime->gpr[rs] + signExtend16(imm);
        if (!writeU16(runtime, addr, runtime->gpr[rt]))
        {
            return false;
        }
        break;
    case 0x2a:
        addr = runtime->gpr[rs] + signExtend16(imm);
        if (!writePartialSwl(runtime, addr, runtime->gpr[rt]))
        {
            return false;
        }
        break;
    case 0x2b:
        addr = runtime->gpr[rs] + signExtend16(imm);
        if (!writeU32(runtime, addr, runtime->gpr[rt]))
        {
            return false;
        }
        break;
    case 0x2e:
        addr = runtime->gpr[rs] + signExtend16(imm);
        if (!writePartialSwr(runtime, addr, runtime->gpr[rt]))
        {
            return false;
        }
        break;
    case 0x2f:
        break;
    case 0x33:
        traceInterpreterOp("lwc1", pc, insn);
        break;
    default:
        if ((insn & 0xfc000000u) == 0xbc000000u)
        {
            traceInterpreterOp("vfpu", pc, insn);
            break;
        }
        printf("native runtime: unsupported opcode 0x%02x at 0x%08x insn=0x%08x\n", op, pc, insn);
        runtime->lastError = RUNTIME_ERROR_INSN_INVALID;
        return false;
    }

    if (!*branched)
    {
        runtime->pc = pc + 4;
    }
    return true;
}

RuntimeError nativeRuntimeCreate(NativeRuntime** out)
{
    if (!out)
    {
        return RUNTIME_ERROR_ARG;
    }

    NativeRuntime* runtime = new NativeRuntime();
    memset(runtime->gpr, 0, sizeof(runtime->gpr));
    memset(runtime->fpr, 0, sizeof(runtime->fpr));
    memset(runtime->vfpu, 0, sizeof(runtime->vfpu));
    memset(runtime->vfpuCtrl, 0, sizeof(runtime->vfpuCtrl));
    runtime->vfpuCtrl[0] = 0xe4;
    runtime->vfpuCtrl[1] = 0xe4;
    runtime->vfpuCtrl[2] = 0;
    runtime->vfpuCtrl[3] = 0x3f;
    runtime->vfpuCtrl[4] = 0;
    runtime->vfpuCtrl[5] = 0x7772ceab;
    runtime->vfpuCtrl[8] = 0x3f800001;
    runtime->vfpuCtrl[9] = 0x3f800002;
    runtime->vfpuCtrl[10] = 0x3f800004;
    runtime->vfpuCtrl[11] = 0x3f800008;
    runtime->vfpuCtrl[12] = 0x3f800000;
    runtime->vfpuCtrl[13] = 0x3f800000;
    runtime->vfpuCtrl[14] = 0x3f800000;
    runtime->vfpuCtrl[15] = 0x3f800000;
    runtime->pc = 0;
    runtime->hi = 0;
    runtime->lo = 0;
    runtime->fcr31 = 0;
    runtime->fpcond = 0;
    runtime->backend = EXECUTION_BACKEND_INTERPRETER;
    runtime->lastError = RUNTIME_OK;
    runtime->stopRequested.store(false, std::memory_order_release);
    runtime->cachedRegionValid = false;
    runtime->cachedFetchRegionValid = false;
    runtime->hasRangedCodeHooks = false;
    runtime->nextHook = 1;
    runtime->interpreterProfileEnabled = runtimeEnvEnabled("DINGOO_PIE_PROFILE");
    runtime->interpreterProfileLastMicros = 0;
    runtime->interpreterProfileInstructions = 0;
    runtime->interpreterProfileCheckCountdown = 1;
    runtime->interpreterProfileHooks = 0;
    runtime->interpreterPcProfileEnabled = runtimeEnvEnabled("DINGOO_PIE_INTERPRETER_PC_PROFILE");
    runtime->interpreterPcSampleCountdown = 1;
    *out = runtime;
    return RUNTIME_OK;
}

RuntimeError nativeRuntimeDestroy(NativeRuntime* runtime)
{
    delete runtime;
    return RUNTIME_OK;
}

RuntimeError nativeRuntimeRequestStop(NativeRuntime* runtime)
{
    if (!runtime)
    {
        return RUNTIME_ERROR_HANDLE;
    }

    runtime->stopRequested.store(true, std::memory_order_release);
    if (runtime->backend == EXECUTION_BACKEND_PPSSPP_IRJIT)
    {
        ppssppIrJitRequestStop(runtime);
    }
    return RUNTIME_OK;
}

RuntimeError nativeRuntimeStart(NativeRuntime* runtime, uint64_t begin, uint64_t until, uint64_t timeout, size_t count)
{
    if (!runtime)
    {
        return RUNTIME_ERROR_HANDLE;
    }

    (void)timeout;
    runtime->pc = (uint32_t)begin;
    runtime->lastError = RUNTIME_OK;
    if (runtime->stopRequested.load(std::memory_order_acquire))
    {
        runtime->pc = (uint32_t)until;
        return RUNTIME_OK;
    }

    if (runtime->backend == EXECUTION_BACKEND_PPSSPP_IRJIT)
    {
        return ppssppIrJitStart(runtime, begin, until, timeout, count);
    }

    size_t executed = 0;
    uint32_t pausePollCountdown = 1;
    while (runtime->pc != (uint32_t)until)
    {
        if (runtime->stopRequested.load(std::memory_order_acquire))
        {
            runtime->pc = (uint32_t)until;
            break;
        }
        if (--pausePollCountdown == 0)
        {
            pausePollCountdown = 256;
            if (!waitForInterpreterResume(runtime))
            {
                runtime->pc = (uint32_t)until;
                break;
            }
        }
        tracePcSample(runtime, runtime->pc);
        bool branched = false;
        if (!executeOne(runtime, true, &branched))
        {
            return runtime->lastError == RUNTIME_OK ? RUNTIME_ERROR_EXCEPTION : runtime->lastError;
        }
        runtime->gpr[0] = 0;
        executed++;
        profileInterpreterInstruction(runtime);
        if (count && executed >= count)
        {
            break;
        }
    }
    return RUNTIME_OK;
}

RuntimeError nativeRuntimeSetBackend(NativeRuntime* runtime, ExecutionBackend backend)
{
    if (!runtime)
    {
        return RUNTIME_ERROR_HANDLE;
    }
    runtime->backend = backend;
    return RUNTIME_OK;
}

ExecutionBackend nativeRuntimeGetBackend(NativeRuntime* runtime)
{
    if (!runtime)
    {
        return EXECUTION_BACKEND_INTERPRETER;
    }
    return runtime->backend;
}

RuntimeError nativeRuntimeReadRegister(NativeRuntime* runtime, int regid, void* value)
{
    if (!runtime || !value)
    {
        return RUNTIME_ERROR_ARG;
    }

    uint32_t out = 0;
    if (regid >= 0 && regid < 32)
    {
        out = runtime->gpr[regid];
    }
    else if (regid == RUNTIME_REG_PC)
    {
        out = runtime->pc;
    }
    else if (regid == RUNTIME_REG_HI)
    {
        out = runtime->hi;
    }
    else if (regid == RUNTIME_REG_LO)
    {
        out = runtime->lo;
    }
    else
    {
        return RUNTIME_ERROR_ARG;
    }

    memcpy(value, &out, sizeof(out));
    return RUNTIME_OK;
}

RuntimeError nativeRuntimeWriteRegister(NativeRuntime* runtime, int regid, const void* value)
{
    if (!runtime || !value)
    {
        return RUNTIME_ERROR_ARG;
    }

    uint32_t in = 0;
    memcpy(&in, value, sizeof(in));
    if (regid >= 0 && regid < 32)
    {
        setReg(runtime, regid, in);
    }
    else if (regid == RUNTIME_REG_PC)
    {
        runtime->pc = in;
    }
    else if (regid == RUNTIME_REG_HI)
    {
        runtime->hi = in;
    }
    else if (regid == RUNTIME_REG_LO)
    {
        runtime->lo = in;
    }
    else
    {
        return RUNTIME_ERROR_ARG;
    }

    runtime->gpr[0] = 0;
    return RUNTIME_OK;
}

RuntimeError nativeRuntimeMapMemory(NativeRuntime* runtime, uint64_t address, size_t size, uint32_t perms, void* ptr)
{
    if (!runtime || !ptr || size == 0 || address > 0xffffffffu)
    {
        return RUNTIME_ERROR_ARG;
    }

    MemoryRegion region;
    region.start = (uint32_t)address;
    region.size = (uint32_t)size;
    region.data = (uint8_t*)ptr;
    region.perms = perms;
    runtime->regions.push_back(region);
    std::sort(runtime->regions.begin(), runtime->regions.end(), [](const MemoryRegion& a, const MemoryRegion& b) {
        return a.start < b.start;
    });
    runtime->cachedRegionValid = false;
    runtime->cachedFetchRegionValid = false;
    return RUNTIME_OK;
}

RuntimeError nativeRuntimeReadMemory(NativeRuntime* runtime, uint64_t address, void* bytes, size_t size)
{
    if (!runtime || !bytes || address > 0xffffffffu)
    {
        return RUNTIME_ERROR_ARG;
    }
    if (!readMemRaw(runtime, (uint32_t)address, bytes, size))
    {
        return RUNTIME_ERROR_READ_UNMAPPED;
    }
    return RUNTIME_OK;
}

RuntimeError nativeRuntimeWriteMemory(NativeRuntime* runtime, uint64_t address, const void* bytes, size_t size)
{
    if (!runtime || !bytes || address > 0xffffffffu)
    {
        return RUNTIME_ERROR_ARG;
    }
    if (!writeMemRaw(runtime, (uint32_t)address, bytes, size))
    {
        return RUNTIME_ERROR_WRITE_UNMAPPED;
    }
    return RUNTIME_OK;
}

uint32_t* nativeRuntimeGpr(NativeRuntime* runtime)
{
    return runtime ? runtime->gpr : NULL;
}

uint32_t* nativeRuntimePc(NativeRuntime* runtime)
{
    return runtime ? &runtime->pc : NULL;
}

uint32_t* nativeRuntimeHi(NativeRuntime* runtime)
{
    return runtime ? &runtime->hi : NULL;
}

uint32_t* nativeRuntimeLo(NativeRuntime* runtime)
{
    return runtime ? &runtime->lo : NULL;
}

float* nativeRuntimeFpr(NativeRuntime* runtime)
{
    return runtime ? runtime->fpr : NULL;
}

float* nativeRuntimeVfpu(NativeRuntime* runtime)
{
    return runtime ? runtime->vfpu : NULL;
}

uint32_t* nativeRuntimeVfpuCtrl(NativeRuntime* runtime)
{
    return runtime ? runtime->vfpuCtrl : NULL;
}

uint32_t* nativeRuntimeFcr31(NativeRuntime* runtime)
{
    return runtime ? &runtime->fcr31 : NULL;
}

uint32_t* nativeRuntimeFpCond(NativeRuntime* runtime)
{
    return runtime ? &runtime->fpcond : NULL;
}

size_t nativeRuntimeMemoryRegionCount(NativeRuntime* runtime)
{
    return runtime ? runtime->regions.size() : 0;
}

bool nativeRuntimeGetMemoryRegion(NativeRuntime* runtime, size_t index, RuntimeMemoryRegion* out)
{
    if (!runtime || !out || index >= runtime->regions.size())
    {
        return false;
    }

    const MemoryRegion& region = runtime->regions[index];
    out->start = region.start;
    out->size = region.size;
    out->data = region.data;
    out->perms = region.perms;
    return true;
}

bool nativeRuntimeReadRaw(NativeRuntime* runtime, uint32_t address, void* out, size_t size)
{
    return runtime && out && readMemRaw(runtime, address, out, size);
}

bool nativeRuntimeWriteRaw(NativeRuntime* runtime, uint32_t address, const void* in, size_t size)
{
    return runtime && in && writeMemRaw(runtime, address, in, size);
}

uint8_t* nativeRuntimeHostPointer(NativeRuntime* runtime, uint32_t address, size_t size)
{
    return runtime ? hostPtrFor(runtime, address, size) : NULL;
}

void nativeRuntimeCallCodeHooks(NativeRuntime* runtime, uint32_t address)
{
    if (runtime)
    {
        callCodeHooks(runtime, address);
    }
}

bool nativeRuntimeHasCodeHook(NativeRuntime* runtime, uint32_t address)
{
    return runtime ? hasCodeHook(runtime, address) : false;
}

RuntimeError nativeRuntimeAddHook(NativeRuntime* runtime, RuntimeHook* hh, int type, void* callback, void* user_data, uint64_t begin, uint64_t end, ...)
{
    if (!runtime || !hh || !callback || begin > 0xffffffffu || end > 0xffffffffu)
    {
        return RUNTIME_ERROR_ARG;
    }

    HookEntry hook;
    hook.handle = runtime->nextHook++;
    hook.type = type;
    hook.begin = (uint32_t)begin;
    hook.end = (uint32_t)end;
    hook.callback = callback;
    hook.userData = user_data;
    hook.active = true;

    std::lock_guard<std::mutex> lock(runtime->hookMutex);
    size_t hookIndex = runtime->hooks.size();
    runtime->hooks.push_back(hook);
    if (type & RUNTIME_HOOK_CODE)
    {
        if (hook.begin == hook.end)
        {
            markCodeHookPages(runtime, hook.begin, hook.end, kCodeHookPageExact);
            indexExactCodeHook(runtime, hook.begin, hookIndex);
            runtime->codeHookIndex[hook.begin].push_back(hookIndex);
        }
        else
        {
            markCodeHookPages(runtime, hook.begin, hook.end, kCodeHookPageRange);
            runtime->hasRangedCodeHooks = true;
            runtime->rangedCodeHooks.push_back(hookIndex);
        }
    }
    *hh = hook.handle;
    return RUNTIME_OK;
}

RuntimeError nativeRuntimeRemoveHook(NativeRuntime* runtime, RuntimeHook hh)
{
    if (!runtime)
    {
        return RUNTIME_ERROR_ARG;
    }

    std::lock_guard<std::mutex> lock(runtime->hookMutex);
    for (size_t i = 0; i < runtime->hooks.size(); ++i)
    {
        if (runtime->hooks[i].handle == hh)
        {
            runtime->hooks[i].active = false;
            return RUNTIME_OK;
        }
    }
    return RUNTIME_ERROR_HOOK;
}

RuntimeError nativeRuntimeFlushCodeCache(NativeRuntime* runtime)
{
    if (runtime && runtime->backend == EXECUTION_BACKEND_PPSSPP_IRJIT)
    {
        return ppssppIrJitFlushCodeCache(runtime);
    }
    return RUNTIME_OK;
}

void nativeRuntimeNotifyMemoryAccess(NativeRuntime* runtime, RuntimeMemoryAccess type, uint32_t address, int size, int64_t value)
{
    if (!runtime || size <= 0)
    {
        return;
    }
    if (type == RUNTIME_MEM_WRITE || type == RUNTIME_MEM_READ || type == RUNTIME_MEM_FETCH || type == RUNTIME_MEM_READ_AFTER)
    {
        callValidMemoryHooks(runtime, type, address, size, value);
    }
    else
    {
        callInvalidMemoryHooks(runtime, type, address, size, value);
    }
}

const char* nativeRuntimeErrorString(RuntimeError err)
{
    switch (err)
    {
    case RUNTIME_OK: return "OK";
    case RUNTIME_ERROR_NOMEM: return "Out of memory";
    case RUNTIME_ERROR_HANDLE: return "Invalid handle";
    case RUNTIME_ERROR_READ_UNMAPPED: return "Invalid memory read";
    case RUNTIME_ERROR_WRITE_UNMAPPED: return "Invalid memory write";
    case RUNTIME_ERROR_FETCH_UNMAPPED: return "Invalid memory fetch";
    case RUNTIME_ERROR_HOOK: return "Invalid hook";
    case RUNTIME_ERROR_INSN_INVALID: return "Invalid instruction";
    case RUNTIME_ERROR_ARG: return "Invalid argument";
    case RUNTIME_ERROR_EXCEPTION: return "Guest exception";
    case RUNTIME_ERROR_BACKEND_UNAVAILABLE: return "Execution backend unavailable";
    default: return "Unknown error";
    }
}
