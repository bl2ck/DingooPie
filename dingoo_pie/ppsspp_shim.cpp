#include "ppsspp_irjit_backend.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <atomic>
#include <chrono>
#include <initializer_list>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "emulated_memory.h"
#include "framebuffer.h"
#include "pause_gate.h"
#include "sdk_hle.h"
#include "input_state.h"
#include "guest_filesystem.h"
#include "runtime_log.h"
#include "runtime_resource_monitor.h"
#include "Common/CPUDetect.h"
#include "Common/File/FileUtil.h"
#include "Common/File/VFS/VFS.h"
#include "Common/Log.h"
#include "Common/Serialize/Serializer.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/CoreParameter.h"
#include "Core/CoreTiming.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/ReplaceTables.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/JitCommon/JitState.h"
#include "Core/MIPS/MIPSDebugInterface.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/MIPS/MIPSTracer.h"
#include "Core/Reporting.h"

namespace http
{
class RequestManager
{
};
}

static NativeRuntime* g_ppssppRuntime = NULL;
static uint64_t g_runtimeBeginTicks = 0;
static uint64_t g_runtimeMaxTicks = 0;
static bool g_logEnabled = true;
static int g_irjitTraceEnabled = -1;
static std::atomic<bool> g_ppssppProfileEnabled(false);
static bool g_fastPageDirectEnabled = true;
static int g_fastPageDirectOverride = -1;
static bool g_fastFramebufferDirectEnabled = true;
static std::atomic<bool> g_irjitThrottleEnabled(false);
static std::atomic<bool> g_runtimePauseRequested(false);
static std::atomic<double> g_runtimeSpeedScale(0.0);
static std::atomic<uint32_t> g_irjitThrottleAheadLimitUs(1000000);
static std::atomic<uint32_t> g_irjitThrottleMaxLagUs(100000);
static std::atomic<uint64_t> g_throttleStartTicks(0);
static std::atomic<uint64_t> g_throttleStartUs(0);
static std::atomic<bool> g_runtimeStopRequested(false);
static uint64_t g_ppssppThrottleSleepMs = 0;
static uint64_t g_ppssppLastProfileTicks = 0;
static uint64_t g_ppssppHookCalls = 0;
static uint64_t g_ppssppAdvanceCalls = 0;
static uint64_t g_ppssppReads = 0;
static uint64_t g_ppssppWrites = 0;
static uint64_t g_ppssppFastHleCalls = 0;
static uint64_t g_ppssppLastProfileCoreTicks = 0;
static uint64_t g_ppssppFastLcdCalls = 0;
static uint64_t g_ppssppFastFreadCalls = 0;
static uint64_t g_ppssppFastFreadBytes = 0;
static uint64_t g_ppssppFastFseekCalls = 0;
static uint64_t g_ppssppFastAudioCalls = 0;
static uint64_t g_ppssppFastSemCalls = 0;
static uint64_t g_fastHleAddressRefreshMs = 0;
static uint32_t g_fastHleAddressRefreshAttempts = 0;
static std::mutex g_emuhackMutex;
static std::unordered_map<uint32_t, uint32_t> g_emuhackOriginalOps;

struct FastHleAddresses
{
    bool initialized;
    uint32_t mallocFn;
    uint32_t freeFn;
    uint32_t reallocFn;
    uint32_t lcdGetFrame;
    uint32_t lcdGetFrameLegacy;
    uint32_t lcdGetCFrame;
    uint32_t lcdSetFrame;
    uint32_t lcdSetFrameLegacy;
    uint32_t apLcdSetFrame;
    uint32_t lcdFlip;
    uint32_t kbdGetStatus;
    uint32_t kbdGetStatusLegacy;
    uint32_t kbdGetKey;
    uint32_t kbdGetKeyLegacy;
    uint32_t sysJudgeEvent;
    uint32_t sysJudgeEventLegacy;
    uint32_t freadFn;
    uint32_t fseekFn;
    uint32_t fsysFread;
    uint32_t fsysFseek;
    uint32_t fsysFtell;
    uint32_t fsysFeof;
    uint32_t waveoutWrite;
    uint32_t waveoutCanWrite;
    uint32_t osSemPend;
    uint32_t osSemPost;
};

static FastHleAddresses g_fastHleAddresses = {};

struct FastMemoryRegion
{
    uint32_t start;
    uint32_t end;
    uint8_t* data;
};

struct FastMemoryPage
{
    uint8_t* base;
    uint32_t start;
    uint32_t end;
};

static const uint32_t kFastPageBits = 12;
static const uint32_t kFastPageSize = 1u << kFastPageBits;
static const uint32_t kFastPageMask = kFastPageSize - 1;
static const uint32_t kFastPageCount = 1u << (32 - kFastPageBits);
static std::vector<FastMemoryRegion> g_fastRegions;
static std::vector<FastMemoryPage> g_fastPages;
static uintptr_t* g_fastPageBases = NULL;
static size_t g_lastFastRegion = (size_t)-1;
static uint32_t g_irjitDispatchTraceCount = 0;
static uint32_t g_irjitHookTraceCount = 0;
bool* g_bLogEnabledSetting = &g_logEnabled;
LogChannel g_log[(size_t)Log::NUMBER_OF_LOGS];
CPUInfo cpu_info;
Config g_Config;
CoreParameter g_CoreParameter;
volatile CoreState coreState = CORE_RUNNING_CPU;
volatile bool coreStatePending = false;
static const int kDefaultIrJitCpuHz = 336000000;
static const int kMinIrJitCpuHz = 1000000;
static const int kMaxIrJitCpuHz = 1000000000;
static const int kDefaultIrJitSlicesPerSecond = 240;
static const double kAutoRuntimeSpeedScale = 0.65;
int CPU_HZ = kDefaultIrJitCpuHz;
int CoreTiming::slicelength = kDefaultIrJitCpuHz / kDefaultIrJitSlicesPerSecond;
BreakpointManager g_breakpoints;
SymbolMap* g_symbolMap = NULL;
VFS g_VFS;
MIPSTracer mipsTracer;
http::RequestManager g_DownloadManager;

namespace Memory
{
u8* base = NULL;
u32 g_MemorySize = RAM_DOUBLE_SIZE;
u32 g_PSPModel = PSP_MODEL_SLIM;
}

MIPSState mipsr4k;
MIPSState* currentMIPS = &mipsr4k;
MIPSDebugInterface* currentDebugMIPS = NULL;
u8 voffset[128];
u8 fromvoffset[128];

const float cst_constants[32] = {
    0.0f,
    3.4028234663852886e+38f,
    1.4142135623730951f,
    0.7071067811865476f,
    1.1283791670955126f,
    0.6366197723675813f,
    0.3183098861837907f,
    0.7853981633974483f,
    1.5707963267948966f,
    3.1415926535897932f,
    2.7182818284590452f,
    1.4426950408889634f,
    0.4342944819032518f,
    0.6931471805599453f,
    2.3025850929940457f,
    6.2831853071795865f,
    0.5235987755982989f,
    0.3010299956639812f,
    3.3219280948873623f,
    0.8660254037844386f,
};

namespace MIPSComp
{
JitInterface* jit = NULL;
std::recursive_mutex jitLock;

void JitAt()
{
    if (jit && currentMIPS)
    {
        jit->Compile(currentMIPS->pc);
    }
}

void DoDummyJitState(PointerWrap&)
{
}

JitInterface* CreateNativeJit(MIPSState*, bool)
{
    return NULL;
}

BranchInfo::BranchInfo(u32 pc, MIPSOpcode op, MIPSOpcode delaySlotOp, bool andLink, bool likely)
    : compilerPC(pc), op(op), delaySlotOp(delaySlotOp), likely(likely), andLink(andLink)
{
    delaySlotInfo = MIPSGetInfo(delaySlotOp).value;
    delaySlotIsBranch = (delaySlotInfo & (IS_JUMP | IS_CONDBRANCH)) != 0;
}

u32 ResolveNotTakenTarget(const BranchInfo& branchInfo)
{
    u32 notTakenTarget = branchInfo.compilerPC + 8;
    if ((branchInfo.delaySlotInfo & (IS_JUMP | IS_CONDBRANCH)) != 0)
    {
        bool isJump = (branchInfo.delaySlotInfo & IS_JUMP) != 0;
        if (isJump || !branchInfo.likely)
        {
            notTakenTarget -= 4;
        }
    }
    return notTakenTarget;
}
}

static uint32_t loadLe32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void storeLe32(uint8_t* p, uint32_t value)
{
    p[0] = (uint8_t)(value & 0xff);
    p[1] = (uint8_t)((value >> 8) & 0xff);
    p[2] = (uint8_t)((value >> 16) & 0xff);
    p[3] = (uint8_t)((value >> 24) & 0xff);
}

static uint16_t loadFastLe16(const uint8_t* p)
{
    return (uint16_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8));
}

static uint32_t loadFastLe32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t loadFastLe64(const uint8_t* p)
{
    return (uint64_t)loadFastLe32(p) | ((uint64_t)loadFastLe32(p + 4) << 32);
}

static void storeFastLe16(uint8_t* p, uint32_t value)
{
    p[0] = (uint8_t)(value & 0xff);
    p[1] = (uint8_t)((value >> 8) & 0xff);
}

static void storeFastLe64(uint8_t* p, uint64_t value)
{
    storeLe32(p, (uint32_t)value);
    storeLe32(p + 4, (uint32_t)(value >> 32));
}

static uint32_t canonicalGuestAddress(uint32_t address)
{
    return address & 0x1fffffffu;
}

static bool irjitTraceEnabled()
{
    if (g_irjitTraceEnabled < 0)
    {
        const char* value = getenv("DINGOO_PIE_IRJIT_TRACE");
        g_irjitTraceEnabled = value && value[0] && strcmp(value, "0") != 0 ? 1 : 0;
    }
    return g_irjitTraceEnabled != 0;
}

static bool ppssppShimLogEnabled()
{
    static int enabled = -1;
    if (enabled < 0)
    {
        const char* value = getenv("DINGOO_PIE_IRJIT_LOG");
        enabled = value && value[0] && strcmp(value, "0") != 0 ? 1 : 0;
    }
    return enabled != 0 || irjitTraceEnabled();
}

static const char* describeCop0Fallback(uint32_t op)
{
    uint32_t rs = (op >> 21) & 0x1f;
    if (rs == 0x00)
    {
        return "mfc0";
    }
    if (rs == 0x04)
    {
        return "mtc0";
    }
    if (rs == 0x0a)
    {
        return "rdpgpr";
    }
    if (rs == 0x0b)
    {
        return "mfmc0";
    }
    if (rs == 0x0e)
    {
        return "wrpgpr";
    }
    if (rs >= 0x10)
    {
        switch (op & 0x3f)
        {
        case 0x01: return "tlbr";
        case 0x02: return "tlbwi";
        case 0x06: return "tlbwr";
        case 0x08: return "tlbp";
        case 0x18: return "eret";
        case 0x19: return "iack";
        case 0x1f: return "deret";
        case 0x20: return "wait";
        default: return "cop0";
        }
    }
    return "cop0";
}

static bool isDingooSafeNullInterpreterOp(uint32_t op)
{
    uint32_t primary = op >> 26;
    if (primary != 0x10)
    {
        return false;
    }

    uint32_t rs = (op >> 21) & 0x1f;
    uint32_t func = op & 0x3f;
    return rs == 0x00 || rs == 0x04 || rs == 0x0a || rs == 0x0b || rs == 0x0e ||
        (rs >= 0x10 && (func == 0x01 || func == 0x02 || func == 0x06 || func == 0x08 ||
            func == 0x18 || func == 0x19 || func == 0x1f || func == 0x20));
}

static uint64_t ppssppShimNowMs()
{
    using namespace std::chrono;
    return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

static uint64_t ppssppShimNowUs()
{
    using namespace std::chrono;
    return (uint64_t)duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

static int ppssppShimParsePositiveEnv(const char* name, int defaultValue, int minValue, int maxValue)
{
    const char* value = getenv(name);
    if (!value || !value[0])
    {
        return defaultValue;
    }

    char* end = NULL;
    long parsed = strtol(value, &end, 0);
    if (end == value || parsed < minValue || parsed > maxValue)
    {
        printf("ppsspp-irjit: ignoring invalid %s=%s, using %d\n", name, value, defaultValue);
        return defaultValue;
    }

    return (int)parsed;
}

static int ppssppShimDefaultSliceLengthForClock(int cpuHz)
{
    int slice = cpuHz / kDefaultIrJitSlicesPerSecond;
    if (slice < 10000)
    {
        return 10000;
    }
    if (slice > 10000000)
    {
        return 10000000;
    }
    return slice;
}

static void ppssppShimApplyCpuClockSettings(void)
{
    int cpuHz = ppssppShimParsePositiveEnv(
        "DINGOO_PIE_IRJIT_CLOCK_HZ",
        kDefaultIrJitCpuHz,
        kMinIrJitCpuHz,
        kMaxIrJitCpuHz);
    int defaultSlice = ppssppShimDefaultSliceLengthForClock(cpuHz);
    int sliceLength = ppssppShimParsePositiveEnv(
        "DINGOO_PIE_IRJIT_SLICE",
        defaultSlice,
        10000,
        10000000);

    CPU_HZ = cpuHz;
    CoreTiming::slicelength = sliceLength;
}

static bool ppssppShimParseEnabledEnv(const char* name, bool defaultValue)
{
    const char* value = getenv(name);
    if (!value || !value[0])
    {
        return defaultValue;
    }
    return strcmp(value, "0") != 0;
}

static double ppssppShimParseSpeedScaleEnv(const char* name)
{
    const char* value = getenv(name);
    if (!value || !value[0])
    {
        return kAutoRuntimeSpeedScale;
    }

    char* end = NULL;
    double parsed = strtod(value, &end);
    if (end == value || parsed <= 0.0)
    {
        printf("ppsspp-irjit: invalid %s='%s', using auto speed\n", name, value);
        return kAutoRuntimeSpeedScale;
    }
    if (parsed > 1.0)
    {
        parsed = 1.0;
    }
    if (parsed < 0.10)
    {
        parsed = 0.10;
    }
    return parsed;
}

void ppssppShimApplyRuntimeSettings(void)
{
    ppssppShimApplyCpuClockSettings();
    double speedScale = ppssppShimParseSpeedScaleEnv("DINGOO_PIE_RUNTIME_SPEED_SCALE");
    bool profileEnabled = runtimeLogProfileEnabled();

    // Dingoo SDK timers and delays are handled by HLE by default. Runtime speed
    // scaling opts into wall-clock throttling for animation-sensitive samples.
    bool throttleEnabled = speedScale > 0.0 ||
        ppssppShimParseEnabledEnv("DINGOO_PIE_IRJIT_THROTTLE", false);
    int defaultAheadMs = speedScale > 0.0 ? 16 : 1000;
    uint32_t aheadLimitUs = (uint32_t)ppssppShimParsePositiveEnv(
        "DINGOO_PIE_IRJIT_THROTTLE_AHEAD_MS", defaultAheadMs, 0, 5000) * 1000u;
    uint32_t maxLagUs = (uint32_t)ppssppShimParsePositiveEnv(
        "DINGOO_PIE_IRJIT_THROTTLE_MAX_LAG_MS", 100, 1, 5000) * 1000u;

    g_runtimeSpeedScale.store(speedScale);
    g_ppssppProfileEnabled.store(profileEnabled);
    g_irjitThrottleEnabled.store(throttleEnabled);
    g_irjitThrottleAheadLimitUs.store(aheadLimitUs);
    g_irjitThrottleMaxLagUs.store(maxLagUs);

    if (g_ppssppRuntime)
    {
        g_throttleStartTicks.store(CoreTiming::GetTicks());
        g_throttleStartUs.store(ppssppShimNowUs());
    }
    else
    {
        g_throttleStartTicks.store(0);
        g_throttleStartUs.store(0);
    }

    if (irjitTraceEnabled())
    {
        printf("ppsspp-irjit: runtime settings throttle=%u speed_scale=%.3f clock_hz=%d slice=%d ahead_ms=%u max_lag_ms=%u profile=%u\n",
            throttleEnabled ? 1u : 0u,
            speedScale,
            CPU_HZ,
            CoreTiming::slicelength,
            aheadLimitUs / 1000u,
            maxLagUs / 1000u,
            profileEnabled ? 1u : 0u);
    }
}

static bool ppssppShimFlagTokenEquals(const char* tokenStart, size_t tokenLength, const char* expected)
{
    size_t expectedLength = strlen(expected);
    return tokenLength == expectedLength && _strnicmp(tokenStart, expected, expectedLength) == 0;
}

static uint32_t ppssppShimParseJitDisableFlags(uint32_t defaultFlags)
{
    const char* value = getenv("DINGOO_PIE_IRJIT_DISABLE_FLAGS");
    if (!value || !value[0])
    {
        return defaultFlags;
    }

    char* end = NULL;
    unsigned long numeric = strtoul(value, &end, 0);
    if (end && *end == '\0')
    {
        return (uint32_t)numeric;
    }

    uint32_t flags = 0;
    const char* cursor = value;
    while (*cursor)
    {
        while (*cursor == ' ' || *cursor == '\t' || *cursor == ',' || *cursor == '|')
        {
            ++cursor;
        }
        const char* tokenStart = cursor;
        while (*cursor && *cursor != ' ' && *cursor != '\t' && *cursor != ',' && *cursor != '|')
        {
            ++cursor;
        }
        size_t tokenLength = (size_t)(cursor - tokenStart);
        if (tokenLength == 0)
        {
            continue;
        }

        uint32_t flag = 0;
        if (ppssppShimFlagTokenEquals(tokenStart, tokenLength, "none") ||
            ppssppShimFlagTokenEquals(tokenStart, tokenLength, "0"))
        {
            flags = 0;
            continue;
        }
        if (ppssppShimFlagTokenEquals(tokenStart, tokenLength, "default"))
        {
            flags |= defaultFlags;
            continue;
        }
        if (ppssppShimFlagTokenEquals(tokenStart, tokenLength, "blocklink") ||
            ppssppShimFlagTokenEquals(tokenStart, tokenLength, "block_link"))
        {
            flag = (uint32_t)MIPSComp::JitDisable::BLOCKLINK;
        }
        else if (ppssppShimFlagTokenEquals(tokenStart, tokenLength, "cache_pointers") ||
            ppssppShimFlagTokenEquals(tokenStart, tokenLength, "cache-pointers"))
        {
            flag = (uint32_t)MIPSComp::JitDisable::CACHE_POINTERS;
        }
        else if (ppssppShimFlagTokenEquals(tokenStart, tokenLength, "pointerify"))
        {
            flag = (uint32_t)MIPSComp::JitDisable::POINTERIFY;
        }
        else if (ppssppShimFlagTokenEquals(tokenStart, tokenLength, "regalloc_gpr"))
        {
            flag = (uint32_t)MIPSComp::JitDisable::REGALLOC_GPR;
        }
        else if (ppssppShimFlagTokenEquals(tokenStart, tokenLength, "regalloc_fpr"))
        {
            flag = (uint32_t)MIPSComp::JitDisable::REGALLOC_FPR;
        }
        else
        {
            printf("ppsspp-irjit: unknown DINGOO_PIE_IRJIT_DISABLE_FLAGS token '%.*s'\n",
                (int)tokenLength, tokenStart);
            continue;
        }

        flags |= flag;
    }

    return flags;
}

static uint64_t ticksToMicroseconds(uint64_t ticks, uint32_t clockHz)
{
    if (!clockHz)
    {
        return 0;
    }
    uint64_t seconds = ticks / clockHz;
    uint64_t remainder = ticks % clockHz;
    return seconds * 1000000ull + (remainder * 1000000ull) / clockHz;
}

static void ppssppShimThrottleToGuestClock(uint64_t currentTicks)
{
    bool throttleEnabled = g_irjitThrottleEnabled.load();
    double speedScale = g_runtimeSpeedScale.load();
    uint32_t aheadLimitUs = g_irjitThrottleAheadLimitUs.load();
    uint32_t maxLagUs = g_irjitThrottleMaxLagUs.load();
    uint64_t throttleStartTicks = g_throttleStartTicks.load();
    uint64_t throttleStartUs = g_throttleStartUs.load();
    if (!throttleEnabled || !throttleStartUs)
    {
        return;
    }

    uint64_t guestElapsedUs = ticksToMicroseconds(currentTicks - throttleStartTicks, (uint32_t)CPU_HZ);
    if (speedScale > 0.0 && speedScale < 1.0)
    {
        guestElapsedUs = (uint64_t)((double)guestElapsedUs / speedScale);
    }
    uint64_t hostElapsedUs = ppssppShimNowUs() - throttleStartUs;
    if (hostElapsedUs > guestElapsedUs + maxLagUs)
    {
        g_throttleStartTicks.store(currentTicks);
        g_throttleStartUs.store(ppssppShimNowUs());
        return;
    }
    if (guestElapsedUs <= hostElapsedUs)
    {
        return;
    }

    uint64_t aheadUs = guestElapsedUs - hostElapsedUs;
    if (aheadUs <= aheadLimitUs)
    {
        return;
    }

    uint64_t sleepUs = aheadUs - aheadLimitUs;
    if (sleepUs < 500)
    {
        return;
    }

    std::this_thread::sleep_for(std::chrono::microseconds(sleepUs));
    g_ppssppThrottleSleepMs += sleepUs / 1000;
}

static void ppssppShimProfileTick()
{
    if (!g_ppssppProfileEnabled.load())
    {
        return;
    }

    uint64_t now = ppssppShimNowMs();
    if (!g_ppssppLastProfileTicks)
    {
        g_ppssppLastProfileTicks = now;
        return;
    }
    if (now - g_ppssppLastProfileTicks < runtimeLogProfileIntervalMs())
    {
        return;
    }

    bridge_profile_tick();

    uint64_t coreTicks = CoreTiming::GetTicks();
    uint64_t elapsedMs = now - g_ppssppLastProfileTicks;
    uint64_t tickDelta = g_ppssppLastProfileCoreTicks ? coreTicks - g_ppssppLastProfileCoreTicks : 0;
    unsigned guestMhz = elapsedMs ? (unsigned)(tickDelta / elapsedMs / 1000) : 0;

    uint64_t submittedFrames = consumeFramebufferSubmittedCount();
    uint64_t framebufferCopyMicros = consumeFramebufferCopyMicros();

    uint64_t totalFrameIntervalMicros = 0;
    uint64_t maxFrameIntervalMicros = 0;
    uint64_t frameIntervalsOver25ms = 0;
    uint64_t frameIntervalsOver33ms = 0;
    consumeFramebufferTimingStats(&totalFrameIntervalMicros, &maxFrameIntervalMicros,
        &frameIntervalsOver25ms, &frameIntervalsOver33ms);
    uint64_t avgFrameIntervalMicros = submittedFrames ? totalFrameIntervalMicros / submittedFrames : 0;
    bool throttleEnabled = g_irjitThrottleEnabled.load();
    uint32_t aheadLimitUs = g_irjitThrottleAheadLimitUs.load();

    if (!g_ppssppHookCalls && !g_ppssppFastHleCalls && !g_ppssppFastLcdCalls &&
        !g_ppssppFastAudioCalls && !g_ppssppFastSemCalls && !g_ppssppAdvanceCalls &&
        !g_ppssppReads && !g_ppssppWrites && !g_ppssppFastFreadCalls &&
        !g_ppssppFastFreadBytes && !g_ppssppFastFseekCalls && !g_ppssppThrottleSleepMs &&
        !submittedFrames && !framebufferCopyMicros && !avgFrameIntervalMicros &&
        !maxFrameIntervalMicros && !frameIntervalsOver25ms && !frameIntervalsOver33ms &&
        !runtimeLogShouldPrintEmptyProfile())
    {
        g_ppssppLastProfileCoreTicks = coreTicks;
        g_ppssppLastProfileTicks = now;
        return;
    }

    printf("profile:irjit hooks=%llu/s fast_hle=%llu/s fast_lcd=%llu/s fast_audio=%llu/s fast_sem=%llu/s advances=%llu/s reads=%llu/s writes=%llu/s fast_fread=%llu/%llub fast_fseek=%llu guest_mhz=%u throttle=%u throttle_sleep_ms=%llu throttle_ahead_ms=%u clock_hz=%d fb_submit=%llu fb_copy_us=%llu fb_interval_us=%llu/%llu over25=%llu over33=%llu core_ticks=%llu downcount=%d pc=0x%08x ra=0x%08x core=%s\n",
        (unsigned long long)runtimeLogRatePerSecond(g_ppssppHookCalls, elapsedMs),
        (unsigned long long)runtimeLogRatePerSecond(g_ppssppFastHleCalls, elapsedMs),
        (unsigned long long)runtimeLogRatePerSecond(g_ppssppFastLcdCalls, elapsedMs),
        (unsigned long long)runtimeLogRatePerSecond(g_ppssppFastAudioCalls, elapsedMs),
        (unsigned long long)runtimeLogRatePerSecond(g_ppssppFastSemCalls, elapsedMs),
        (unsigned long long)runtimeLogRatePerSecond(g_ppssppAdvanceCalls, elapsedMs),
        (unsigned long long)runtimeLogRatePerSecond(g_ppssppReads, elapsedMs),
        (unsigned long long)runtimeLogRatePerSecond(g_ppssppWrites, elapsedMs),
        (unsigned long long)g_ppssppFastFreadCalls,
        (unsigned long long)g_ppssppFastFreadBytes,
        (unsigned long long)g_ppssppFastFseekCalls,
        guestMhz,
        throttleEnabled ? 1u : 0u,
        (unsigned long long)g_ppssppThrottleSleepMs,
        aheadLimitUs / 1000u,
        CPU_HZ,
        (unsigned long long)submittedFrames,
        (unsigned long long)framebufferCopyMicros,
        (unsigned long long)avgFrameIntervalMicros,
        (unsigned long long)maxFrameIntervalMicros,
        (unsigned long long)frameIntervalsOver25ms,
        (unsigned long long)frameIntervalsOver33ms,
        (unsigned long long)coreTicks,
        currentMIPS ? currentMIPS->downcount : 0,
        currentMIPS ? currentMIPS->pc : 0,
        currentMIPS ? currentMIPS->r[MIPS_REG_RA] : 0,
        CoreStateToString(coreState));

    g_ppssppHookCalls = 0;
    g_ppssppFastHleCalls = 0;
    g_ppssppAdvanceCalls = 0;
    g_ppssppReads = 0;
    g_ppssppWrites = 0;
    g_ppssppFastLcdCalls = 0;
    g_ppssppFastFreadCalls = 0;
    g_ppssppFastFreadBytes = 0;
    g_ppssppFastFseekCalls = 0;
    g_ppssppFastAudioCalls = 0;
    g_ppssppFastSemCalls = 0;
    g_ppssppLastProfileCoreTicks = coreTicks;
    g_ppssppLastProfileTicks = now;
    g_ppssppThrottleSleepMs = 0;
}

static void refreshFastHleAddresses()
{
    memset(&g_fastHleAddresses, 0x00, sizeof(g_fastHleAddresses));
    bridge_lookup_hook_address("malloc", &g_fastHleAddresses.mallocFn);
    bridge_lookup_hook_address("free", &g_fastHleAddresses.freeFn);
    bridge_lookup_hook_address("realloc", &g_fastHleAddresses.reallocFn);
    bridge_lookup_hook_address("_lcd_get_frame", &g_fastHleAddresses.lcdGetFrame);
    bridge_lookup_hook_address("lcd_get_frame", &g_fastHleAddresses.lcdGetFrameLegacy);
    bridge_lookup_hook_address("lcd_get_cframe", &g_fastHleAddresses.lcdGetCFrame);
    bridge_lookup_hook_address("_lcd_set_frame", &g_fastHleAddresses.lcdSetFrame);
    bridge_lookup_hook_address("lcd_set_frame", &g_fastHleAddresses.lcdSetFrameLegacy);
    bridge_lookup_hook_address("ap_lcd_set_frame", &g_fastHleAddresses.apLcdSetFrame);
    bridge_lookup_hook_address("lcd_flip", &g_fastHleAddresses.lcdFlip);
    bridge_lookup_hook_address("_kbd_get_status", &g_fastHleAddresses.kbdGetStatus);
    bridge_lookup_hook_address("kbd_get_status", &g_fastHleAddresses.kbdGetStatusLegacy);
    bridge_lookup_hook_address("_kbd_get_key", &g_fastHleAddresses.kbdGetKey);
    bridge_lookup_hook_address("kbd_get_key", &g_fastHleAddresses.kbdGetKeyLegacy);
    bridge_lookup_hook_address("_sys_judge_event", &g_fastHleAddresses.sysJudgeEvent);
    bridge_lookup_hook_address("sys_judge_event", &g_fastHleAddresses.sysJudgeEventLegacy);
    bridge_lookup_hook_address("fread", &g_fastHleAddresses.freadFn);
    bridge_lookup_hook_address("fseek", &g_fastHleAddresses.fseekFn);
    bridge_lookup_hook_address("fsys_fread", &g_fastHleAddresses.fsysFread);
    bridge_lookup_hook_address("fsys_fseek", &g_fastHleAddresses.fsysFseek);
    bridge_lookup_hook_address("fsys_ftell", &g_fastHleAddresses.fsysFtell);
    bridge_lookup_hook_address("fsys_feof", &g_fastHleAddresses.fsysFeof);
    bridge_lookup_hook_address("waveout_write", &g_fastHleAddresses.waveoutWrite);
    bridge_lookup_hook_address("waveout_can_write", &g_fastHleAddresses.waveoutCanWrite);
    bridge_lookup_hook_address("OSSemPend", &g_fastHleAddresses.osSemPend);
    bridge_lookup_hook_address("OSSemPost", &g_fastHleAddresses.osSemPost);
    g_fastHleAddresses.initialized = true;
    g_fastHleAddressRefreshAttempts++;
    g_fastHleAddressRefreshMs = ppssppShimNowMs();
    if (g_ppssppProfileEnabled.load() || irjitTraceEnabled())
    {
        printf("ppsspp-irjit: fast-hle addrs waveout_write=0x%08x waveout_can_write=0x%08x ossem_pend=0x%08x ossem_post=0x%08x attempts=%u\n",
            g_fastHleAddresses.waveoutWrite,
            g_fastHleAddresses.waveoutCanWrite,
            g_fastHleAddresses.osSemPend,
            g_fastHleAddresses.osSemPost,
            g_fastHleAddressRefreshAttempts);
    }
}

static bool shouldRetryOptionalFastHleAddresses()
{
    if (!g_fastHleAddresses.initialized)
    {
        return true;
    }

    if (g_fastHleAddresses.waveoutWrite && g_fastHleAddresses.waveoutCanWrite &&
        g_fastHleAddresses.osSemPend && g_fastHleAddresses.osSemPost)
    {
        return false;
    }

    if (g_fastHleAddressRefreshAttempts >= 3)
    {
        return false;
    }

    uint64_t now = ppssppShimNowMs();
    return g_fastHleAddressRefreshMs == 0 || now - g_fastHleAddressRefreshMs >= 2000;
}

static bool readRawCanonical(uint32_t address, void* out, size_t size);

static bool isPpssppRunBlockMarker(uint32_t value)
{
    return MIPS_IS_RUNBLOCK(value);
}

static void rebuildFastMemoryRegions(NativeRuntime* runtime)
{
    // PPSSPP's native JIT normally assumes a PSP memory base. Dingoo apps use
    // runtime-registered guest ranges instead, so we expose a 4 KB page table
    // that generated code can check before falling back to shim callbacks.
    g_fastRegions.clear();
    g_fastPages.clear();
    g_fastPages.resize(kFastPageCount);
    if (!g_fastPageBases)
    {
        g_fastPageBases = (uintptr_t*)calloc(kFastPageCount, sizeof(uintptr_t));
    }
    else
    {
        memset(g_fastPageBases, 0, kFastPageCount * sizeof(uintptr_t));
    }
    g_lastFastRegion = (size_t)-1;

    if (!runtime || !g_fastPageBases)
    {
        return;
    }

    size_t regionCount = nativeRuntimeMemoryRegionCount(runtime);
    uint32_t mappedPages = 0;
    uint32_t mappedFramebufferPages = 0;
    uint32_t skippedFramebufferPages = 0;
    uint32_t partialPages = 0;
    g_fastRegions.reserve(regionCount);
    for (size_t i = 0; i < regionCount; ++i)
    {
        RuntimeMemoryRegion region;
        if (!nativeRuntimeGetMemoryRegion(runtime, i, &region) || !region.data || !region.size)
        {
            continue;
        }

        FastMemoryRegion fastRegion;
        fastRegion.start = region.start;
        fastRegion.end = region.start + region.size;
        fastRegion.data = region.data;
        g_fastRegions.push_back(fastRegion);

        uint32_t firstPage = region.start >> kFastPageBits;
        uint32_t lastPage = (region.start + region.size - 1) >> kFastPageBits;
        for (uint32_t page = firstPage; page <= lastPage; ++page)
        {
            uint32_t pageStart = page << kFastPageBits;
            uint32_t pageEnd = pageStart + kFastPageSize;
            FastMemoryPage& fastPage = g_fastPages[page];
            fastPage.start = region.start > pageStart ? region.start : pageStart;
            fastPage.end = region.start + region.size < pageEnd ? region.start + region.size : pageEnd;
            fastPage.base = region.data - region.start;
            bool framebufferPage = framebufferAddressOverlaps(pageStart, kFastPageSize);
            if (fastPage.start == pageStart && fastPage.end == pageEnd &&
                (!framebufferPage || g_fastFramebufferDirectEnabled))
            {
                g_fastPageBases[page] = (uintptr_t)(region.data - region.start);
                mappedPages++;
                if (framebufferPage)
                {
                    mappedFramebufferPages++;
                }
            }
            else if (framebufferPage)
            {
                skippedFramebufferPages++;
            }
            else
            {
                partialPages++;
            }
        }
    }

    if (ppssppShimLogEnabled())
    {
        printf("ppsspp-shim: fast pages regions=%u mapped=%u mapped_fb=%u skipped_fb=%u partial=%u\n",
            (unsigned)g_fastRegions.size(), mappedPages, mappedFramebufferPages,
            skippedFramebufferPages, partialPages);
    }
}

static uint8_t* findFastPagePointer(uint32_t address, size_t size)
{
    if (g_fastPages.empty())
    {
        return NULL;
    }
    if (size == 0)
    {
        size = 1;
    }
    if (((address & kFastPageMask) + size) > kFastPageSize)
    {
        return NULL;
    }

    size_t pageIndex = address >> kFastPageBits;
    if (pageIndex >= g_fastPages.size())
    {
        return NULL;
    }

    const FastMemoryPage& page = g_fastPages[pageIndex];
    uint64_t end = (uint64_t)address + size;
    if (!page.base || address < page.start || end > page.end)
    {
        return NULL;
    }

    return page.base + address;
}

static uint8_t* findFastMemoryPointerExact(uint32_t address, size_t size)
{
    uint8_t* pagePtr = findFastPagePointer(address, size);
    if (pagePtr)
    {
        return pagePtr;
    }

    if (size == 0)
    {
        size = 1;
    }

    uint64_t begin = address;
    uint64_t end = begin + size;

    if (g_lastFastRegion < g_fastRegions.size())
    {
        const FastMemoryRegion& region = g_fastRegions[g_lastFastRegion];
        if (begin >= region.start && end <= region.end)
        {
            return region.data + (address - region.start);
        }
    }

    for (size_t i = 0; i < g_fastRegions.size(); ++i)
    {
        const FastMemoryRegion& region = g_fastRegions[i];
        if (begin >= region.start && end <= region.end)
        {
            g_lastFastRegion = i;
            return region.data + (address - region.start);
        }
    }

    return NULL;
}

static uint8_t* findFastMemoryPointer(uint32_t address, size_t size)
{
    uint8_t* direct = findFastMemoryPointerExact(address, size);
    if (direct)
    {
        return direct;
    }

    uint32_t alias = canonicalGuestAddress(address);
    if (alias != address)
    {
        direct = findFastMemoryPointerExact(alias, size);
        if (direct)
        {
            return direct;
        }
    }

    return NULL;
}

static bool fastReadRawCanonical(uint32_t address, void* out, size_t size)
{
    uint8_t* ptr = findFastMemoryPointer(address, size);
    if (!ptr)
    {
        return false;
    }

    memcpy(out, ptr, size);
    return true;
}

static bool fastWriteRawCanonical(uint32_t address, const void* in, size_t size)
{
    uint8_t* ptr = findFastMemoryPointer(address, size);
    if (!ptr)
    {
        return false;
    }

    memcpy(ptr, in, size);
    return true;
}

static void rememberEmuHackOriginal(uint32_t address, uint32_t marker)
{
    if (!isPpssppRunBlockMarker(marker))
    {
        return;
    }

    uint8_t bytes[4] = {};
    if (!readRawCanonical(address, bytes, sizeof(bytes)))
    {
        return;
    }

    const uint32_t original = loadLe32(bytes);
    if (isPpssppRunBlockMarker(original))
    {
        return;
    }

    std::lock_guard<std::mutex> lock(g_emuhackMutex);
    g_emuhackOriginalOps[address] = original;
    const uint32_t alias = canonicalGuestAddress(address);
    g_emuhackOriginalOps[alias] = original;
}

static void clearEmuHackOriginals()
{
    // Runblock markers are only valid for the current JIT cache and runtime.
    std::lock_guard<std::mutex> lock(g_emuhackMutex);
    g_emuhackOriginalOps.clear();
}

bool ppssppShimResolveEmuHack(uint32_t address, uint32_t value, uint32_t* original)
{
    if (!original || !isPpssppRunBlockMarker(value))
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_emuhackMutex);
    auto it = g_emuhackOriginalOps.find(address);
    if (it == g_emuhackOriginalOps.end())
    {
        it = g_emuhackOriginalOps.find(canonicalGuestAddress(address));
    }
    if (it == g_emuhackOriginalOps.end())
    {
        return false;
    }
    *original = it->second;
    return true;
}

static bool tryReadRuntimeRegionAlias(uint32_t address, void* out, size_t size, uint32_t mask)
{
    if (!g_ppssppRuntime || !out)
    {
        return false;
    }

    const uint32_t lowOffset = address & mask;
    const size_t regionCount = nativeRuntimeMemoryRegionCount(g_ppssppRuntime);
    for (size_t i = 0; i < regionCount; ++i)
    {
        RuntimeMemoryRegion region;
        if (!nativeRuntimeGetMemoryRegion(g_ppssppRuntime, i, &region))
        {
            continue;
        }

        const uint32_t regionLowStart = region.start & mask;
        if (lowOffset < regionLowStart)
        {
            continue;
        }

        uint32_t candidate = region.start + (lowOffset - regionLowStart);
        if (nativeRuntimeReadRaw(g_ppssppRuntime, candidate, out, size))
        {
            return true;
        }
    }

    return false;
}

static bool tryWriteRuntimeRegionAlias(uint32_t address, const void* in, size_t size, uint32_t mask)
{
    if (!g_ppssppRuntime || !in)
    {
        return false;
    }

    const uint32_t lowOffset = address & mask;
    const size_t regionCount = nativeRuntimeMemoryRegionCount(g_ppssppRuntime);
    for (size_t i = 0; i < regionCount; ++i)
    {
        RuntimeMemoryRegion region;
        if (!nativeRuntimeGetMemoryRegion(g_ppssppRuntime, i, &region))
        {
            continue;
        }

        const uint32_t regionLowStart = region.start & mask;
        if (lowOffset < regionLowStart)
        {
            continue;
        }

        uint32_t candidate = region.start + (lowOffset - regionLowStart);
        if (nativeRuntimeWriteRaw(g_ppssppRuntime, candidate, in, size))
        {
            return true;
        }
    }

    return false;
}

static uint8_t* tryRuntimeRegionAliasPointer(uint32_t address, size_t size, uint32_t mask)
{
    if (!g_ppssppRuntime)
    {
        return NULL;
    }

    const uint32_t lowOffset = address & mask;
    const size_t regionCount = nativeRuntimeMemoryRegionCount(g_ppssppRuntime);
    for (size_t i = 0; i < regionCount; ++i)
    {
        RuntimeMemoryRegion region;
        if (!nativeRuntimeGetMemoryRegion(g_ppssppRuntime, i, &region))
        {
            continue;
        }

        const uint32_t regionLowStart = region.start & mask;
        if (lowOffset < regionLowStart)
        {
            continue;
        }

        uint32_t candidate = region.start + (lowOffset - regionLowStart);
        uint8_t* ptr = nativeRuntimeHostPointer(g_ppssppRuntime, candidate, size);
        if (ptr)
        {
            return ptr;
        }
    }

    return NULL;
}

static uint32_t maxSizeInFastRegion(uint32_t address)
{
    uint32_t best = 0;
    const uint32_t candidates[2] = { address, canonicalGuestAddress(address) };
    for (size_t c = 0; c < 2; ++c)
    {
        uint32_t candidate = candidates[c];
        if (c == 1 && candidate == address)
        {
            continue;
        }
        for (size_t i = 0; i < g_fastRegions.size(); ++i)
        {
            const FastMemoryRegion& region = g_fastRegions[i];
            if (candidate >= region.start && candidate < region.end)
            {
                uint32_t available = region.end - candidate;
                if (available > best)
                {
                    best = available;
                }
            }
        }
    }
    return best;
}

static uint32_t maxSizeInRuntimeRegions(uint32_t address, uint32_t mask)
{
    if (!g_ppssppRuntime)
    {
        return 0;
    }

    const uint32_t lowOffset = address & mask;
    uint32_t best = 0;
    const size_t regionCount = nativeRuntimeMemoryRegionCount(g_ppssppRuntime);
    for (size_t i = 0; i < regionCount; ++i)
    {
        RuntimeMemoryRegion region;
        if (!nativeRuntimeGetMemoryRegion(g_ppssppRuntime, i, &region) || !region.size)
        {
            continue;
        }

        if (address >= region.start && address < region.start + region.size)
        {
            uint32_t available = region.start + region.size - address;
            if (available > best)
            {
                best = available;
            }
        }

        const uint32_t regionLowStart = region.start & mask;
        if (lowOffset >= regionLowStart)
        {
            uint32_t candidate = region.start + (lowOffset - regionLowStart);
            if (candidate >= region.start && candidate < region.start + region.size)
            {
                uint32_t available = region.start + region.size - candidate;
                if (available > best)
                {
                    best = available;
                }
            }
        }
    }
    return best;
}

static bool readRawCanonical(uint32_t address, void* out, size_t size)
{
    if (!g_ppssppRuntime || !out)
    {
        return false;
    }
    if (fastReadRawCanonical(address, out, size))
    {
        return true;
    }
    if (nativeRuntimeReadRaw(g_ppssppRuntime, address, out, size))
    {
        return true;
    }
    uint32_t alias = canonicalGuestAddress(address);
    if (alias != address && nativeRuntimeReadRaw(g_ppssppRuntime, alias, out, size))
    {
        return true;
    }
    if (tryReadRuntimeRegionAlias(address, out, size, 0x1fffffffu))
    {
        return true;
    }
    return tryReadRuntimeRegionAlias(address, out, size, 0x03ffffffu);
}

static bool writeRawCanonical(uint32_t address, const void* in, size_t size)
{
    if (!g_ppssppRuntime || !in)
    {
        return false;
    }
    if (fastWriteRawCanonical(address, in, size))
    {
        return true;
    }
    if (nativeRuntimeWriteRaw(g_ppssppRuntime, address, in, size))
    {
        return true;
    }
    uint32_t alias = canonicalGuestAddress(address);
    if (alias != address && nativeRuntimeWriteRaw(g_ppssppRuntime, alias, in, size))
    {
        return true;
    }
    if (tryWriteRuntimeRegionAlias(address, in, size, 0x1fffffffu))
    {
        return true;
    }
    return tryWriteRuntimeRegionAlias(address, in, size, 0x03ffffffu);
}

static void notifyRuntimeWrite(uint32_t address, int size, int64_t value)
{
    if (g_ppssppRuntime)
    {
        nativeRuntimeNotifyMemoryAccess(g_ppssppRuntime, RUNTIME_MEM_WRITE, address, size, value);
    }
}

static uint8_t* hostPointerCanonical(uint32_t address, size_t size)
{
    if (!g_ppssppRuntime)
    {
        return NULL;
    }
    uint8_t* fastPtr = findFastMemoryPointer(address, size);
    if (fastPtr)
    {
        return fastPtr;
    }
    uint8_t* ptr = nativeRuntimeHostPointer(g_ppssppRuntime, address, size);
    if (ptr)
    {
        return ptr;
    }
    uint32_t alias = canonicalGuestAddress(address);
    ptr = alias != address ? nativeRuntimeHostPointer(g_ppssppRuntime, alias, size) : NULL;
    if (ptr)
    {
        return ptr;
    }
    ptr = tryRuntimeRegionAliasPointer(address, size, 0x1fffffffu);
    return ptr ? ptr : tryRuntimeRegionAliasPointer(address, size, 0x03ffffffu);
}

static void syncPpssppStateToRuntime()
{
    if (!g_ppssppRuntime || !currentMIPS)
    {
        return;
    }

    uint32_t* gpr = nativeRuntimeGpr(g_ppssppRuntime);
    uint32_t* pc = nativeRuntimePc(g_ppssppRuntime);
    uint32_t* hi = nativeRuntimeHi(g_ppssppRuntime);
    uint32_t* lo = nativeRuntimeLo(g_ppssppRuntime);
    float* fpr = nativeRuntimeFpr(g_ppssppRuntime);
    float* vfpu = nativeRuntimeVfpu(g_ppssppRuntime);
    uint32_t* vfpuCtrl = nativeRuntimeVfpuCtrl(g_ppssppRuntime);
    uint32_t* fcr31 = nativeRuntimeFcr31(g_ppssppRuntime);
    uint32_t* fpcond = nativeRuntimeFpCond(g_ppssppRuntime);
    if (gpr)
    {
        memcpy(gpr, currentMIPS->r, sizeof(currentMIPS->r));
        gpr[0] = 0;
    }
    if (fpr)
    {
        memcpy(fpr, currentMIPS->f, sizeof(currentMIPS->f));
    }
    if (vfpu)
    {
        memcpy(vfpu, currentMIPS->v, sizeof(currentMIPS->v));
    }
    if (vfpuCtrl)
    {
        memcpy(vfpuCtrl, currentMIPS->vfpuCtrl, sizeof(currentMIPS->vfpuCtrl));
    }
    if (pc)
    {
        *pc = currentMIPS->pc;
    }
    if (hi)
    {
        *hi = currentMIPS->hi;
    }
    if (lo)
    {
        *lo = currentMIPS->lo;
    }
    if (fcr31)
    {
        *fcr31 = currentMIPS->fcr31;
    }
    if (fpcond)
    {
        *fpcond = currentMIPS->fpcond;
    }
}

static void syncRuntimeStateToPpsspp()
{
    if (!g_ppssppRuntime || !currentMIPS)
    {
        return;
    }

    uint32_t* gpr = nativeRuntimeGpr(g_ppssppRuntime);
    uint32_t* pc = nativeRuntimePc(g_ppssppRuntime);
    uint32_t* hi = nativeRuntimeHi(g_ppssppRuntime);
    uint32_t* lo = nativeRuntimeLo(g_ppssppRuntime);
    float* fpr = nativeRuntimeFpr(g_ppssppRuntime);
    float* vfpu = nativeRuntimeVfpu(g_ppssppRuntime);
    uint32_t* vfpuCtrl = nativeRuntimeVfpuCtrl(g_ppssppRuntime);
    uint32_t* fcr31 = nativeRuntimeFcr31(g_ppssppRuntime);
    uint32_t* fpcond = nativeRuntimeFpCond(g_ppssppRuntime);
    if (gpr)
    {
        memcpy(currentMIPS->r, gpr, sizeof(currentMIPS->r));
        currentMIPS->r[0] = 0;
    }
    if (fpr)
    {
        memcpy(currentMIPS->f, fpr, sizeof(currentMIPS->f));
    }
    if (vfpu)
    {
        memcpy(currentMIPS->v, vfpu, sizeof(currentMIPS->v));
    }
    if (vfpuCtrl)
    {
        memcpy(currentMIPS->vfpuCtrl, vfpuCtrl, sizeof(currentMIPS->vfpuCtrl));
    }
    if (pc)
    {
        currentMIPS->pc = *pc;
    }
    if (hi)
    {
        currentMIPS->hi = *hi;
    }
    if (lo)
    {
        currentMIPS->lo = *lo;
    }
    if (fcr31)
    {
        currentMIPS->fcr31 = *fcr31;
    }
    if (fpcond)
    {
        currentMIPS->fpcond = *fpcond;
    }
}

void ppssppShimSyncStateToRuntime(NativeRuntime* runtime)
{
    if (runtime && runtime == g_ppssppRuntime)
    {
        syncPpssppStateToRuntime();
    }
}

void ppssppShimSyncStateFromRuntime(NativeRuntime* runtime)
{
    if (runtime && runtime == g_ppssppRuntime)
    {
        syncRuntimeStateToPpsspp();
    }
}

void ppssppShimClearJitCache(NativeRuntime* runtime)
{
    if (runtime && runtime == g_ppssppRuntime && MIPSComp::jit)
    {
        MIPSComp::jit->ClearCache();
        clearEmuHackOriginals();
    }
}

static bool addressMatchesAny(uint32_t address, std::initializer_list<uint32_t> values)
{
    for (uint32_t value : values)
    {
        if (value && address == value)
        {
            return true;
        }
    }
    return false;
}

static bool traceKbdCallersEnabled()
{
    static int enabled = -1;
    if (enabled < 0)
    {
        const char* value = getenv("DINGOO_PIE_TRACE_KBD_CALLERS");
        enabled = (value && value[0] && value[0] != '0') ? 1 : 0;
    }
    return enabled != 0;
}

static bool writeFastKeyStatus(uint32_t address)
{
    KEY_STATUS status = {};
    _kbd_get_status(&status);

    uint8_t* out = hostPointerCanonical(address, sizeof(uint32_t) * 3);
    if (!out)
    {
        return false;
    }

    storeLe32(out, (uint32_t)status.pressed);
    storeLe32(out + 4, (uint32_t)status.released);
    storeLe32(out + 8, (uint32_t)status.status);
    if (traceKbdCallersEnabled() && (status.pressed || status.released || status.status))
    {
        printf("trace-kbd-fast: pc=0x%08x ra=0x%08x sp=0x%08x ks=0x%08x pressed=0x%08lx released=0x%08lx status=0x%08lx app=%s\n",
            currentMIPS ? currentMIPS->pc : 0,
            currentMIPS ? currentMIPS->r[MIPS_REG_RA] : 0,
            currentMIPS ? currentMIPS->r[MIPS_REG_SP] : 0,
            address,
            (unsigned long)status.pressed,
            (unsigned long)status.released,
            (unsigned long)status.status,
            bridge_get_app_identity());
    }
    return true;
}

static bool finishFastHle(uint32_t ret)
{
    g_ppssppFastHleCalls++;
    currentMIPS->r[MIPS_REG_V0] = ret;
    currentMIPS->pc = currentMIPS->r[MIPS_REG_RA];
    return true;
}

static bool tryRunFastHle(uint32_t address)
{
    if (!currentMIPS)
    {
        return false;
    }
    if (!g_fastHleAddresses.initialized ||
        (!g_fastHleAddresses.mallocFn && !g_fastHleAddresses.fsysFread &&
            !g_fastHleAddresses.lcdSetFrame && !g_fastHleAddresses.kbdGetStatus) ||
        shouldRetryOptionalFastHleAddresses())
    {
        refreshFastHleAddresses();
    }

    uint32_t ret = 0;
    if (address == g_fastHleAddresses.mallocFn)
    {
        uint32_t len = currentMIPS->r[MIPS_REG_A0];
        if (len >= 0x02000000)
        {
            printf("ppsspp-fast-hle: large malloc len=0x%08x pc=0x%08x ra=0x%08x a1=0x%08x a2=0x%08x a3=0x%08x v0=0x%08x last_hle=\"%s\"\n",
                len,
                currentMIPS->pc,
                currentMIPS->r[MIPS_REG_RA],
                currentMIPS->r[MIPS_REG_A1],
                currentMIPS->r[MIPS_REG_A2],
                currentMIPS->r[MIPS_REG_A3],
                currentMIPS->r[MIPS_REG_V0],
                bridge_get_last_hle_summary());
        }
        ret = vm_malloc(len);
    }
    else if (address == g_fastHleAddresses.freeFn)
    {
        vm_free(currentMIPS->r[MIPS_REG_A0]);
        ret = 0;
    }
    else if (address == g_fastHleAddresses.reallocFn)
    {
        ret = vm_realloc(currentMIPS->r[MIPS_REG_A0], currentMIPS->r[MIPS_REG_A1]);
    }
    else if (addressMatchesAny(address, {
        g_fastHleAddresses.lcdGetFrame,
        g_fastHleAddresses.lcdGetFrameLegacy,
        g_fastHleAddresses.lcdGetCFrame }))
    {
        ret = _lcd_get_frame();
    }
    else if (addressMatchesAny(address, {
        g_fastHleAddresses.lcdSetFrame,
        g_fastHleAddresses.lcdSetFrameLegacy,
        g_fastHleAddresses.apLcdSetFrame,
        g_fastHleAddresses.lcdFlip }))
    {
        g_ppssppFastLcdCalls++;
        requestFbUpdate();
        ret = 0;
    }
    else if (addressMatchesAny(address, {
        g_fastHleAddresses.kbdGetStatus,
        g_fastHleAddresses.kbdGetStatusLegacy }))
    {
        if (!writeFastKeyStatus(currentMIPS->r[MIPS_REG_A0]))
        {
            return false;
        }
        ret = 0;
    }
    else if (addressMatchesAny(address, {
        g_fastHleAddresses.kbdGetKey,
        g_fastHleAddresses.kbdGetKeyLegacy }))
    {
        ret = _kbd_get_key();
    }
    else if (addressMatchesAny(address, {
        g_fastHleAddresses.sysJudgeEvent,
        g_fastHleAddresses.sysJudgeEventLegacy }))
    {
        ret = inputHasPendingEvent();
    }
    else if (address == g_fastHleAddresses.fseekFn)
    {
        uint32_t streamPtr = currentMIPS->r[MIPS_REG_A0];
        uint8_t* fileBytes = hostPointerCanonical(streamPtr, sizeof(_file_t));
        _file_t file = {};
        if (!fileBytes)
        {
            return false;
        }
        memcpy(&file, fileBytes, sizeof(file));

        if (file.type == _file_type_file)
        {
            g_ppssppFastFseekCalls++;
            fsys_begin_fast_hle_call();
            if (!fsys_seek_cached(
                file.data,
                currentMIPS->r[MIPS_REG_A1],
                currentMIPS->r[MIPS_REG_A2],
                &ret))
            {
                ret = fsys_fseek(
                    file.data,
                    currentMIPS->r[MIPS_REG_A1],
                    currentMIPS->r[MIPS_REG_A2]);
            }
            fsys_end_fast_hle_call();
        }
        else if (file.type == _file_type_mem)
        {
            uint8_t* memBytes = hostPointerCanonical(file.data, sizeof(_file_mem_t));
            if (!memBytes)
            {
                return false;
            }

            _file_mem_t memFile = {};
            memcpy(&memFile, memBytes, sizeof(memFile));
            int64_t base = 0;
            uint32_t origin = currentMIPS->r[MIPS_REG_A2];
            if (origin == SEEK_SET)
            {
                base = 0;
            }
            else if (origin == SEEK_CUR)
            {
                base = memFile.offset;
            }
            else if (origin == SEEK_END)
            {
                base = memFile.size;
            }
            else
            {
                ret = (uint32_t)-1;
            }

            if (ret != (uint32_t)-1)
            {
                int64_t next = base + (int32_t)currentMIPS->r[MIPS_REG_A1];
                if (next < 0 || next > memFile.size)
                {
                    ret = (uint32_t)-1;
                }
                else
                {
                    memFile.offset = (uint32_t)next;
                    file.eof = memFile.offset >= memFile.size ? 1u : 0u;
                    memcpy(memBytes, &memFile, sizeof(memFile));
                    memcpy(fileBytes, &file, sizeof(file));
                    g_ppssppFastFseekCalls++;
                    ret = 0;
                }
            }
        }
        else
        {
            return false;
        }
    }
    else if (address == g_fastHleAddresses.fsysFseek)
    {
        g_ppssppFastFseekCalls++;
        fsys_begin_fast_hle_call();
        if (!fsys_seek_cached(
            currentMIPS->r[MIPS_REG_A0],
            currentMIPS->r[MIPS_REG_A1],
            currentMIPS->r[MIPS_REG_A2],
            &ret))
        {
            ret = fsys_fseek(
                currentMIPS->r[MIPS_REG_A0],
                currentMIPS->r[MIPS_REG_A1],
                currentMIPS->r[MIPS_REG_A2]);
        }
        fsys_end_fast_hle_call();
    }
    else if (address == g_fastHleAddresses.fsysFtell)
    {
        ret = fsys_ftell(currentMIPS->r[MIPS_REG_A0]);
    }
    else if (address == g_fastHleAddresses.fsysFeof)
    {
        ret = fsys_feof(currentMIPS->r[MIPS_REG_A0]);
    }
    else if (address == g_fastHleAddresses.freadFn)
    {
        uint32_t ptr = currentMIPS->r[MIPS_REG_A0];
        uint32_t size = currentMIPS->r[MIPS_REG_A1];
        uint32_t count = currentMIPS->r[MIPS_REG_A2];
        uint32_t stream = currentMIPS->r[MIPS_REG_A3];
        uint8_t* fileBytes = hostPointerCanonical(stream, sizeof(_file_t));
        _file_t file = {};
        if (!fileBytes)
        {
            return false;
        }
        memcpy(&file, fileBytes, sizeof(file));

        size_t bytes = (size_t)size * (size_t)count;
        if (size == 0 || count == 0)
        {
            ret = 0;
        }
        else if (bytes / size != count)
        {
            ret = (uint32_t)-1;
        }
        else
        {
            void* dst = hostPointerCanonical(ptr, bytes);
            if (!dst)
            {
                ret = (uint32_t)-1;
            }
            else if (file.type == _file_type_file)
            {
                fsys_begin_fast_hle_call();
                bool shouldRecordResourceLoad = runtimeResourceMonitorIsCapturing();
                uint32_t positionBefore = shouldRecordResourceLoad ?
                    fsys_stream_position(file.data) : 0;
                const uint8_t* cachedData = NULL;
                uint32_t cachedBytes = 0;
                uint32_t cachedItems = 0;
                if (fsys_read_cached(file.data, size, count, &cachedData, &cachedBytes, &cachedItems))
                {
                    if (cachedBytes > 0)
                    {
                        memcpy(dst, cachedData, cachedBytes);
                    }
                    ret = cachedItems;
                }
                else
                {
                    ret = vm_fread(dst, size, count, file.data);
                }
                fsys_end_fast_hle_call();
                if (ret != (uint32_t)-1)
                {
                    if (shouldRecordResourceLoad)
                    {
                        fsys_record_load_to_guest(file.data, ptr, dst, positionBefore);
                    }
                    g_ppssppFastFreadCalls++;
                    g_ppssppFastFreadBytes += (uint64_t)ret * (uint64_t)size;
                }
            }
            else if (file.type == _file_type_mem)
            {
                uint8_t* memBytes = hostPointerCanonical(file.data, sizeof(_file_mem_t));
                if (!memBytes)
                {
                    return false;
                }

                _file_mem_t memFile = {};
                memcpy(&memFile, memBytes, sizeof(memFile));
                if (!memFile.read)
                {
                    ret = (uint32_t)-1;
                }
                else
                {
                    uint32_t available = memFile.offset < memFile.size ? memFile.size - memFile.offset : 0;
                    uint32_t bytesToRead = bytes < available ? (uint32_t)bytes : available;
                    void* src = hostPointerCanonical(memFile.base + memFile.offset, bytesToRead ? bytesToRead : 1);
                    if (!src)
                    {
                        ret = (uint32_t)-1;
                    }
                    else
                    {
                        if (bytesToRead > 0)
                        {
                            memcpy(dst, src, bytesToRead);
                            memFile.offset += bytesToRead;
                        }
                        file.eof = memFile.offset >= memFile.size ? 1u : 0u;
                        memcpy(memBytes, &memFile, sizeof(memFile));
                        memcpy(fileBytes, &file, sizeof(file));
                        ret = bytesToRead / size;
                        g_ppssppFastFreadCalls++;
                        g_ppssppFastFreadBytes += bytesToRead;
                    }
                }
            }
            else
            {
                return false;
            }
        }
    }
    else if (address == g_fastHleAddresses.fsysFread)
    {
        uint32_t ptr = currentMIPS->r[MIPS_REG_A0];
        uint32_t size = currentMIPS->r[MIPS_REG_A1];
        uint32_t count = currentMIPS->r[MIPS_REG_A2];
        uint32_t stream = currentMIPS->r[MIPS_REG_A3];
        size_t bytes = (size_t)size * (size_t)count;
        if (size != 0 && bytes / size != count)
        {
            ret = (uint32_t)-1;
        }
        else if (size == 0 || count == 0)
        {
            ret = 0;
        }
        else
        {
            void* dst = hostPointerCanonical(ptr, bytes);
            fsys_begin_fast_hle_call();
            bool shouldRecordResourceLoad = runtimeResourceMonitorIsCapturing();
            uint32_t positionBefore = shouldRecordResourceLoad ?
                fsys_stream_position(stream) : 0;
            const uint8_t* cachedData = NULL;
            uint32_t cachedBytes = 0;
            uint32_t cachedItems = 0;
            if (dst && fsys_read_cached(stream, size, count, &cachedData, &cachedBytes, &cachedItems))
            {
                if (cachedBytes > 0)
                {
                    memcpy(dst, cachedData, cachedBytes);
                }
                ret = cachedItems;
            }
            else
            {
                ret = dst ? vm_fread(dst, size, count, stream) : (uint32_t)-1;
            }
            fsys_end_fast_hle_call();
            if (dst && ret != (uint32_t)-1)
            {
                if (shouldRecordResourceLoad)
                {
                    fsys_record_load_to_guest(stream, ptr, dst, positionBefore);
                }
                g_ppssppFastFreadCalls++;
                g_ppssppFastFreadBytes += (uint64_t)ret * (uint64_t)size;
            }
        }
    }
    else if (address == g_fastHleAddresses.waveoutWrite)
    {
        if (!bridge_fast_waveout_write(
            currentMIPS->r[MIPS_REG_A0],
            currentMIPS->r[MIPS_REG_A1],
            currentMIPS->r[MIPS_REG_A2],
            &ret))
        {
            return false;
        }
        g_ppssppFastAudioCalls++;
    }
    else if (address == g_fastHleAddresses.waveoutCanWrite)
    {
        ret = bridge_fast_waveout_can_write();
        g_ppssppFastAudioCalls++;
    }
    else if (address == g_fastHleAddresses.osSemPend)
    {
        syncPpssppStateToRuntime();
        bool interrupted = false;
        if (!bridge_fast_os_sem_pend(
            currentMIPS->r[MIPS_REG_A0],
            currentMIPS->r[MIPS_REG_A1],
            currentMIPS->r[MIPS_REG_A2],
            g_ppssppRuntime,
            &interrupted))
        {
            if (interrupted)
            {
                syncRuntimeStateToPpsspp();
                return true;
            }
            return false;
        }
        syncRuntimeStateToPpsspp();
        g_ppssppFastSemCalls++;
        ret = 0;
    }
    else if (address == g_fastHleAddresses.osSemPost)
    {
        if (!bridge_fast_os_sem_post(currentMIPS->r[MIPS_REG_A0], &ret))
        {
            return false;
        }
        g_ppssppFastSemCalls++;
    }
    else
    {
        return false;
    }

    return finishFastHle(ret);
}

static void initVfpuOrder()
{
    static bool initialized = false;
    if (initialized)
    {
        return;
    }

    int index = 0;
    for (int m = 0; m < 8; ++m)
    {
        for (int y = 0; y < 4; ++y)
        {
            for (int x = 0; x < 4; ++x)
            {
                voffset[m * 4 + x * 32 + y] = (u8)index++;
            }
        }
    }

    for (int i = 0; i < 128; ++i)
    {
        fromvoffset[voffset[i]] = (u8)i;
    }
    initialized = true;
}

void ppssppShimAttachRuntime(NativeRuntime* runtime)
{
    clearEmuHackOriginals();
    g_ppssppRuntime = runtime;
    g_runtimePauseRequested.store(false, std::memory_order_release);
    g_fastFramebufferDirectEnabled = ppssppShimParseEnabledEnv("DINGOO_PIE_IRJIT_FASTMEM_FB", true);
    rebuildFastMemoryRegions(runtime);
    initVfpuOrder();

    // Keep PPSSPP's PSP fast-memory mode disabled. Dingoo fast pages are
    // provided through ppssppShimFastPageBases() and project patches instead.
    g_Config.bFastMemory = false;
    const uint32_t defaultDisableFlags = 0;
    g_Config.uJitDisableFlags = ppssppShimParseJitDisableFlags(defaultDisableFlags);
    ppssppShimApplyRuntimeSettings();
    if (irjitTraceEnabled())
    {
        bool throttleEnabled = g_irjitThrottleEnabled.load();
        double speedScale = g_runtimeSpeedScale.load();
        uint32_t aheadLimitUs = g_irjitThrottleAheadLimitUs.load();
        uint32_t maxLagUs = g_irjitThrottleMaxLagUs.load();
        printf("ppsspp-irjit: disable_flags=0x%08x throttle=%u speed_scale=%.3f clock_hz=%d slice=%d ahead_ms=%u max_lag_ms=%u\n",
            g_Config.uJitDisableFlags,
            throttleEnabled ? 1u : 0u,
            speedScale,
            CPU_HZ,
            CoreTiming::slicelength,
            aheadLimitUs / 1000u,
            maxLagUs / 1000u);
    }
    g_Config.bFuncReplacements = false;
    g_Config.bIgnoreBadMemAccess = true;

    g_CoreParameter.cpuCore = CPUCore::JIT_IR;
    currentMIPS = &mipsr4k;
    ppssppShimApplyCpuClockSettings();
    coreState = CORE_RUNNING_CPU;
    coreStatePending = false;
    g_runtimeStopRequested.store(false, std::memory_order_release);
    g_fastPageDirectEnabled = g_fastPageDirectOverride >= 0 ?
        (g_fastPageDirectOverride != 0) :
        (getenv("DINGOO_PIE_IRJIT_FASTMEM") == NULL ||
            strcmp(getenv("DINGOO_PIE_IRJIT_FASTMEM"), "0") != 0);
    g_runtimeBeginTicks = 0;
    g_runtimeMaxTicks = 0;
    g_ppssppLastProfileTicks = 0;
    g_ppssppHookCalls = 0;
    g_ppssppFastHleCalls = 0;
    g_ppssppAdvanceCalls = 0;
    g_ppssppReads = 0;
    g_ppssppWrites = 0;
    g_ppssppThrottleSleepMs = 0;
    g_ppssppFastLcdCalls = 0;
    g_ppssppFastFreadCalls = 0;
    g_ppssppFastFreadBytes = 0;
    g_ppssppFastFseekCalls = 0;
    g_ppssppFastAudioCalls = 0;
    g_ppssppFastSemCalls = 0;
    g_ppssppLastProfileCoreTicks = CoreTiming::GetTicks();
    memset(&g_fastHleAddresses, 0x00, sizeof(g_fastHleAddresses));
}

void ppssppShimSetFastMemoryOverride(int enabled)
{
    g_fastPageDirectOverride = enabled < 0 ? -1 : (enabled ? 1 : 0);
}

void ppssppShimDetachRuntime(NativeRuntime* runtime)
{
    if (g_ppssppRuntime == runtime)
    {
        g_ppssppRuntime = NULL;
    }
    clearEmuHackOriginals();
    g_fastRegions.clear();
    g_lastFastRegion = (size_t)-1;
    g_runtimeBeginTicks = 0;
    g_runtimeMaxTicks = 0;
    g_runtimePauseRequested.store(false, std::memory_order_release);
    g_throttleStartTicks.store(0);
    g_throttleStartUs.store(0);
    memset(&g_fastHleAddresses, 0x00, sizeof(g_fastHleAddresses));
}

void ppssppShimSetRuntimeLimit(uint64_t beginTicks, uint64_t maxTicks)
{
    g_runtimeBeginTicks = beginTicks;
    g_runtimeMaxTicks = maxTicks;
}

void ppssppShimRequestStop(void)
{
    g_runtimeStopRequested.store(true, std::memory_order_release);
    g_runtimePauseRequested.store(false, std::memory_order_release);
    coreState = CORE_POWERDOWN;
    coreStatePending = true;
    if (currentMIPS)
    {
        currentMIPS->downcount = -1;
    }
}

void ppssppShimRequestPause(NativeRuntime* runtime)
{
    if (runtime && runtime == g_ppssppRuntime && currentMIPS)
    {
        g_runtimePauseRequested.store(true, std::memory_order_release);
        coreState = CORE_STEPPING_CPU;
        coreStatePending = true;
        currentMIPS->downcount = -1;
    }
}

bool ppssppShimWaitForPauseResume(NativeRuntime* runtime)
{
    if (!runtime || runtime != g_ppssppRuntime ||
        !g_runtimePauseRequested.exchange(false, std::memory_order_acq_rel))
    {
        return false;
    }

    syncPpssppStateToRuntime();
    uint32_t restoreGeneration = pauseGateRestoreGeneration();
    if (pauseGateWaitForResume() &&
        restoreGeneration != pauseGateRestoreGeneration())
    {
        syncRuntimeStateToPpsspp();
    }
    if (g_runtimeStopRequested.load(std::memory_order_acquire))
    {
        coreState = CORE_POWERDOWN;
        coreStatePending = true;
    }
    else
    {
        coreState = CORE_RUNNING_CPU;
        coreStatePending = false;
    }
    return true;
}

uint32_t ppssppShimRunCodeHook(uint32_t address)
{
    if (!g_ppssppRuntime || !currentMIPS)
    {
        return address;
    }

    uint32_t currentAddress = address;
    for (uint32_t pass = 0; pass < 4; ++pass)
    {
        {
            uint32_t restoreGeneration = pauseGateRestoreGeneration();
            if (pauseGateWaitForResume() &&
                restoreGeneration != pauseGateRestoreGeneration())
            {
                syncRuntimeStateToPpsspp();
                if (currentMIPS->pc != currentAddress)
                {
                    currentAddress = currentMIPS->pc;
                    continue;
                }
            }
        }

        bool hasHook = nativeRuntimeHasCodeHook(g_ppssppRuntime, currentAddress);
        if (irjitTraceEnabled() && g_irjitDispatchTraceCount < 128)
        {
            printf("irjit-dispatch: pc=0x%08x hook=%u ra=0x%08x sp=0x%08x downcount=%d\n",
                currentAddress,
                hasHook ? 1u : 0u,
                currentMIPS->r[MIPS_REG_RA],
                currentMIPS->r[MIPS_REG_SP],
                currentMIPS->downcount);
            g_irjitDispatchTraceCount++;
        }

        if (!hasHook)
        {
            return currentAddress;
        }

        // Synchronize only when a generated block reaches an HLE/compat hook.
        // If save-state restore moves PC to another hook, process that hook
        // before returning to the dispatcher so SDK import stubs are not
        // executed as ordinary guest instructions.
        g_ppssppHookCalls++;
        ppssppShimProfileTick();
        {
            uint32_t restoreGeneration = pauseGateRestoreGeneration();
            if (pauseGateWaitForResume() &&
                restoreGeneration != pauseGateRestoreGeneration())
            {
                syncRuntimeStateToPpsspp();
                if (currentMIPS->pc != currentAddress)
                {
                    currentAddress = currentMIPS->pc;
                    continue;
                }
            }
        }

        uint32_t fastReturnValue = 0;
        if (bridge_try_fast_return_hook(currentAddress, &fastReturnValue))
        {
            if (irjitTraceEnabled() && g_irjitHookTraceCount < 128)
            {
                printf("irjit-hook: fast-return pc=0x%08x ret=0x%08x ra=0x%08x\n",
                    currentAddress, fastReturnValue, currentMIPS->r[MIPS_REG_RA]);
                g_irjitHookTraceCount++;
            }
            currentMIPS->r[MIPS_REG_V0] = fastReturnValue;
            currentMIPS->pc = currentMIPS->r[MIPS_REG_RA];
            return currentMIPS->pc;
        }

        if (tryRunFastHle(currentAddress))
        {
            if (irjitTraceEnabled() && g_irjitHookTraceCount < 128)
            {
                printf("irjit-hook: fast-hle pc=0x%08x next=0x%08x v0=0x%08x ra=0x%08x\n",
                    currentAddress, currentMIPS->pc, currentMIPS->r[MIPS_REG_V0], currentMIPS->r[MIPS_REG_RA]);
                g_irjitHookTraceCount++;
            }
            return currentMIPS->pc;
        }

        currentMIPS->pc = currentAddress;
        syncPpssppStateToRuntime();
        uint32_t* runtimePc = nativeRuntimePc(g_ppssppRuntime);
        if (runtimePc)
        {
            *runtimePc = currentAddress;
        }

        nativeRuntimeCallCodeHooks(g_ppssppRuntime, currentAddress);
        syncRuntimeStateToPpsspp();

        uint32_t after = currentAddress;
        if (runtimePc)
        {
            after = *runtimePc;
        }
        currentMIPS->pc = after;
        if (irjitTraceEnabled() && g_irjitHookTraceCount < 128)
        {
            printf("irjit-hook: native pc=0x%08x next=0x%08x v0=0x%08x ra=0x%08x\n",
                currentAddress, after, currentMIPS->r[MIPS_REG_V0], currentMIPS->r[MIPS_REG_RA]);
            g_irjitHookTraceCount++;
        }
        return after;
    }

    return currentMIPS->pc;
}

void ppssppShimTraceJitExit(uint32_t pc)
{
    if (!irjitTraceEnabled() || !currentMIPS || g_irjitDispatchTraceCount >= 128)
    {
        return;
    }

    printf("irjit-exit: target=0x%08x state_pc=0x%08x ra=0x%08x sp=0x%08x downcount=%d\n",
        pc,
        currentMIPS->pc,
        currentMIPS->r[MIPS_REG_RA],
        currentMIPS->r[MIPS_REG_SP],
        currentMIPS->downcount);
}

void ppssppShimInterpretFallback(uint32_t op)
{
    MIPSOpcode opcode(op);
    MIPSInterpretFunc func = MIPSGetInterpretFunc(opcode);
    if (func)
    {
        func(opcode);
        return;
    }

    if (!currentMIPS)
    {
        coreState = CORE_RUNTIME_ERROR;
        return;
    }

    // Dingoo games can execute kernel-mode COP0 setup instructions during
    // startup. They do not affect the user-mode SDK surface emulated here, but
    // PPSSPP's tables leave their interpreter handlers null.
    if (isDingooSafeNullInterpreterOp(op))
    {
        if (((op >> 26) == 0x10) && (((op >> 21) & 0x1f) == 0x00))
        {
            uint32_t rt = (op >> 16) & 0x1f;
            if (rt != 0)
            {
                currentMIPS->r[rt] = 0;
            }
        }
        if (irjitTraceEnabled())
        {
            printf("ppsspp-irjit: no-op unsupported %s op=0x%08x pc=0x%08x\n",
                describeCop0Fallback(op), op, currentMIPS->pc);
        }
        currentMIPS->r[0] = 0;
        currentMIPS->pc += 4;
        return;
    }

    printf("ppsspp-irjit: unsupported fallback op=0x%08x pc=0x%08x\n", op, currentMIPS->pc);
    coreState = CORE_RUNTIME_ERROR;
}

uint32_t ppssppShimRead8(uint32_t address)
{
    g_ppssppReads++;
    uint8_t* ptr = findFastMemoryPointer(address, 1);
    if (ptr)
    {
        return *ptr;
    }

    uint8_t value = 0;
    if (!readRawCanonical(address, &value, sizeof(value)))
    {
        Core_MemoryException(address, 1, currentMIPS ? currentMIPS->pc : 0, MemoryExceptionType::READ_WORD);
        return 0;
    }
    return value;
}

uint32_t ppssppShimRead16(uint32_t address)
{
    g_ppssppReads++;
    uint8_t* ptr = findFastMemoryPointer(address, 2);
    if (ptr)
    {
        return loadFastLe16(ptr);
    }

    uint8_t bytes[2] = {};
    if (!readRawCanonical(address, bytes, sizeof(bytes)))
    {
        Core_MemoryException(address, 2, currentMIPS ? currentMIPS->pc : 0, MemoryExceptionType::READ_WORD);
        return 0;
    }
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8);
}

uint32_t ppssppShimRead32(uint32_t address)
{
    g_ppssppReads++;
    uint8_t* ptr = findFastMemoryPointer(address, 4);
    if (ptr)
    {
        return loadFastLe32(ptr);
    }

    uint8_t bytes[4] = {};
    if (!readRawCanonical(address, bytes, sizeof(bytes)))
    {
        Core_MemoryException(address, 4, currentMIPS ? currentMIPS->pc : 0, MemoryExceptionType::READ_WORD);
        return 0;
    }
    return loadLe32(bytes);
}

uint64_t ppssppShimRead64(uint32_t address)
{
    g_ppssppReads++;
    uint8_t* ptr = findFastMemoryPointer(address, 8);
    if (ptr)
    {
        return loadFastLe64(ptr);
    }

    uint8_t bytes[8] = {};
    if (!readRawCanonical(address, bytes, sizeof(bytes)))
    {
        Core_MemoryException(address, 8, currentMIPS ? currentMIPS->pc : 0, MemoryExceptionType::READ_WORD);
        return 0;
    }
    return (uint64_t)loadLe32(bytes) | ((uint64_t)loadLe32(bytes + 4) << 32);
}

uint32_t ppssppShimLoad32Left(uint32_t address, uint32_t currentValue)
{
    uint32_t alignedAddress = address & 0xfffffffcu;
    uint32_t shift = (address & 3u) * 8u;
    uint32_t mem = ppssppShimRead32(alignedAddress);
    uint32_t destMask = 0x00ffffffu >> shift;
    return (currentValue & destMask) | (mem << (24u - shift));
}

uint32_t ppssppShimLoad32Right(uint32_t address, uint32_t currentValue)
{
    uint32_t alignedAddress = address & 0xfffffffcu;
    uint32_t shift = (address & 3u) * 8u;
    uint32_t mem = ppssppShimRead32(alignedAddress);
    uint32_t destMask = 0xffffff00u << (24u - shift);
    return (currentValue & destMask) | (mem >> shift);
}

void ppssppShimWrite8(uint32_t address, uint32_t value)
{
    g_ppssppWrites++;
    uint8_t* ptr = findFastMemoryPointer(address, 1);
    if (ptr)
    {
        *ptr = (uint8_t)value;
        notifyRuntimeWrite(address, 1, value & 0xffu);
        trackFramebufferWrite(address, 1);
        return;
    }

    uint8_t byte = (uint8_t)value;
    if (!writeRawCanonical(address, &byte, sizeof(byte)))
    {
        Core_MemoryException(address, 1, currentMIPS ? currentMIPS->pc : 0, MemoryExceptionType::WRITE_WORD);
    }
    else
    {
        notifyRuntimeWrite(address, 1, value & 0xffu);
        trackFramebufferWrite(address, sizeof(byte));
    }
}

void ppssppShimWrite16(uint32_t address, uint32_t value)
{
    g_ppssppWrites++;
    uint8_t* ptr = findFastMemoryPointer(address, 2);
    if (ptr)
    {
        storeFastLe16(ptr, value);
        notifyRuntimeWrite(address, 2, value & 0xffffu);
        trackFramebufferWrite(address, 2);
        return;
    }

    uint8_t bytes[2];
    bytes[0] = (uint8_t)(value & 0xff);
    bytes[1] = (uint8_t)((value >> 8) & 0xff);
    if (!writeRawCanonical(address, bytes, sizeof(bytes)))
    {
        Core_MemoryException(address, 2, currentMIPS ? currentMIPS->pc : 0, MemoryExceptionType::WRITE_WORD);
    }
    else
    {
        notifyRuntimeWrite(address, 2, value & 0xffffu);
        trackFramebufferWrite(address, sizeof(bytes));
    }
}

void ppssppShimWrite32(uint32_t address, uint32_t value)
{
    g_ppssppWrites++;
    uint8_t* ptr = findFastMemoryPointer(address, 4);
    if (ptr)
    {
        rememberEmuHackOriginal(address, value);
        storeLe32(ptr, value);
        notifyRuntimeWrite(address, 4, value);
        trackFramebufferWrite(address, 4);
        return;
    }

    uint8_t bytes[4];
    rememberEmuHackOriginal(address, value);
    storeLe32(bytes, value);
    if (!writeRawCanonical(address, bytes, sizeof(bytes)))
    {
        Core_MemoryException(address, 4, currentMIPS ? currentMIPS->pc : 0, MemoryExceptionType::WRITE_WORD);
    }
    else
    {
        notifyRuntimeWrite(address, 4, value);
        trackFramebufferWrite(address, sizeof(bytes));
    }
}

void ppssppShimStore32Left(uint32_t address, uint32_t value)
{
    uint32_t alignedAddress = address & 0xfffffffcu;
    uint32_t shift = (address & 3u) * 8u;
    uint32_t mem = ppssppShimRead32(alignedAddress);
    uint32_t memMask = 0xffffff00u << shift;
    uint32_t result = (value >> (24u - shift)) | (mem & memMask);
    ppssppShimWrite32(alignedAddress, result);
}

void ppssppShimStore32Right(uint32_t address, uint32_t value)
{
    uint32_t alignedAddress = address & 0xfffffffcu;
    uint32_t shift = (address & 3u) * 8u;
    uint32_t mem = ppssppShimRead32(alignedAddress);
    uint32_t memMask = 0x00ffffffu >> (24u - shift);
    uint32_t result = (value << shift) | (mem & memMask);
    ppssppShimWrite32(alignedAddress, result);
}

void ppssppShimWrite64(uint32_t address, uint64_t value)
{
    g_ppssppWrites++;
    uint8_t* ptr = findFastMemoryPointer(address, 8);
    if (ptr)
    {
        storeFastLe64(ptr, value);
        notifyRuntimeWrite(address, 8, (int64_t)value);
        trackFramebufferWrite(address, 8);
        return;
    }

    uint8_t bytes[8];
    storeLe32(bytes, (uint32_t)value);
    storeLe32(bytes + 4, (uint32_t)(value >> 32));
    if (!writeRawCanonical(address, bytes, sizeof(bytes)))
    {
        Core_MemoryException(address, 8, currentMIPS ? currentMIPS->pc : 0, MemoryExceptionType::WRITE_WORD);
    }
    else
    {
        notifyRuntimeWrite(address, 8, (int64_t)value);
        trackFramebufferWrite(address, sizeof(bytes));
    }
}

void ppssppShimReadBlock(uint32_t address, void* out, uint32_t size)
{
    if (!out || !readRawCanonical(address, out, size))
    {
        if (out && size)
        {
            memset(out, 0, size);
        }
        Core_MemoryException(address, size, currentMIPS ? currentMIPS->pc : 0, MemoryExceptionType::READ_BLOCK);
    }
}

void ppssppShimWriteBlock(uint32_t address, const void* in, uint32_t size)
{
    if (!in || !writeRawCanonical(address, in, size))
    {
        Core_MemoryException(address, size, currentMIPS ? currentMIPS->pc : 0, MemoryExceptionType::WRITE_BLOCK);
    }
    else
    {
        g_ppssppWrites++;
        notifyRuntimeWrite(address, (int)size, 0);
        trackFramebufferWrite(address, size);
    }
}

uint8_t* ppssppShimGetPointer(uint32_t address, uint32_t size)
{
    return hostPointerCanonical(address, size ? size : 1);
}

uint32_t ppssppShimValidateAddress(uint32_t address, uint32_t alignment, uint32_t isWrite)
{
    if (address < 0x10000 && currentMIPS && irjitTraceEnabled())
    {
        printf("ppsspp-shim: bad-looking validate address=0x%08x align=%u write=%u pc=0x%08x a0=0x%08x a1=0x%08x a2=0x%08x a3=0x%08x v0=0x%08x v1=0x%08x\n",
            address, alignment, isWrite, currentMIPS->pc,
            currentMIPS->r[MIPS_REG_A0], currentMIPS->r[MIPS_REG_A1],
            currentMIPS->r[MIPS_REG_A2], currentMIPS->r[MIPS_REG_A3],
            currentMIPS->r[MIPS_REG_V0], currentMIPS->r[MIPS_REG_V1]);
    }

    if (alignment > 1 && (address & (alignment - 1)) != 0)
    {
        Core_MemoryException(address, alignment, currentMIPS ? currentMIPS->pc : 0, MemoryExceptionType::ALIGNMENT);
        return coreState != CORE_RUNNING_CPU ? 1u : 0u;
    }

    if (!ppssppShimIsValidRange(address, alignment))
    {
        MemoryExceptionType type = isWrite ? MemoryExceptionType::WRITE_WORD : MemoryExceptionType::READ_WORD;
        if (alignment > 4)
        {
            type = isWrite ? MemoryExceptionType::WRITE_BLOCK : MemoryExceptionType::READ_BLOCK;
        }
        Core_MemoryException(address, alignment, currentMIPS ? currentMIPS->pc : 0, type);
        return coreState != CORE_RUNNING_CPU ? 1u : 0u;
    }

    return 0;
}

uint32_t ppssppShimIsValidRange(uint32_t address, uint32_t size)
{
    return hostPointerCanonical(address, size) != NULL ? 1 : 0;
}

uint32_t ppssppShimIsValid4AlignedAddress(uint32_t address)
{
    return ((address & 3) == 0 && ppssppShimIsValidRange(address, 4)) ? 1 : 0;
}

uint32_t ppssppShimMaxSizeAtAddress(uint32_t address)
{
    uint32_t best = maxSizeInFastRegion(address);
    uint32_t direct = maxSizeInRuntimeRegions(address, 0xffffffffu);
    if (direct > best)
    {
        best = direct;
    }
    uint32_t alias29 = maxSizeInRuntimeRegions(address, 0x1fffffffu);
    if (alias29 > best)
    {
        best = alias29;
    }
    uint32_t alias26 = maxSizeInRuntimeRegions(address, 0x03ffffffu);
    return alias26 > best ? alias26 : best;
}

uintptr_t* ppssppShimFastPageBases(void)
{
    return g_fastPageDirectEnabled ? g_fastPageBases : NULL;
}

MIPSState::MIPSState()
{
    initVfpuOrder();
}

MIPSState::~MIPSState()
{
}

void MIPSState::Init()
{
    memset(r, 0, sizeof(r));
    memset(f, 0, sizeof(f));
    memset(v, 0, sizeof(v));
    memset(vfpuCtrl, 0, sizeof(vfpuCtrl));
    vfpuCtrl[VFPU_CTRL_SPREFIX] = 0xe4;
    vfpuCtrl[VFPU_CTRL_TPREFIX] = 0xe4;
    vfpuCtrl[VFPU_CTRL_DPREFIX] = 0;
    vfpuCtrl[VFPU_CTRL_CC] = 0x3f;
    vfpuCtrl[VFPU_CTRL_REV] = 0x7772ceab;
    pc = 0;
    hi = 0;
    lo = 0;
    fcr31 = 0;
    fpcond = 0;
    nextPC = 0;
    downcount = 100000;
    inDelaySlot = false;
    llBit = 0;
    debugCount = 0;
    currentMIPS = this;
}

void MIPSState::Shutdown()
{
}

void MIPSState::Reset()
{
    Init();
}

void MIPSState::UpdateCore(CPUCore)
{
}

void MIPSState::DoState(PointerWrap&)
{
}

void MIPSState::SingleStep()
{
}

int MIPSState::RunLoopUntil(u64)
{
    if (MIPSComp::jit)
    {
        MIPSComp::jit->RunLoopUntil(0);
    }
    return 1;
}

void MIPSState::ProcessPendingClears()
{
}

void MIPSState::InvalidateICache(u32 address, int length)
{
    if (MIPSComp::jit)
    {
        MIPSComp::jit->InvalidateCacheAt(address, length);
    }
}

void MIPSState::ClearJitCache()
{
    if (MIPSComp::jit)
    {
        MIPSComp::jit->ClearCache();
    }
}

bool MIPSState::HasDefaultPrefix() const
{
    return vfpuCtrl[VFPU_CTRL_SPREFIX] == 0xe4 && vfpuCtrl[VFPU_CTRL_TPREFIX] == 0xe4 && vfpuCtrl[VFPU_CTRL_DPREFIX] == 0;
}

Config::~Config()
{
}

void Config::Init()
{
}

bool DisplayLayoutConfig::InternalRotationIsPortrait() const
{
    return false;
}

bool DisplayLayoutConfig::ResetToDefault(std::string_view)
{
    return false;
}

bool TouchControlConfig::ResetToDefault(std::string_view)
{
    return false;
}

void TouchControlConfig::ResetLayout()
{
}

bool GestureControlConfig::ResetToDefault(std::string_view)
{
    return false;
}

void Compatibility::Clear()
{
    memset(&flags_, 0, sizeof(flags_));
    memset(&vrCompat_, 0, sizeof(vrCompat_));
    activeList_.clear();
    filesLoaded_.clear();
}

CPUInfo::CPUInfo()
{
    memset(this, 0, sizeof(*this));
    vendor = VENDOR_OTHER;
    OS64bit = true;
    CPU64bit = true;
    Mode64bit = true;
    num_cores = 1;
    logical_cpu_count = 1;
    bSSE = true;
    bSSE2 = true;
    bSSE3 = true;
    bSSSE3 = true;
    bSSE4_1 = true;
    bSSE4_2 = true;
}

std::vector<std::string> CPUInfo::Features()
{
    return std::vector<std::string>();
}

std::string CPUInfo::Summarize()
{
    return "shim cpu";
}

const char* GetCompilerABI()
{
    return "mingw64";
}

std::string GetLastErrorMsg()
{
    return std::string();
}

PointerWrapSection PointerWrap::Section(const char*, int, int ver)
{
    return PointerWrapSection(*this, ver, "");
}

PointerWrapSection::~PointerWrapSection()
{
}

void PointerWrap::SetError(Error error_)
{
    error = error_;
}

void PointerWrap::DoVoid(void* data, int size)
{
    if (mode == MODE_READ || mode == MODE_WRITE || mode == MODE_VERIFY)
    {
        if (ptr && *ptr && data && size > 0)
        {
            if (mode == MODE_READ)
            {
                memcpy(data, *ptr, size);
            }
            else if (mode == MODE_WRITE)
            {
                memcpy(*ptr, data, size);
            }
        }
    }
    if (ptr && *ptr)
    {
        *ptr += size;
    }
}

bool PointerWrap::ExpectVoid(void*, int)
{
    return true;
}

void PointerWrap::DoMarker(const char*, u32)
{
}

void PointerWrap::RewindForWrite(u8* writePtr)
{
    if (ptr)
    {
        *ptr = writePtr;
    }
}

bool PointerWrap::CheckAfterWrite()
{
    return true;
}

std::string SymbolMap::GetDescription(unsigned int)
{
    return std::string();
}

u32 SymbolMap::GetFunctionSize(u32)
{
    return 0;
}

namespace Memory
{
bool Init(MemMapSetupFlags)
{
    return true;
}

void Shutdown()
{
}

void DoState(PointerWrap&)
{
}

bool IsActive()
{
    return g_ppssppRuntime != NULL;
}

MemoryInitedLock::MemoryInitedLock()
{
}

MemoryInitedLock::~MemoryInitedLock()
{
}

MemoryInitedLock Lock()
{
    return MemoryInitedLock();
}

Opcode Read_Opcode_JIT(const u32 address)
{
    Opcode inst(ppssppShimRead32(address));
    uint32_t registeredOriginal = 0;
    if (MIPS_IS_RUNBLOCK(inst.encoding) &&
        ppssppShimResolveEmuHack(address, inst.encoding, &registeredOriginal))
    {
        if (irjitTraceEnabled())
        {
            static uint32_t runblockTraceCount = 0;
            if (runblockTraceCount < 64)
            {
                printf("irjit-trace: read-op runblock pc=0x%08x marker=0x%08x original=0x%08x\n",
                    address, inst.encoding, registeredOriginal);
                runblockTraceCount++;
            }
        }
        return Opcode(registeredOriginal);
    }
    if (irjitTraceEnabled())
    {
        static uint32_t opcodeTraceCount = 0;
        if (opcodeTraceCount < 256)
        {
            printf("irjit-trace: read-op pc=0x%08x op=0x%08x\n", address, inst.encoding);
            opcodeTraceCount++;
        }
    }
    return inst;
}

void Write_Opcode_JIT(const u32 address, const Opcode& value)
{
    ppssppShimWrite32(address, value.encoding);
}

Opcode Read_Instruction(const u32 address, bool)
{
    return Read_Opcode_JIT(address);
}

Opcode ReadUnchecked_Instruction(const u32 address, bool)
{
    return Read_Opcode_JIT(address);
}

u8 Read_U8(const u32 address)
{
    return (u8)ppssppShimRead8(address);
}

u16 Read_U16(const u32 address)
{
    return (u16)ppssppShimRead16(address);
}

u32 Read_U32(const u32 address)
{
    return ppssppShimRead32(address);
}

u64 Read_U64(const u32 address)
{
    return ppssppShimRead64(address);
}

u32 Read_U8_ZX(const u32 address)
{
    return Read_U8(address);
}

u32 Read_U16_ZX(const u32 address)
{
    return Read_U16(address);
}

void Write_U8(const u8 data, const u32 address)
{
    ppssppShimWrite8(address, data);
}

void Write_U16(const u16 data, const u32 address)
{
    ppssppShimWrite16(address, data);
}

void Write_U32(const u32 data, const u32 address)
{
    ppssppShimWrite32(address, data);
}

void Write_U64(const u64 data, const u32 address)
{
    ppssppShimWrite64(address, data);
}

u8* GetPointerWrite(const u32 address)
{
    return hostPointerCanonical(address, 1);
}

const u8* GetPointer(const u32 address)
{
    return hostPointerCanonical(address, 1);
}

u8* GetPointerWriteRange(const u32 address, const u32 size)
{
    return hostPointerCanonical(address, size);
}

const u8* GetPointerRange(const u32 address, const u32 size)
{
    return hostPointerCanonical(address, size);
}

bool IsRAMAddress(const u32 address)
{
    return hostPointerCanonical(address, 1) != NULL;
}

bool IsScratchpadAddress(const u32 address)
{
    return (address & 0x1fffc000u) == 0x00010000u;
}

void Memcpy(void* to, const u32 from_address, const u32 len, const char*)
{
    ppssppShimReadBlock(from_address, to, len);
}

void Memcpy(const u32 to_address, const void* from, const u32 len, const char*)
{
    ppssppShimWriteBlock(to_address, from, len);
}

void Memset(const u32 address, const u8 value, const u32 len, const char*)
{
    u8* out = GetPointerWriteRange(address, len);
    if (out)
    {
        memset(out, value, len);
    }
}
}

namespace CoreTiming
{
static u64 g_ticks = 0;

void Init()
{
    g_ticks = 0;
}

void Shutdown()
{
}

u64 GetTicks()
{
    return g_ticks;
}

u64 GetIdleTicks()
{
    return 0;
}

u64 GetGlobalTimeUs()
{
    return ticksToMicroseconds(g_ticks, (uint32_t)CPU_HZ);
}

u64 GetGlobalTimeUsScaled()
{
    return GetGlobalTimeUs();
}

int RegisterEvent(const char*, TimedCallback)
{
    return 0;
}

void RestoreRegisterEvent(int& event_type, const char*, TimedCallback)
{
    event_type = 0;
}

void UnregisterAllEvents()
{
}

void ScheduleEvent(s64, int, u64)
{
}

s64 UnscheduleEvent(int, u64)
{
    return 0;
}

const std::vector<EventType>& GetEventTypes()
{
    static std::vector<EventType> eventTypes;
    return eventTypes;
}

const Event* GetFirstEvent()
{
    return NULL;
}

void RemoveEvent(int)
{
}

bool IsScheduled(int)
{
    return false;
}

void Advance()
{
    g_ppssppAdvanceCalls++;
    if (g_runtimeStopRequested.load(std::memory_order_acquire))
    {
        coreState = CORE_POWERDOWN;
    }
    uint32_t restoreGeneration = pauseGateRestoreGeneration();
    if (pauseGateWaitForResume() &&
        restoreGeneration != pauseGateRestoreGeneration())
    {
        syncRuntimeStateToPpsspp();
    }
    g_ticks += slicelength;
    if (currentMIPS)
    {
        currentMIPS->downcount = slicelength;
    }
    ppssppShimThrottleToGuestClock(g_ticks);
    ppssppShimProfileTick();
    if (g_runtimeMaxTicks && g_ticks - g_runtimeBeginTicks >= g_runtimeMaxTicks)
    {
        coreState = CORE_POWERDOWN;
    }
}

void ForceCheck()
{
    if (currentMIPS)
    {
        currentMIPS->downcount = -1;
    }
}

void Idle(int)
{
}

void ClearPendingEvents()
{
}

void LogPendingEvents()
{
}

std::string GetScheduledEventsSummary()
{
    return std::string();
}

void DoState(PointerWrap&)
{
}

bool SetClockFrequencyHz(int cpuHz)
{
    if (cpuHz <= 0)
    {
        return true;
    }
    if (cpuHz == CPU_HZ)
    {
        return false;
    }
    CPU_HZ = cpuHz;
    return true;
}

int GetClockFrequencyHz()
{
    return CPU_HZ;
}
}

void Core_MemoryException(u32 address, u32 accessSize, u32 pc, MemoryExceptionType type)
{
    if (ppssppShimLogEnabled())
    {
        printf("ppsspp-shim: memory exception type=%d address=0x%08x size=%u pc=0x%08x\n",
            (int)type, address, accessSize, pc);
    }
    coreState = CORE_RUNTIME_ERROR;
}

void Core_MemoryExceptionInfo(u32 address, u32 accessSize, u32 pc, MemoryExceptionType type, std::string_view, bool)
{
    Core_MemoryException(address, accessSize, pc, type);
}

void Core_ExecException(u32 address, u32 pc, ExecExceptionType type)
{
    if (ppssppShimLogEnabled())
    {
        printf("ppsspp-shim: exec exception type=%d address=0x%08x pc=0x%08x\n", (int)type, address, pc);
    }
    coreState = CORE_RUNTIME_ERROR;
}

void Core_BreakException(u32 pc)
{
    if (ppssppShimLogEnabled())
    {
        printf("ppsspp-shim: break exception pc=0x%08x\n", pc);
    }
    coreState = CORE_RUNTIME_ERROR;
}

void Core_ResetException()
{
}

void Core_UpdateState(CoreState newState)
{
    coreState = newState;
}

const char* CoreStateToString(CoreState state)
{
    switch (state)
    {
    case CORE_RUNNING_CPU: return "running";
    case CORE_NEXTFRAME: return "nextframe";
    case CORE_STEPPING_CPU: return "stepping";
    case CORE_POWERDOWN: return "powerdown";
    case CORE_RUNTIME_ERROR: return "runtime_error";
    case CORE_STEPPING_GE: return "stepping_ge";
    case CORE_RUNNING_GE: return "running_ge";
    default: return "unknown";
    }
}

bool Core_IsStepping()
{
    return coreState == CORE_STEPPING_CPU;
}

bool Core_IsActive()
{
    return coreState == CORE_RUNNING_CPU || coreState == CORE_NEXTFRAME;
}

bool Core_IsInactive()
{
    return !Core_IsActive();
}

void GenericLog(Log type, LogLevel level, const char*, int, const char* fmt, ...)
{
    (void)type;
    (void)level;
    if (!ppssppShimLogEnabled())
    {
        return;
    }
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}

bool HandleAssert(bool, const char*, const char*, int, const char* expression, const char* format, ...)
{
    if (!ppssppShimLogEnabled())
    {
        return true;
    }
    printf("ppsspp-shim: assert %s ", expression ? expression : "");
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
    return true;
}

bool HitAnyAsserts()
{
    return false;
}

void ResetHitAnyAsserts()
{
}

void SetExtraAssertInfo(const char*)
{
}

void SetDebugValue(DebugCounter, int)
{
}

void IncrementDebugCounter(DebugCounter)
{
}

void SetAssertCancelCallback(AssertNoCallbackFunc, void*)
{
}

void SetCleanExitOnAssert()
{
}

void BreakIntoPSPDebugger(const char*)
{
}

void SetAssertDialogParent(void*)
{
}

void OutputDebugStringUTF8(const char* p)
{
    if (p)
    {
        printf("%s", p);
    }
}

namespace Reporting
{
void Init() {}
void Shutdown() {}
void DoState(PointerWrap&) {}
void UpdateConfig() {}
void NotifyDebugger() {}
void NotifyExecModule(const char*, int, uint32_t) {}
bool IsEnabled() { return false; }
bool IsSupported() { return false; }
bool Enable(bool, const std::string&) { return false; }
void EnableDefault() {}
void ReportCompatibility(const char*, int, int, int, const std::string&) {}
std::vector<std::string> CompatibilitySuggestions() { return std::vector<std::string>(); }
void QueueCRC(const Path&) {}
bool HasCRC(const Path&) { return false; }
void CancelCRC() {}
uint32_t RetrieveCRC(const Path&) { return 0; }
ReportStatus GetStatus() { return ReportStatus::FAILING; }
std::string ServerHost() { return std::string(); }
std::string CurrentGameID() { return std::string(); }
void ResetCounts() {}
bool ShouldLogNTimes(const char*, int) { return true; }
void SetupCallbacks(AllowedCallback, MessageCallback) {}
void ReportMessage(const char*, ...) {}
void ReportMessageFormatted(const char*, const char*) {}
}

bool parseExpression(const DebugInterface*, PostfixExpression&, u32& dest)
{
    dest = 0;
    return false;
}

bool initExpression(const DebugInterface*, const char*, PostfixExpression&)
{
    return false;
}

void DisAsm(u32, char* out, size_t outSize)
{
    if (out && outSize)
    {
        snprintf(out, outSize, "unavailable");
    }
}

std::string MIPSDebugInterface::GetRegName(int cat, int index)
{
    static const char* const gprNames[32] = {
        "zr", "at", "v0", "v1", "a0", "a1", "a2", "a3",
        "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
        "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
        "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"
    };
    char temp[32];
    if (cat == 0 && index >= 0 && index < 32)
    {
        return gprNames[index];
    }
    snprintf(temp, sizeof(temp), cat == 1 ? "f%d" : "v%d", index);
    return temp;
}

bool MIPSDebugInterface::isAlive() { return true; }
bool MIPSDebugInterface::isBreakpoint(unsigned int) { return false; }
void MIPSDebugInterface::setBreakpoint(unsigned int) {}
void MIPSDebugInterface::clearBreakpoint(unsigned int) {}
void MIPSDebugInterface::clearAllBreakpoints() {}
void MIPSDebugInterface::toggleBreakpoint(unsigned int) {}
unsigned int MIPSDebugInterface::readMemory(unsigned int address) { return Memory::Read_U32(address); }
int MIPSDebugInterface::getColor(unsigned int, bool) const { return 0; }
std::string MIPSDebugInterface::getDescription(unsigned int address) { return std::string(); }

bool BreakpointManager::IsAddressBreakPoint(u32) { return false; }
bool BreakpointManager::IsAddressBreakPoint(u32, bool* enabled)
{
    if (enabled)
    {
        *enabled = false;
    }
    return false;
}
bool BreakpointManager::IsTempBreakPoint(u32) { return false; }
bool BreakpointManager::RangeContainsBreakPoint(u32, u32) { return false; }
int BreakpointManager::AddBreakPoint(u32, bool) { return -1; }
void BreakpointManager::RemoveBreakPoint(u32) {}
void BreakpointManager::ChangeBreakPoint(u32, bool) {}
void BreakpointManager::ChangeBreakPoint(u32, BreakAction) {}
void BreakpointManager::ClearAllBreakPoints() {}
void BreakpointManager::ClearTemporaryBreakPoints() {}
void BreakpointManager::ChangeBreakPointAddCond(u32, const BreakPointCond&) {}
void BreakpointManager::ChangeBreakPointRemoveCond(u32) {}
BreakPointCond* BreakpointManager::GetBreakPointCondition(u32) { return NULL; }
void BreakpointManager::ChangeBreakPointLogFormat(u32, const std::string&) {}
BreakAction BreakpointManager::ExecBreakPoint(u32) { return BREAK_ACTION_IGNORE; }
int BreakpointManager::AddMemCheck(u32, u32, MemCheckCondition, BreakAction) { return -1; }
void BreakpointManager::RemoveMemCheck(u32, u32) {}
void BreakpointManager::ChangeMemCheck(u32, u32, MemCheckCondition, BreakAction) {}
void BreakpointManager::ClearAllMemChecks() {}
void BreakpointManager::ChangeMemCheckAddCond(u32, u32, const BreakPointCond&) {}
void BreakpointManager::ChangeMemCheckRemoveCond(u32, u32) {}
BreakPointCond* BreakpointManager::GetMemCheckCondition(u32, u32) { return NULL; }
void BreakpointManager::ChangeMemCheckLogFormat(u32, u32, const std::string&) {}
bool BreakpointManager::GetMemCheck(u32, u32, MemCheck*) { return false; }
bool BreakpointManager::GetMemCheckInRange(u32, int, MemCheck*) { return false; }
BreakAction BreakpointManager::ExecMemCheck(u32, bool, int, u32, const char*) { return BREAK_ACTION_IGNORE; }
BreakAction BreakpointManager::ExecOpMemCheck(u32, u32) { return BREAK_ACTION_IGNORE; }
void BreakpointManager::SetSkipFirst(u32 pc) { breakSkipFirstAt_ = pc; }
u32 BreakpointManager::CheckSkipFirst() { return breakSkipFirstAt_; }
std::vector<MemCheck> BreakpointManager::GetMemCheckRanges(bool) { return std::vector<MemCheck>(); }
std::vector<MemCheck> BreakpointManager::GetMemChecks() { return std::vector<MemCheck>(); }
std::vector<BreakPoint> BreakpointManager::GetBreakpoints() { return std::vector<BreakPoint>(); }
void BreakpointManager::Frame() {}
bool BreakpointManager::ValidateLogFormat(MIPSDebugInterface*, const std::string&) { return false; }
bool BreakpointManager::EvaluateLogFormat(MIPSDebugInterface*, const std::string&, std::string&) { return false; }

namespace MIPSAnalyst
{
MIPSGPReg GetOutGPReg(MIPSOpcode op)
{
    MIPSInfo info = MIPSGetInfo(op);
    if (info & OUT_RT)
    {
        return MIPS_GET_RT(op);
    }
    if (info & OUT_RD)
    {
        return MIPS_GET_RD(op);
    }
    if (info & OUT_RA)
    {
        return MIPS_REG_RA;
    }
    return MIPS_REG_INVALID;
}

bool ReadsFromGPReg(MIPSOpcode op, MIPSGPReg reg)
{
    MIPSInfo info = MIPSGetInfo(op);
    if ((info & IN_RS) && MIPS_GET_RS(op) == reg)
    {
        return true;
    }
    if ((info & IN_RT) && MIPS_GET_RT(op) == reg)
    {
        return true;
    }
    return false;
}

bool IsDelaySlotNiceReg(MIPSOpcode branchOp, MIPSOpcode op, MIPSGPReg reg1, MIPSGPReg reg2)
{
    MIPSInfo branchInfo = MIPSGetInfo(branchOp);
    MIPSInfo info = MIPSGetInfo(op);
    if (info & IS_CONDBRANCH)
    {
        return false;
    }
    if (reg1 != MIPS_REG_ZERO && GetOutGPReg(op) == reg1)
    {
        return false;
    }
    if (reg2 != MIPS_REG_ZERO && GetOutGPReg(op) == reg2)
    {
        return false;
    }
    if ((branchInfo & OUT_RA) != 0)
    {
        return GetOutGPReg(op) != MIPS_REG_RA && !ReadsFromGPReg(op, MIPS_REG_RA);
    }
    return true;
}

bool IsDelaySlotNiceVFPU(MIPSOpcode, MIPSOpcode op)
{
    MIPSInfo info = MIPSGetInfo(op);
    return (info & (IS_CONDBRANCH | OUT_VFPU_CC)) == 0;
}

bool IsDelaySlotNiceFPU(MIPSOpcode, MIPSOpcode op)
{
    MIPSInfo info = MIPSGetInfo(op);
    return (info & (IS_CONDBRANCH | OUT_FPUFLAG)) == 0;
}

bool IsSyscall(MIPSOpcode op)
{
    return (op >> 26) == 0 && (op & 0x3f) == 12;
}

int OpMemoryAccessSize(u32)
{
    return 0;
}

bool IsOpMemoryWrite(u32)
{
    return false;
}
}

std::vector<std::string> DisassembleX86(const u8*, int)
{
    return std::vector<std::string>();
}

std::vector<std::string> DisassembleArm2(const u8*, int)
{
    return std::vector<std::string>();
}

std::vector<std::string> DisassembleArm64(const u8*, int)
{
    return std::vector<std::string>();
}

std::vector<std::string> DisassembleRV64(const u8*, int)
{
    return std::vector<std::string>();
}

std::vector<std::string> DisassembleLA64(const u8*, int)
{
    return std::vector<std::string>();
}

void Core_Break(BreakReason, u32)
{
    coreState = CORE_STEPPING_CPU;
}

const char* BreakReasonToString(BreakReason)
{
    return "none";
}

BreakReason Core_BreakReason()
{
    return BreakReason::None;
}

void Core_Resume()
{
    coreState = CORE_RUNNING_CPU;
}

bool Core_RequestCPUStep(CPUStepType, int)
{
    return false;
}

bool Core_NextFrame()
{
    coreState = CORE_NEXTFRAME;
    return true;
}

void Core_SwitchToGe() {}
int Core_GetSteppingCounter() { return 0; }
SteppingReason Core_GetSteppingReason() { return SteppingReason(); }
void Core_ListenLifecycle(CoreLifecycleFunc) {}
void Core_NotifyLifecycle(CoreLifecycle) {}
void Core_StateProcessed() {}
void Core_WaitInactive() {}
void Core_SetPowerSaving(bool) {}
bool Core_GetPowerSaving() { return false; }
void Core_RunLoopUntil(u64) {}
const MIPSExceptionInfo& Core_GetExceptionInfo()
{
    static MIPSExceptionInfo info{};
    return info;
}
const char* ExceptionTypeAsString(MIPSExceptionType) { return "none"; }
const char* MemoryExceptionTypeAsString(MemoryExceptionType) { return "none"; }
const char* ExecExceptionTypeAsString(ExecExceptionType) { return "none"; }

const char* GetHLEFuncName(std::string_view, u32)
{
    return "hle";
}

const char* GetHLEFuncName(int, int)
{
    return "hle";
}

void CallSyscall(MIPSOpcode)
{
    coreState = CORE_RUNTIME_ERROR;
}

const HLEFunction* GetSyscallFuncPointer(MIPSOpcode)
{
    return NULL;
}

void* GetQuickSyscallFunc(MIPSOpcode)
{
    return NULL;
}

int GetNumReplacementFuncs()
{
    return 0;
}

std::vector<int> GetReplacementFuncIndexes(u64, int)
{
    return std::vector<int>();
}

const ReplacementTableEntry* GetReplacementFunc(size_t)
{
    static ReplacementTableEntry entry = { "none", NULL, NULL, REPFLAG_DISABLED, 0 };
    return &entry;
}

void Replacement_Init() {}
void Replacement_Shutdown() {}
void WriteReplaceInstructions(u32, u64, int) {}
void RestoreReplacedInstruction(u32) {}
void RestoreReplacedInstructions(u32, u32) {}
bool GetReplacedOpAt(u32, u32*) { return false; }
std::map<u32, u32> SaveAndClearReplacements() { return std::map<u32, u32>(); }
void RestoreSavedReplacements(const std::map<u32, u32>&) {}

uint8_t* VFS::ReadFile(std::string_view, size_t* size)
{
    if (size)
    {
        *size = 0;
    }
    return NULL;
}

bool VFS::GetFileListing(std::string_view, std::vector<File::FileInfo>* listing, const char*)
{
    if (listing)
    {
        listing->clear();
    }
    return false;
}

void VFS::Register(std::string_view, VFSBackend*)
{
}

void VFS::Clear()
{
}

bool VFS::GetFileInfo(std::string_view, File::FileInfo* info)
{
    if (info)
    {
        *info = File::FileInfo();
    }
    return false;
}

bool VFS::Exists(std::string_view)
{
    return false;
}

namespace File
{
FILE* OpenCFile(const Path&, const char*)
{
    return NULL;
}
}

void TraceBlockStorage::initialize(u32)
{
}

void TraceBlockStorage::clear()
{
}

bool TraceBlockStorage::save_block(const u32*, u32)
{
    return false;
}

void MIPSTracer::start_tracing() {}
void MIPSTracer::stop_tracing() {}
void MIPSTracer::prepare_block(const MIPSComp::IRBlock*, MIPSComp::IRBlockCache&) {}
bool MIPSTracer::flush_to_file() { return false; }
void MIPSTracer::flush_block_to_file(const TraceBlockInfo&) {}
void MIPSTracer::initialize(u32, u32) {}
void MIPSTracer::clear() {}
void MIPSTracer::print_stats() const {}
