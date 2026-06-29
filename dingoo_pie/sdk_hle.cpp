#include "sdk_hle.h"
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "native_runtime.h"
#include "runtime_debug.h"
#include "app_loader.h"
#include "emulated_memory.h"
#include "input_controls.h"
#include "pthread.h"
#include "semaphore.h"
#include <assert.h>
#include "framebuffer.h"
#include "pause_gate.h"
#include "sdl_frontend.h"
#include "guest_filesystem.h"
#include "task_scheduler.h"
#include "guest_audio.h"
#include "guest_format.h"
#include "compat_profile.h"
#include <chrono>
#include <atomic>
#include <pthread.h>
#include <string>
#include <thread>
#include <vector>
#include <locale.h>
#include <cstdlib>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

static void returnToRa(NativeRuntime* runtime);
static app* s_bridgeApp = NULL;
static std::string s_bridgeAppSha256;
static char s_lastTaskStopSummary[192] = "";
static char s_lastHleSummary[192] = "";
static std::atomic<bool> s_bridgeProfileEnabled(false);
static std::atomic<double> s_runtimeSpeedScale(1.0);
static std::atomic<bool> s_runtimeSpeedScaleForced(false);
static std::atomic<double> s_hostDelayScale(1.0);
static const double kAutoRuntimeSpeedScale = 0.65;
static const double kAutoHostDelayScale = 1.0;
static const uint32_t kGuestExitPc = 0xFFFFFFFFu;

struct RuntimeBridgeContext
{
    NativeRuntime* runtime;
    bool isMainRuntime;
};

static std::vector<RuntimeBridgeContext> s_runtimeContexts;
static pthread_mutex_t s_runtimeContextsMutex = PTHREAD_MUTEX_INITIALIZER;
static void profilePrintAndReset(uint64_t now);

struct HleProfileCounters
{
    uint64_t lcdSetFrame;
    uint64_t lcdFlip;
    uint64_t osTimeGet;
    uint64_t getTickCount;
    uint64_t delayMs;
    uint64_t delayMsTotal;
    uint64_t udelay;
    uint64_t udelayTotal;
    uint64_t osTimeDly;
    uint64_t osTimeDlyTotal;
    uint64_t waveCanWrite;
    uint64_t waveWrite;
    uint64_t waveWriteBytes;
    uint64_t semPend;
    uint64_t semPost;
    uint64_t semCreate;
    uint64_t taskCreate;
    uint64_t sysJudgeEvent;
    uint64_t kbdStatus;
    uint64_t dlResOpen;
    uint64_t dlResRead;
    uint64_t dlResReadBytes;
};

static HleProfileCounters s_hleProfile = {};

void bridge_set_app_identity(const char* sha256Hex)
{
    s_bridgeAppSha256 = sha256Hex ? sha256Hex : "";
    bridge_apply_runtime_settings();
}

const char* bridge_get_app_identity(void)
{
    return s_bridgeAppSha256.c_str();
}

const char* bridge_get_last_task_stop_summary(void)
{
    return s_lastTaskStopSummary;
}

const char* bridge_get_last_hle_summary(void)
{
    return s_lastHleSummary;
}

bool bridge_try_fast_return_hook(uint32_t address, uint32_t* returnValue);
void bridge_profile_tick(void)
{
    if (s_bridgeProfileEnabled.load())
    {
        uint64_t now = SDL_GetTicks64();
        profilePrintAndReset(now);
    }
}

static void profilePrintAndReset(uint64_t now)
{
    static uint64_t lastTicks = 0;
    if (!s_bridgeProfileEnabled.load())
    {
        return;
    }
    if (!lastTicks)
    {
        lastTicks = now;
        return;
    }
    if (now - lastTicks < 1000)
    {
        return;
    }

    uint64_t fbWrites = consumeFramebufferWriteCount();
    uint64_t fbWriteBytes = consumeFramebufferWriteBytes();

    printf("profile hle: lcd_set=%llu lcd_flip=%llu fb_write=%llu/%llub time=%llu gettick=%llu delay_ms=%llu/%llums udelay=%llu/%lluus ostimedly=%llu/%lluticks wave_can=%llu wave_write=%llu/%llub sem=%llu/%llu/%llu task=%llu sys_event=%llu kbd=%llu dl_res=%llu/%llu/%llub\n",
        (unsigned long long)s_hleProfile.lcdSetFrame,
        (unsigned long long)s_hleProfile.lcdFlip,
        (unsigned long long)fbWrites,
        (unsigned long long)fbWriteBytes,
        (unsigned long long)s_hleProfile.osTimeGet,
        (unsigned long long)s_hleProfile.getTickCount,
        (unsigned long long)s_hleProfile.delayMs,
        (unsigned long long)s_hleProfile.delayMsTotal,
        (unsigned long long)s_hleProfile.udelay,
        (unsigned long long)s_hleProfile.udelayTotal,
        (unsigned long long)s_hleProfile.osTimeDly,
        (unsigned long long)s_hleProfile.osTimeDlyTotal,
        (unsigned long long)s_hleProfile.waveCanWrite,
        (unsigned long long)s_hleProfile.waveWrite,
        (unsigned long long)s_hleProfile.waveWriteBytes,
        (unsigned long long)s_hleProfile.semCreate,
        (unsigned long long)s_hleProfile.semPend,
        (unsigned long long)s_hleProfile.semPost,
        (unsigned long long)s_hleProfile.taskCreate,
        (unsigned long long)s_hleProfile.sysJudgeEvent,
        (unsigned long long)s_hleProfile.kbdStatus,
        (unsigned long long)s_hleProfile.dlResOpen,
        (unsigned long long)s_hleProfile.dlResRead,
        (unsigned long long)s_hleProfile.dlResReadBytes);

    memset(&s_hleProfile, 0, sizeof(s_hleProfile));
    lastTicks = now;
}

static bool envTraceEnabled(const char* name)
{
    const char* value = getenv(name);
    return value && value[0] && strcmp(value, "0") != 0;
}

static uint32_t parseTraceHex(const char* name)
{
    const char* value = getenv(name);
    if (!value || !value[0])
    {
        return 0;
    }
    return (uint32_t)strtoul(value, NULL, 0);
}

static bool traceRangeOverlaps(uint32_t address, uint32_t size)
{
    static bool initialized = false;
    static uint32_t traceStart = 0;
    static uint32_t traceEnd = 0;

    if (!initialized)
    {
        traceStart = parseTraceHex("DINGOO_PIE_TRACE_MEM_START");
        traceEnd = parseTraceHex("DINGOO_PIE_TRACE_MEM_END");
        initialized = true;
    }

    if (!traceStart || !traceEnd)
    {
        return true;
    }

    uint64_t begin = address;
    uint64_t end = begin + size;
    return begin < traceEnd && end > traceStart;
}

static bool shouldTraceCopy(uint32_t address, uint32_t size)
{
    return envTraceEnabled("DINGOO_PIE_TRACE_COPY") && traceRangeOverlaps(address, size);
}

static bool shouldTraceTasks(void)
{
    return envTraceEnabled("DINGOO_PIE_TRACE_TASKS");
}

static bool shouldTraceKbdCallers(void)
{
    return envTraceEnabled("DINGOO_PIE_TRACE_KBD_CALLERS");
}

static bool parseUnitDoubleEnv(const char* name, double* out)
{
    const char* value = getenv(name);
    if (!value || !value[0])
    {
        return false;
    }

    char* end = NULL;
    double parsed = strtod(value, &end);
    if (end == value || parsed < 0.0)
    {
        return false;
    }
    if (parsed > 1.0)
    {
        parsed = 1.0;
    }
    *out = parsed;
    return true;
}

static double defaultHostDelayScaleForApp(void)
{
    double compatScale = compatDefaultHostDelayScale(s_bridgeAppSha256.c_str());
    return compatScale != 1.0 ? compatScale : kAutoHostDelayScale;
}

void bridge_apply_runtime_settings(void)
{
    double envScale = 1.0;
    double runtimeScale = kAutoRuntimeSpeedScale;
    bool runtimeScaleForced = runtimeScale > 0.0 && runtimeScale < 1.0;
    if (parseUnitDoubleEnv("DINGOO_PIE_RUNTIME_SPEED_SCALE", &envScale) && envScale > 0.0)
    {
        runtimeScale = envScale;
        runtimeScaleForced = true;
        printf("hle: runtime speed scale %.3f env\n", runtimeScale);
    }
    else if (runtimeScale != 1.0)
    {
        printf("hle: runtime speed scale %.3f auto\n", runtimeScale);
    }

    double delayScale = 1.0;
    if (parseUnitDoubleEnv("DINGOO_PIE_OSTIMEDLY_SCALE", &envScale))
    {
        delayScale = envScale;
        printf("hle: host delay scale %.3f env\n", delayScale);
    }
    else
    {
        delayScale = defaultHostDelayScaleForApp();
        if (delayScale != 1.0)
        {
            const char* profileName = compatProfileName(s_bridgeAppSha256.c_str());
            printf("hle: host delay scale %.3f %s=%s\n",
                delayScale,
                strcmp(profileName, "default") == 0 ? "auto" : "app",
                strcmp(profileName, "default") == 0 ? "global" : bridge_get_app_identity());
        }
    }

    s_bridgeProfileEnabled.store(getenv("DINGOO_PIE_PROFILE") != NULL);
    s_runtimeSpeedScale.store(runtimeScale);
    s_runtimeSpeedScaleForced.store(runtimeScaleForced);
    s_hostDelayScale.store(delayScale);
}

static double runtimeSpeedScale(void)
{
    return s_runtimeSpeedScale.load();
}

static uint64_t scaledHostDelayMicros(uint64_t originalUs)
{
    double scale = s_hostDelayScale.load();
    if (originalUs == 0 || scale <= 0.0)
    {
        return 0;
    }

    if (s_runtimeSpeedScaleForced.load())
    {
        double speedScale = s_runtimeSpeedScale.load();
        if (speedScale > 0.0 && speedScale < 1.0)
        {
            scale /= speedScale;
        }
    }

    double scaled = (double)originalUs * scale;
    if (scaled < 1.0)
    {
        return 1;
    }
    if (scaled > (double)UINT64_MAX)
    {
        return UINT64_MAX;
    }
    return (uint64_t)(scaled + 0.5);
}

static bool adaptiveHostDelayEnabled(void)
{
    static int enabled = -1;
    if (enabled < 0)
    {
        const char* value = getenv("DINGOO_PIE_ADAPTIVE_DELAY");
        enabled = (!value || !value[0] || strcmp(value, "0") != 0) ? 1 : 0;
    }
    return enabled != 0;
}

static uint64_t hostNowMicros(void)
{
    uint64_t frequency = SDL_GetPerformanceFrequency();
    uint64_t counter = SDL_GetPerformanceCounter();
    if (!frequency)
    {
        return SDL_GetTicks64() * 1000ull;
    }
    return (counter / frequency) * 1000000ull +
        ((counter % frequency) * 1000000ull) / frequency;
}

static uint64_t adaptiveHostDelayMicros(uint64_t requestedUs)
{
    const uint64_t kShortDelayMaxUs = 50000;
    const uint64_t kResetGapUs = 250000;
    const uint64_t kMaxBehindUs = 25000;
    static thread_local uint64_t nextWakeUs = 0;
    static thread_local uint64_t lastCallUs = 0;

    if (requestedUs == 0)
    {
        return 0;
    }

    if (requestedUs > kShortDelayMaxUs)
    {
        nextWakeUs = 0;
        lastCallUs = 0;
        return requestedUs;
    }

    uint64_t nowUs = hostNowMicros();
    if (!nextWakeUs ||
        (lastCallUs && nowUs - lastCallUs > kResetGapUs) ||
        nowUs > nextWakeUs + kMaxBehindUs)
    {
        nextWakeUs = nowUs;
    }

    nextWakeUs += requestedUs;
    lastCallUs = nowUs;
    if (nextWakeUs <= nowUs)
    {
        return 0;
    }

    uint64_t sleepUs = nextWakeUs - nowUs;
    if (sleepUs > requestedUs)
    {
        sleepUs = requestedUs;
    }
    return sleepUs;
}

static void waitHostMicros(uint64_t delayUs)
{
    if (delayUs == 0)
    {
        return;
    }

    uint64_t targetUs = hostNowMicros() + delayUs;
    uint64_t nowUs = hostNowMicros();
    while (nowUs < targetUs)
    {
        uint64_t remainingUs = targetUs - nowUs;
        if (remainingUs > 2000)
        {
            std::this_thread::sleep_for(std::chrono::microseconds(remainingUs - 1000));
        }
        else if (remainingUs > 500)
        {
#ifdef _WIN32
            Sleep(0);
#else
            std::this_thread::yield();
#endif
        }
        else
        {
            std::this_thread::yield();
        }
        nowUs = hostNowMicros();
    }
}

static void sleepScaledHostDelayMicros(uint64_t originalUs)
{
    uint64_t hostUs = scaledHostDelayMicros(originalUs);
    if (hostUs > 0 &&
        adaptiveHostDelayEnabled())
    {
        hostUs = adaptiveHostDelayMicros(hostUs);
    }
    if (hostUs > 0)
    {
        waitHostMicros(hostUs);
    }
}

static void registerRuntimeContext(NativeRuntime* runtime, bool isMainRuntime)
{
    if (!runtime)
    {
        return;
    }

    pthread_mutex_lock(&s_runtimeContextsMutex);
    for (size_t i = 0; i < s_runtimeContexts.size(); ++i)
    {
        if (s_runtimeContexts[i].runtime == runtime)
        {
            s_runtimeContexts[i].isMainRuntime = isMainRuntime;
            pthread_mutex_unlock(&s_runtimeContextsMutex);
            return;
        }
    }

    RuntimeBridgeContext context;
    context.runtime = runtime;
    context.isMainRuntime = isMainRuntime;
    s_runtimeContexts.push_back(context);
    pthread_mutex_unlock(&s_runtimeContextsMutex);
}

static bool isMainRuntimeContext(NativeRuntime* runtime)
{
    bool isMainRuntime = true;
    pthread_mutex_lock(&s_runtimeContextsMutex);
    for (size_t i = 0; i < s_runtimeContexts.size(); ++i)
    {
        if (s_runtimeContexts[i].runtime == runtime)
        {
            isMainRuntime = s_runtimeContexts[i].isMainRuntime;
            break;
        }
    }
    pthread_mutex_unlock(&s_runtimeContextsMutex);
    return isMainRuntime;
}

static CompatGuestExitDecision taskStopGuestExitDecision(uint32_t ra)
{
    CompatTaskStopExitContext context;
    context.returnAddress = ra;
    context.frontendQuitRequested = frontendQuitRequested();
    context.sawSuspiciousFileOpenFailure = fsys_saw_suspicious_open_failure();
    return compatTaskStopGuestExitDecision(s_bridgeAppSha256.c_str(), &context);
}

static void requestGuestExit(NativeRuntime* runtime, const char* reason)
{
    uint32_t ra = 0;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &ra);
    printf("hle: guest exit requested by %s ra=0x%08x\n",
        reason ? reason : "<unknown>", ra);
    taskSchedulerRequestShutdown(reason);
    uint32_t ret = 0;
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &ret);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &kGuestExitPc);
    frontendRequestQuit();
}

static void stopCurrentGuestRuntime(NativeRuntime* runtime, const char* reason)
{
    uint32_t ra = 0;
    uint32_t a0 = 0;
    uint32_t pc = 0;
    uint32_t sp = 0;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &ra);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &a0);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_PC, &pc);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_SP, &sp);
    snprintf(s_lastTaskStopSummary, sizeof(s_lastTaskStopSummary),
        "%s pc=0x%08x ra=0x%08x a0=0x%08x sp=0x%08x main=%u",
        reason ? reason : "<unknown>", pc, ra, a0, sp,
        isMainRuntimeContext(runtime) ? 1u : 0u);
    CompatGuestExitDecision exitDecision = taskStopGuestExitDecision(ra);
    printf("hle: task stop reason=%s app_sha256=%s pc=0x%08x ra=0x%08x a0=0x%08x main=%u promoted=%u",
        reason ? reason : "<unknown>",
        s_bridgeAppSha256.c_str(),
        pc,
        ra,
        a0,
        isMainRuntimeContext(runtime) ? 1u : 0u,
        exitDecision.shouldExit ? 1u : 0u);
    if (exitDecision.matched)
    {
        printf(" compat=%s", exitDecision.label ? exitDecision.label : "task-stop exit");
    }
    if (fsys_saw_suspicious_open_failure())
    {
        printf(" fs_suspicious_open_failure=1");
    }
    printf("\n");
    if (envTraceEnabled("DINGOO_PIE_TRACE_HLE"))
    {
        printf("trace-hle: guest task stop requested by %s ra=0x%08x\n",
            reason ? reason : "<unknown>", ra);
    }
    if (shouldTraceTasks())
    {
        printf("trace-task: %s pc=0x%08x ra=0x%08x a0=0x%08x sp=0x%08x main=%u\n",
            reason ? reason : "<unknown>", pc, ra, a0, sp, isMainRuntimeContext(runtime) ? 1u : 0u);
    }
    if (exitDecision.shouldExit)
    {
        requestGuestExit(runtime, reason);
        return;
    }

    uint32_t ret = 0;
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &ret);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &kGuestExitPc);
}

static bool shouldTraceHle(void)
{
    return envTraceEnabled("DINGOO_PIE_TRACE_HLE");
}

// Prototype comments in this section name the guest SDK/libc imports handled
// by each bridge entry point.
//void* malloc(size_t len);
static void br_malloc(NativeRuntime* runtime)
{
    uint32_t len;
    uint32_t ra;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &len);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &ra);
    uint32_t p = vm_malloc(len);
    if (!p && len != 0)
    {
        dumpREG(runtime);
        dumpStackCall(runtime, 0xa0000000);
        dumpAsm(runtime);
        assert(0);
    }
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &p);

    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &ra);
}
//void free(void*);
static void br_free(NativeRuntime* runtime)
{
    uint32_t addr;
    uint32_t pc;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &addr);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &pc);
    vm_free(addr);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &pc);
}
//void* realloc(void*, size_t);
static void br_realloc(NativeRuntime* runtime)
{
    uint32_t addr;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &addr);

    uint32_t size;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A1, &size);

    uint32_t p = vm_realloc(addr, size);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &p);

    uint32_t pc;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &pc);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &pc);
}

//void __func(void);
static void br_common(NativeRuntime* runtime)
{
    uint32_t pc;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &pc);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &pc);
}

static void br_return_zero(NativeRuntime* runtime)
{
    uint32_t ret = 0;
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &ret);
    br_common(runtime);
}

static void br_vxGoHome(NativeRuntime* runtime)
{
    requestGuestExit(runtime, "vxGoHome");
}

static void br_abort(NativeRuntime* runtime)
{
    requestGuestExit(runtime, "abort");
}

static void br_TaskMediaFunStop(NativeRuntime* runtime)
{
    requestGuestExit(runtime, "TaskMediaFunStop");
}

static void br_OSTaskDel(NativeRuntime* runtime)
{
    if (isMainRuntimeContext(runtime))
    {
        requestGuestExit(runtime, "OSTaskDel");
        return;
    }

    stopCurrentGuestRuntime(runtime, "OSTaskDel");
}

static void br_av_end_thread(NativeRuntime* runtime)
{
    requestGuestExit(runtime, "av_end_thread");
}

static void br_av_queue_abort(NativeRuntime* runtime)
{
    requestGuestExit(runtime, "av_queue_abort");
}

static void br_cache_flush(NativeRuntime* runtime)
{
    nativeRuntimeFlushCodeCache(runtime);
    returnToRa(runtime);
}

static void br__to_locale_ansi(NativeRuntime* runtime)
{
    uint32_t inInputpPtr;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &inInputpPtr);

    uint16_t wideBuf[512];
    memset(wideBuf, 0x00, sizeof(wideBuf));
    RuntimeError err = nativeRuntimeReadMemory(runtime, inInputpPtr, wideBuf, sizeof(wideBuf) - sizeof(uint16_t));
    if (err)
    {
        printf("Failed on nativeRuntimeReadMemory(__to_locale_ansi) with error returned: %u (%s)\n", err, nativeRuntimeErrorString(err));
        br_return_zero(runtime);
        return;
    }

    char ansiBuf[512];
    memset(ansiBuf, 0x00, sizeof(ansiBuf));
    size_t out = 0;
    for (size_t i = 0; i < sizeof(wideBuf) / sizeof(wideBuf[0]) && wideBuf[i] != 0 && out + 1 < sizeof(ansiBuf); ++i)
    {
        uint16_t wc = wideBuf[i];
        ansiBuf[out++] = (wc >= 0x20 && wc <= 0x7e) ? (char)wc : '?';
    }

    err = nativeRuntimeWriteMemory(runtime, inInputpPtr, ansiBuf, (uint32_t)(out + 1));
    if (err)
    {
        printf("Failed on nativeRuntimeWriteMemory(__to_locale_ansi) with error returned: %u (%s)\n", err, nativeRuntimeErrorString(err));
        br_return_zero(runtime);
        return;
    }

    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &inInputpPtr);
    br_common(runtime);
}

static uint64_t s_tempTicks = 0;
// time ms
uint32_t OSTimeGet(void)
{
    if (s_tempTicks == 0)
    {
        s_tempTicks = SDL_GetTicks64();
    }

    uint64_t tempTicks = SDL_GetTicks64() - s_tempTicks;
    double speedScale = runtimeSpeedScale();
    if (speedScale > 0.0 && speedScale < 1.0)
    {
        tempTicks = (uint64_t)((double)tempTicks * speedScale);
    }

    tempTicks *= OS_TICKS_PER_SEC;
    tempTicks /= 1000;

    return (uint32_t)tempTicks;
}

uint32_t bridge_capture_os_ticks(void)
{
    return OSTimeGet();
}

void bridge_restore_os_ticks(uint32_t ticks)
{
    double speedScale = runtimeSpeedScale();
    double effectiveScale = (speedScale > 0.0 && speedScale < 1.0) ? speedScale : 1.0;
    uint64_t elapsedMs = ((uint64_t)ticks * 1000ull) / OS_TICKS_PER_SEC;
    uint64_t hostElapsedMs = (uint64_t)((double)elapsedMs / effectiveScale);
    uint64_t now = SDL_GetTicks64();
    s_tempTicks = now > hostElapsedMs ? now - hostElapsedMs : 1;
}

static void br_GetTickCount(NativeRuntime* runtime)
{
    s_hleProfile.getTickCount++;
    uint64_t ticks = OSTimeGet();
    uint64_t value = (ticks * 1000000ull) / OS_TICKS_PER_SEC;
    uint32_t ret = value & 0xFFFFFFFF;
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &ret);

    uint32_t pc;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &pc);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &pc);
}

static void br_OSTimeGet(NativeRuntime* runtime)
{
    s_hleProfile.osTimeGet++;
    uint32_t tick_time_10ms = OSTimeGet();

    uint32_t ret = tick_time_10ms;
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &ret);

    uint32_t pc;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &pc);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &pc);
}

static void br__kbd_get_status(NativeRuntime* runtime)
{
    s_hleProfile.kbdStatus++;

    uint32_t ksPtr;
    uint32_t pc = 0;
    uint32_t ra = 0;
    uint32_t sp = 0;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &ksPtr);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_PC, &pc);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &ra);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_SP, &sp);

    KEY_STATUS* ks = (KEY_STATUS*)toHostPtr(ksPtr);
    if (ks)
    {
        _kbd_get_status(ks);
        if (shouldTraceKbdCallers() && (ks->pressed || ks->released || ks->status))
        {
            printf("trace-kbd: pc=0x%08x ra=0x%08x sp=0x%08x ks=0x%08x pressed=0x%08lx released=0x%08lx status=0x%08lx app=%s\n",
                pc, ra, sp, ksPtr,
                (unsigned long)ks->pressed,
                (unsigned long)ks->released,
                (unsigned long)ks->status,
                bridge_get_app_identity());
        }
    }
    else
    {
        printf("hle: _kbd_get_status invalid pointer 0x%08x\n", ksPtr);
    }

    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &ra);
}

struct DingooSemaphore
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    std::atomic<uint32_t> count;
};

static DingooSemaphore* s_semaphore_map[128] = { NULL };

static bool dingooSemaphorePend(DingooSemaphore* sem)
{
    if (!sem)
    {
        return false;
    }

    uint32_t current = sem->count.load(std::memory_order_acquire);
    while (current > 0)
    {
        if (sem->count.compare_exchange_weak(
                current, current - 1,
                std::memory_order_acq_rel,
                std::memory_order_acquire))
        {
            return true;
        }
    }

    pthread_mutex_lock(&sem->mutex);
    while ((current = sem->count.load(std::memory_order_acquire)) == 0)
    {
        pthread_cond_wait(&sem->cond, &sem->mutex);
    }
    sem->count.store(current - 1, std::memory_order_release);
    pthread_mutex_unlock(&sem->mutex);
    return true;
}

static bool dingooSemaphorePost(DingooSemaphore* sem)
{
    if (!sem)
    {
        return false;
    }

    pthread_mutex_lock(&sem->mutex);
    sem->count.fetch_add(1, std::memory_order_release);
    pthread_cond_signal(&sem->cond);
    pthread_mutex_unlock(&sem->mutex);
    return true;
}

//typedef void OS_EVENT;
//OS_EVENT* OSSemCreate(uint16_t cnt);
static void br_OSSemCreate(NativeRuntime* runtime)
{
    s_hleProfile.semCreate++;

    uint32_t index = 1;
    uint32_t cnt;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &cnt);

    DingooSemaphore* sem = (DingooSemaphore*)malloc(sizeof(DingooSemaphore));
    if (!sem)
    {
        printf("Failed malloc for semaphore\n");
        assert(0);
    }
    int mutexRet = pthread_mutex_init(&sem->mutex, NULL);
    int condRet = pthread_cond_init(&sem->cond, NULL);
    if (mutexRet || condRet)
    {
        printf("Failed semaphore init mutex=%d cond=%d\n", mutexRet, condRet);
        assert(0);
    }
    sem->count.store(cnt, std::memory_order_release);
    for (; index < sizeof(s_semaphore_map) / sizeof(s_semaphore_map[0]); ++index)
    {
        if (s_semaphore_map[index] == NULL)
        {
            break;
        }
    }
    if (index >= sizeof(s_semaphore_map) / sizeof(s_semaphore_map[0]))
    {
        printf("Failed sem_init with error : %u, index %d \n", errno, index);
        assert(0);
    }
    s_semaphore_map[index] = sem;

    uint32_t ret = index;
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &ret);

    uint32_t pc;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &pc);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &pc);
}

/*
extern OS_EVENT* OSSemDel(OS_EVENT *event, uint8_t option, uint8_t* error);
extern void      OSSemPend(OS_EVENT* event, uint16_t timeout, uint8_t* error);
extern uint8_t   OSSemPost(OS_EVENT* event);
*/

static void br_OSSemPend(NativeRuntime* runtime)
{
    s_hleProfile.semPend++;

    uint32_t eventVal;
    uint32_t timeout;
    uint32_t errorPtr;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &eventVal);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A1, &timeout);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A2, &errorPtr);

    DingooSemaphore* sem = s_semaphore_map[eventVal];
    if (!dingooSemaphorePend(sem))
    {
        printf("Failed semaphore pend index=%u timeout=%u\n", eventVal, timeout);
        assert(0);
    }

    uint8_t* error = (uint8_t*)toHostPtr(errorPtr);
    if (error)
    {
        *error = OS_NO_ERR;
    }

    uint32_t pc;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &pc);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &pc);
}

static void br_OSSemPost(NativeRuntime* runtime)
{
    s_hleProfile.semPost++;

    uint32_t eventVal;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &eventVal);

    DingooSemaphore* sem = s_semaphore_map[eventVal];
    if (!dingooSemaphorePost(sem))
    {
        printf("Failed semaphore post index=%u\n", eventVal);
        assert(0);
    }

    uint32_t retVal = OS_NO_ERR;
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &retVal);

    uint32_t pc;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &pc);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &pc);
}

bool bridge_fast_os_sem_pend(uint32_t eventVal, uint32_t timeout, uint32_t errorPtr)
{
    (void)timeout;
    if (eventVal >= sizeof(s_semaphore_map) / sizeof(s_semaphore_map[0]) || !s_semaphore_map[eventVal])
    {
        return false;
    }

    s_hleProfile.semPend++;
    if (!dingooSemaphorePend(s_semaphore_map[eventVal]))
    {
        return false;
    }

    uint8_t* error = (uint8_t*)toHostPtr(errorPtr);
    if (error)
    {
        *error = OS_NO_ERR;
    }
    return true;
}

bool bridge_fast_os_sem_post(uint32_t eventVal, uint32_t* returnValue)
{
    if (eventVal >= sizeof(s_semaphore_map) / sizeof(s_semaphore_map[0]) || !s_semaphore_map[eventVal])
    {
        return false;
    }

    s_hleProfile.semPost++;
    if (!dingooSemaphorePost(s_semaphore_map[eventVal]))
    {
        return false;
    }

    if (returnValue)
    {
        *returnValue = OS_NO_ERR;
    }
    return true;
}

//uint8_t   OSTaskCreate(void (*task)(void* data), void* data, OS_STK* stack, uint8_t priority);
static void br_OSTaskCreate(NativeRuntime* runtime)
{
    s_hleProfile.taskCreate++;

    uint32_t taskFuncAddr;
    uint32_t dataPtr;
    uint32_t stackPtr;
    uint32_t priority;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &taskFuncAddr);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A1, &dataPtr);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A2, &stackPtr);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A3, &priority);
    if (shouldTraceTasks())
    {
        uint32_t ra = 0;
        uint32_t sp = 0;
        nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &ra);
        nativeRuntimeReadRegister(runtime, RUNTIME_REG_SP, &sp);
        printf("trace-task: OSTaskCreate entry=0x%08x data=0x%08x stack=0x%08x priority=%u ra=0x%08x sp=0x%08x main=%u\n",
            taskFuncAddr, dataPtr, stackPtr, priority, ra, sp, isMainRuntimeContext(runtime) ? 1u : 0u);
    }

    uint32_t ret = OSTaskCreate(taskFuncAddr, dataPtr, stackPtr, priority);

    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &ret);

    uint32_t pc;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &pc);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &pc);
}

//extern void* waveout_open(waveout_args* args);
static void br_waveout_open(NativeRuntime* runtime)
{
    uint32_t argsPtr;
    uint32_t ret = 0;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &argsPtr);

    waveout_args* args = (waveout_args*)toHostPtr(argsPtr);
    waveout_args* argsCpy = (waveout_args*)malloc(sizeof(waveout_args));
    if (args != NULL && argsCpy != NULL)
    {
        memcpy(argsCpy, args, sizeof(waveout_args));
        ret = waveout_open(argsCpy);
    }

    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &ret);

    uint32_t pc;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &pc);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &pc);
}

//extern int waveout_write(waveout_inst* inst, char* buffer, int count);
static void br_waveout_write(NativeRuntime* runtime)
{
    uint32_t instPtr;
    uint32_t bufferPtr;
    uint32_t count;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &instPtr);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A1, &bufferPtr);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A2, &count);
    s_hleProfile.waveWrite++;
    s_hleProfile.waveWriteBytes += count;

    uint32_t ret = 1;
    if (!waveout_drops_audio())
    {
        void* src = toHostPtr(bufferPtr);
        if (src && count > 0)
        {
            char* buff = (char*)malloc(count);
            if (buff)
            {
                memcpy(buff, src, count);
                ret = waveout_write(instPtr, buff, count);
            }
        }
    }
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &ret);

    uint32_t pc;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &pc);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &pc);
}

//typedef void waveout_inst;
//int waveout_close(waveout_inst*);
static void br_waveout_close(NativeRuntime* runtime)
{
    uint32_t ptr;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &ptr);

    uint32_t ret = waveout_close(ptr);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &ret);

    uint32_t pc;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &pc);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &pc);
}

static void br_HP_Mute_sw(NativeRuntime* runtime)
{
    uint32_t muted;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &muted);

    uint32_t ret = waveout_mute(muted);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &ret);

    uint32_t pc;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &pc);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &pc);
}

//int waveout_can_write();
static void br_waveout_can_write(NativeRuntime* runtime)
{
    s_hleProfile.waveCanWrite++;
    uint32_t ret = waveout_can_write();
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &ret);

    uint32_t pc;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &pc);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &pc);
}

bool bridge_fast_waveout_write(uint32_t instPtr, uint32_t bufferPtr, uint32_t count, uint32_t* returnValue)
{
    s_hleProfile.waveWrite++;
    s_hleProfile.waveWriteBytes += count;

    uint32_t ret = 1;
    if (!waveout_drops_audio())
    {
        void* src = toHostPtr(bufferPtr);
        if (!src || count == 0)
        {
            ret = 0;
        }
        else
        {
            char* buffer = (char*)malloc(count);
            if (!buffer)
            {
                return false;
            }
            memcpy(buffer, src, count);
            ret = waveout_write(instPtr, buffer, count);
        }
    }

    if (returnValue)
    {
        *returnValue = ret;
    }
    return true;
}

uint32_t bridge_fast_waveout_can_write(void)
{
    s_hleProfile.waveCanWrite++;
    return waveout_can_write();
}

static void br_waveout_set_volume(NativeRuntime* runtime)
{
    uint32_t vol;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &vol);

    uint32_t ret = waveout_set_volume(vol);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &ret);

    uint32_t pc;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &pc);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &pc);
}

//void* _lcd_get_frame()
static void br__lcd_get_frame(NativeRuntime* runtime)
{
    uint32_t ptr = _lcd_get_frame();
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &ptr);

    uint32_t pc;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &pc);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &pc);
}
//void* lcd_get_frame()
static void br_lcd_get_frame(NativeRuntime* runtime)
{
    br__lcd_get_frame(runtime);
}

extern void updateFb(void);

//void _lcd_set_frame()
static void br__lcd_set_frame(NativeRuntime* runtime)
{
    s_hleProfile.lcdSetFrame++;
    updateFb();

    br_common(runtime);
}

static void br_lcd_set_frame(NativeRuntime* runtime)
{
    br__lcd_set_frame(runtime);
}

static void br_ap_lcd_set_frame(NativeRuntime* runtime)
{
    br__lcd_set_frame(runtime);
}

static void br_lcd_get_cframe(NativeRuntime* runtime)
{
    br__lcd_get_frame(runtime);
}

static void br_lcd_flip(NativeRuntime* runtime)
{
    s_hleProfile.lcdFlip++;
    requestFbUpdate();
    returnToRa(runtime);
}

static void br_lcd_get_bpp(NativeRuntime* runtime)
{
    uint32_t ret = 16;
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &ret);
    returnToRa(runtime);
}

static void br_LcdGetDisMode(NativeRuntime* runtime)
{
    uint32_t ret = 0;
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &ret);
    returnToRa(runtime);
}

static void br__kbd_get_key(NativeRuntime* runtime)
{
    uint32_t pc = 0;
    uint32_t ra = 0;
    uint32_t sp = 0;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_PC, &pc);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &ra);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_SP, &sp);

    uint32_t ret = _kbd_get_key();
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &ret);
    if (shouldTraceKbdCallers() && ret)
    {
        printf("trace-kbd-key: pc=0x%08x ra=0x%08x sp=0x%08x ret=0x%08x app=%s\n",
            pc, ra, sp, ret, bridge_get_app_identity());
    }
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &ra);
}

static void br_kbd_get_key(NativeRuntime* runtime)
{
    br__kbd_get_key(runtime);
}

static void br_kbd_get_status(NativeRuntime* runtime)
{
    br__kbd_get_status(runtime);
}

static void br_delay_ms(NativeRuntime* runtime)
{
    uint32_t ms;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &ms);
    s_hleProfile.delayMs++;
    s_hleProfile.delayMsTotal += ms;
    sleepScaledHostDelayMicros((uint64_t)ms * 1000ull);
    returnToRa(runtime);
}

static void br_udelay(NativeRuntime* runtime)
{
    uint32_t us;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &us);
    s_hleProfile.udelay++;
    s_hleProfile.udelayTotal += us;
    sleepScaledHostDelayMicros(us);
    returnToRa(runtime);
}

//int fread(void* ptr, size_t size, size_t count, FILE* stream);
static void br_fread(NativeRuntime* runtime)
{
    uint32_t ptr;
    uint32_t size;
    uint32_t count;
    uint32_t stream;
    uint32_t read_size;
    uint32_t read_ret = -1;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &ptr);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A1, &size);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A2, &count);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A3, &stream);

    if (size != 0 && count > UINT32_MAX / size)
    {
        read_size = UINT32_MAX;
    }
    else
    {
        read_size = size * count;
    }

    _file_t *_file = (_file_t*)toHostPtr(stream);
    if (!_file)
    {
        read_ret =  -1;
    }
    else
    {
        if (_file->type == _file_type_mem)
        {
            _file_mem_t* _file_mem = (_file_mem_t*)toHostPtr(_file->data);
            if (!_file_mem)
            {
                read_ret = -1;
            }
            else if (_file_mem->read)
            {
                uint32_t available = (_file_mem->offset < _file_mem->size) ?
                    (_file_mem->size - _file_mem->offset) : 0;
                uint32_t bytesToRead = read_size < available ? read_size : available;
                void* buff = toHostPtr(_file_mem->base + _file_mem->offset);
                void* distPtr = toHostPtr(ptr);
                if (!buff || !distPtr)
                {
                    read_ret = -1;
                }
                else
                {
                    if (bytesToRead > 0)
                    {
                        memcpy(distPtr, buff, bytesToRead);
                        _file_mem->offset += bytesToRead;
                    }
                    read_ret = size ? (bytesToRead / size) : 0;

                    _file->eof = _file_mem->offset >= _file_mem->size ? 1u : 0u;
                }
            }
        }
        else if (_file->type == _file_type_file)
        {
            void* buff = toHostPtr(ptr);
            if (buff)
            {
                read_ret = vm_fread(buff, size, count, _file->data);
            }
            else
            {
                read_ret = -1;
            }
        }
        else
        {
            printf("Failed br_fread with: %d\n", _file->type);
            assert(0);
        }
    }

    uint32_t ret = read_ret;
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &ret);

    uint32_t pc;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &pc);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &pc);
}

//int sprintf(char* buff, const char* fmt, char* va_list);
uint32_t vm_sprintf(NativeRuntime* runtime, uint32_t buffPtr, uint32_t fmtPtr, uint32_t val1Ptr, uint32_t val2Ptr)
{
    char* buff = (char*)toHostPtr(buffPtr);
    char* fmt = (char*)toHostPtr(fmtPtr);
    char* val1 = (char*)toHostPtr(val1Ptr);
    char* val2 = (char*)toHostPtr(val2Ptr);

    if (NULL == val1 && NULL != val2)
    {
        return sprintf(buff, fmt, val1Ptr, val2);
    }
    else if (NULL == val1 && NULL == val2)
    {
        return sprintf(buff, fmt, val1Ptr, val2Ptr);
    }

    return sprintf(buff, fmt, val1, val2);
}

static void br_sprintf(NativeRuntime* runtime)
{
    my_sprintf(runtime);
}

//typedef void FSYS_FILE;
//extern FSYS_FILE* fsys_fopen(const char* name, const char* mode);
static void br_fsys_fopen(NativeRuntime* runtime)
{
    uint32_t namePtr;
    uint32_t modePtr;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &namePtr);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A1, &modePtr);

    char* name = (char*)toHostPtr(namePtr);
    char* mode = (char*)toHostPtr(modePtr);
    uint32_t fpPtr = fsys_fopen(name, mode);
    uint32_t ret = fpPtr;
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &ret);
    uint32_t pc;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &pc);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &pc);
}

//typedef void FSYS_FILE;
//extern int fsys_fclose(FSYS_FILE*);
static void br_fsys_fclose(NativeRuntime* runtime)
{
    uint32_t file;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &file);

    uint32_t ret = fsys_fclose(file);

    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &ret);
    uint32_t pc;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &pc);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &pc);
}
//int fsys_fseek(FSYS_FILE*, int offset, int origin);
static void br_fsys_fseek(NativeRuntime* runtime)
{
    uint32_t file;
    uint32_t offset;
    uint32_t origin;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &file);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A1, &offset);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A2, &origin);

    uint32_t ret = fsys_fseek(file, offset, origin);

    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &ret);

    uint32_t pc;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &pc);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &pc);
}

static void br_fsys_ftell(NativeRuntime* runtime)
{
    uint32_t file;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &file);

    uint32_t ret = fsys_ftell(file);

    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &ret);

    uint32_t pc;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &pc);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &pc);
}

//uint32_t fsys_fwrite(void* ptr, uint32_t size, uint32_t count, uint32_t stream)
static void br_fsys_fwrite(NativeRuntime* runtime)
{
    uint32_t ret = (uint32_t)-1;
    uint32_t ptr, size, count, stream;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &ptr);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A1, &size);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A2, &count);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A3, &stream);

    void* buff = toHostPtr(ptr);
    if (buff)
    {
        ret = fsys_fwrite(buff, size, count, stream);
    }

    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &ret);

    uint32_t pc;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &pc);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &pc);
}

//uint32_t fsys_fread(void* ptr, uint32_t size, uint32_t count, uint32_t stream)
static void br_fsys_fread(NativeRuntime* runtime)
{
    uint32_t read_ret = -1;
    uint32_t ptr, size, count, stream;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &ptr);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A1, &size);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A2, &count);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A3, &stream);

    void* buff = toHostPtr(ptr);
    if (buff)
    {
        read_ret = vm_fread(buff, size, count, stream);
    }
    else
    {
        read_ret = -1;
    }

    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &read_ret);

    uint32_t pc;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &pc);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &pc);
}

static void br_fsys_feof(NativeRuntime* runtime)
{
    uint32_t file;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &file);

    uint32_t ret = fsys_feof(file);

    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &ret);

    uint32_t pc;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &pc);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &pc);
}

// Reports whether there is a pending input/system event.
static void br__sys_judge_event(NativeRuntime* runtime)
{
    s_hleProfile.sysJudgeEvent++;

    uint32_t inPtr;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &inPtr);

    (void)inPtr;

    uint32_t ret = inputHasPendingEvent();
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &ret);

    uint32_t pc;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &pc);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &pc);
}

static void br_sys_judge_event(NativeRuntime* runtime)
{
    br__sys_judge_event(runtime);
}

// extern void OSTimeDly(uint16_t ticks);
static void br_OSTimeDly(NativeRuntime* runtime)
{
    uint32_t ticks;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &ticks);
    s_hleProfile.osTimeDly++;
    s_hleProfile.osTimeDlyTotal += ticks;

    uint64_t delayUs = ((uint64_t)ticks * 1000000ull) / OS_TICKS_PER_SEC;
    sleepScaledHostDelayMicros(delayUs);

    uint32_t pc;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &pc);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &pc);
}

//extern void* memset(void* outDest, int inValue, size_t inLength);
static void br_memset(NativeRuntime* runtime)
{
    uint32_t ret = 0;
    uint32_t outDestPtr;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &outDestPtr);
    uint32_t inValue;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A1, &inValue);
    uint32_t inLength;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A2, &inLength);

    if (inLength == 0)
    {
        ret = outDestPtr;
        nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &ret);

        uint32_t pc;
        nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &pc);
        nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &pc);
        return;
    }

    void* in = toHostPtr(outDestPtr);
    if (in)
    {
        void* out = memset(in, inValue, inLength);
        ret = toVmPtr(out);
    }
    else
    {
        ret = 0;
    }

    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &ret);

    uint32_t pc;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &pc);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &pc);
}

//extern void* memcpy(void* outDest, const void* inSrc, size_t inLength);
static void br_memcpy(NativeRuntime* runtime)
{
    uint32_t ret = 0;
    uint32_t outDestPtr;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &outDestPtr);
    uint32_t inSrcPtr;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A1, &inSrcPtr);
    uint32_t inLength;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A2, &inLength);

    if (inLength == 0)
    {
        ret = outDestPtr;
        nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &ret);

        uint32_t pc;
        nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &pc);
        nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &pc);
        return;
    }

    void* outDest = toHostPtr(outDestPtr);
    void* inSrc = toHostPtr(inSrcPtr);
    if (outDest && inSrc)
    {
        if (shouldTraceCopy(outDestPtr, inLength) || shouldTraceCopy(inSrcPtr, inLength))
        {
            uint32_t ra = 0;
            nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &ra);
            printf("trace-copy: hle=memcpy dst=0x%08x src=0x%08x len=%u ra=0x%08x\n",
                outDestPtr, inSrcPtr, inLength, ra);
            printf("trace-copy-src:\n");
            dumpMem(inSrc, inLength < 0x40 ? inLength : 0x40);
        }
        void* out = memcpy(outDest, inSrc, inLength);
        ret = toVmPtr(out);
    }
    else
    {
        ret = 0;
    }

    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &ret);

    uint32_t pc;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &pc);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &pc);
}

//extern size_t strlen(const char* str);
static void br_strlen(NativeRuntime* runtime)
{
    uint32_t strPtr;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &strPtr);

    char* str = (char*)toHostPtr(strPtr);
    uint32_t ret = str ? (uint32_t)strlen(str) : 0;

    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &ret);

    uint32_t pc;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &pc);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &pc);
}

//extern int fseek(FILE* stream, long int offset, int origin);
static void br_fseek(NativeRuntime* runtime)
{
    uint32_t origin;
    uint32_t offset;
    uint32_t stream;
    uint32_t read_ret = -1;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A2, &origin);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A1, &offset);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &stream);

    _file_t* _file = (_file_t*)toHostPtr(stream);
    if (_file)
    {
        if (_file->type == _file_type_mem)
        {
            _file_mem_t* _file_mem = (_file_mem_t*)toHostPtr(_file->data);
            if (!_file_mem)
            {
                read_ret = -1;
            }
            else
            {
                int64_t base = 0;
                if (origin == SEEK_SET)
                {
                    base = 0;
                }
                else if (origin == SEEK_CUR)
                {
                    base = _file_mem->offset;
                }
                else if (origin == SEEK_END)
                {
                    base = _file_mem->size;
                }
                else
                {
                    read_ret = -1;
                }

                if (read_ret != (uint32_t)-1)
                {
                    int64_t next = base + (int32_t)offset;
                    if (next < 0 || next > _file_mem->size)
                    {
                        read_ret = -1;
                    }
                    else
                    {
                        _file_mem->offset = (uint32_t)next;
                        _file->eof = _file_mem->offset >= _file_mem->size ? 1u : 0u;
                        read_ret = 0;
                    }
                }
            }
        }
        else if (_file->type == _file_type_file)
        {
            read_ret = fsys_fseek(_file->data, offset, origin);
        }
        else
        {
            printf("Failed br_fseek with: %d\n", _file->type);
            assert(0);
        }
    }

    uint32_t ret = read_ret;
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &ret);

    uint32_t pc;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &pc);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &pc);
}

static void br_get_current_language(NativeRuntime* runtime)
{
    uint32_t val0;
    uint32_t val1;
    uint32_t val2;
    uint32_t val3;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &val0);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A1, &val1);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A2, &val2);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A3, &val3);

    uint32_t ret = 0;
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &ret);

    uint32_t pc;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &pc);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &pc);
}

static void br_get_game_vol(NativeRuntime* runtime)
{
    uint32_t ret = 31;
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &ret);
    returnToRa(runtime);
}

static void br__to_unicode_le(NativeRuntime* runtime)
{
    uint32_t inPtr;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &inPtr);

    char* in = (char*)toHostPtr(inPtr);
    if (!in)
    {
        br_return_zero(runtime);
        return;
    }

    size_t inLen = strnlen(in, 512);
    uint32_t outBytes = (uint32_t)((inLen + 1) * sizeof(uint16_t));
    uint32_t outPtr = vm_malloc(outBytes);
    if (!outPtr)
    {
        br_return_zero(runtime);
        return;
    }

    uint16_t* out = (uint16_t*)toHostPtr(outPtr);
    for (size_t i = 0; i <= inLen; ++i)
    {
        out[i] = (uint8_t)in[i];
    }

    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &outPtr);
    br_common(runtime);
}

static void br_fsys_fopenW(NativeRuntime* runtime)
{
    uint32_t namePtr;
    uint32_t modePtr;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &namePtr);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A1, &modePtr);

    wchar_t* name = (wchar_t*)toHostPtr(namePtr);
    wchar_t* mode = (wchar_t*)toHostPtr(modePtr);

    std::string namestr = WString2String(name);
    std::string modestr = WString2String(mode);

    uint32_t fpPtr = fsys_fopen(namestr.c_str(), modestr.c_str());
    uint32_t ret = fpPtr;
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &ret);
    uint32_t pc;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &pc);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &pc);
}

struct DlResHandle
{
    app_resource_entry* entry;
    uint32_t dataPtr;
    uint32_t offset;
};

static DlResHandle s_dl_res_handles[128];

static char* hostStringIfVmPtr(uint32_t ptr)
{
    if (ptr < 0x10000)
    {
        return NULL;
    }
    return (char*)toHostPtr(ptr);
}

static void br_get_dl_handle(NativeRuntime* runtime)
{
    uint32_t ret = s_bridgeApp ? 1 : 0;
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &ret);
    returnToRa(runtime);
}

static void br_dl_res_open(NativeRuntime* runtime)
{
    uint32_t a0;
    uint32_t a1;
    uint32_t a2;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &a0);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A1, &a1);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A2, &a2);

    char* name = hostStringIfVmPtr(a2);
    if (!name)
    {
        name = hostStringIfVmPtr(a1);
    }
    if (!name)
    {
        name = hostStringIfVmPtr(a0);
    }

    app_resource_entry* entry = app_find_resource(s_bridgeApp, name);
    uint32_t ret = 0;
    if (entry)
    {
        for (uint32_t i = 1; i < sizeof(s_dl_res_handles) / sizeof(s_dl_res_handles[0]); ++i)
        {
            if (!s_dl_res_handles[i].entry)
            {
                s_dl_res_handles[i].entry = entry;
                s_dl_res_handles[i].dataPtr = 0;
                s_dl_res_handles[i].offset = 0;
                ret = i;
                break;
            }
        }
    }

    if (shouldTraceHle())
    {
        printf("trace-hle: dl_res_open a0=0x%08x a1=0x%08x a2=0x%08x name=%s ret=%u",
            a0, a1, a2, name ? name : "<null>", ret);
        if (entry)
        {
            printf(" offset=0x%08x size=0x%08x xor=0x%02x", entry->offset, entry->size, entry->xor_key);
        }
        printf("\n");
    }

    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &ret);
    returnToRa(runtime);
}

static void br_dl_res_get_size(NativeRuntime* runtime)
{
    uint32_t handle;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &handle);
    uint32_t ret = 0;
    if (handle < sizeof(s_dl_res_handles) / sizeof(s_dl_res_handles[0]) && s_dl_res_handles[handle].entry)
    {
        ret = s_dl_res_handles[handle].entry->size;
    }
    if (shouldTraceHle())
    {
        printf("trace-hle: dl_res_get_size handle=%u ret=%u\n", handle, ret);
    }
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &ret);
    returnToRa(runtime);
}

static void br_dl_res_get_data(NativeRuntime* runtime)
{
    uint32_t handle;
    uint32_t bufferPtr;
    uint32_t buffLen;
    uint32_t readLen;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &handle);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A1, &bufferPtr);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A2, &buffLen);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A3, &readLen);
    uint32_t ret = 0;
    if (handle < sizeof(s_dl_res_handles) / sizeof(s_dl_res_handles[0]) && s_dl_res_handles[handle].entry)
    {
        DlResHandle* h = &s_dl_res_handles[handle];
        const uint8_t* resourceData = app_resource_data(s_bridgeApp, h->entry);
        if (!resourceData)
        {
            nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &ret);
            returnToRa(runtime);
            return;
        }
        if (bufferPtr)
        {
            void* dst = toHostPtr(bufferPtr);
            if (dst)
            {
                uint32_t remaining = (h->offset < h->entry->size) ? (h->entry->size - h->offset) : 0;
                uint32_t copySize = readLen ? readLen : buffLen;
                if (readLen && buffLen > 1 && readLen <= UINT32_MAX / buffLen)
                {
                    copySize = readLen * buffLen;
                }
                if (copySize == 0 || copySize > remaining)
                {
                    copySize = remaining;
                }
                memcpy(dst, resourceData + h->offset, copySize);
                h->offset += copySize;
                ret = readLen ? (copySize / readLen) : copySize;
                s_hleProfile.dlResRead++;
                s_hleProfile.dlResReadBytes += copySize;
                if (shouldTraceHle() || shouldTraceCopy(bufferPtr, copySize))
                {
                    printf("trace-hle: dl_res_get_data handle=%u buffer=0x%08x buffLen=%u readLen=%u copy=%u ret=%u offset=0x%08x\n",
                        handle, bufferPtr, buffLen, readLen, copySize, ret, h->offset);
                }
            }
        }
        else
        {
            if (!h->dataPtr)
            {
                h->dataPtr = vm_malloc(h->entry->size);
                if (h->dataPtr)
                {
                    memcpy(toHostPtr(h->dataPtr), resourceData, h->entry->size);
                }
            }
            ret = h->dataPtr;
            if (ret)
            {
                s_hleProfile.dlResRead++;
                s_hleProfile.dlResReadBytes += h->entry->size;
            }
            if (shouldTraceHle())
            {
                printf("trace-hle: dl_res_get_data handle=%u allocated=0x%08x size=%u\n",
                    handle, ret, h->entry->size);
            }
        }
    }
    s_hleProfile.dlResOpen++;
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &ret);
    returnToRa(runtime);
}

static void br_dl_res_close(NativeRuntime* runtime)
{
    uint32_t handle;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &handle);
    if (handle < sizeof(s_dl_res_handles) / sizeof(s_dl_res_handles[0]))
    {
        if (s_dl_res_handles[handle].dataPtr)
        {
            vm_free(s_dl_res_handles[handle].dataPtr);
        }
        s_dl_res_handles[handle].entry = NULL;
        s_dl_res_handles[handle].dataPtr = 0;
        s_dl_res_handles[handle].offset = 0;
    }
    if (shouldTraceHle())
    {
        printf("trace-hle: dl_res_close handle=%u\n", handle);
    }
    returnToRa(runtime);
}

#define br_none br_return_zero

typedef void (*br_func)(NativeRuntime* runtime);

static void returnToRa(NativeRuntime* runtime)
{
    uint32_t pc;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &pc);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &pc);
}

struct _hook_code_func_
{
    uint32_t offset;
    const char* name;
    br_func func;
    uint32_t lock;
    uint32_t trigger_times;
    uint32_t profile_times;
    uint32_t fast_return_enabled;
    uint32_t fast_return_value;
}_hook_code_func_map[] =
{
    {0,"OSTimeGet",br_OSTimeGet , 1},
    {0,"fread",br_fread, 1},
    {0,"memcpy",br_memcpy, 1},
    {0,"malloc",br_malloc , 1},
    {0,"free",br_free , 1},
    {0,"_lcd_get_frame",br__lcd_get_frame, 1},
    {0,"_lcd_set_frame",br__lcd_set_frame, 1},
    {0,"_sys_judge_event",br__sys_judge_event, 1},
    {0,"_kbd_get_status",br__kbd_get_status, 1},
    {0,"__dcache_writeback_all",br_cache_flush , 1},
    {0,"ap_lcd_set_frame",br_ap_lcd_set_frame, 1},
    {0,"lcd_set_frame",br_lcd_set_frame, 1},
    {0,"lcd_get_frame",br_lcd_get_frame, 1},
    {0,"delay_ms",br_delay_ms, 1},
    {0,"lcd_get_bpp",br_lcd_get_bpp, 1},
    {0,"lcd_get_cframe",br_lcd_get_cframe, 1},
    {0,"lcd_flip",br_lcd_flip, 1},
    {0,"kbd_get_key",br_kbd_get_key, 1},
    {0,"kbd_get_status",br_kbd_get_status, 1},
    {0,"open_gui_key_msg",br_none, 1},
    {0,"tv_get_openflag",br_none, 1},
    {0,"tv_set_openflag",br_none, 1},
    {0,"tv_get_closeflag",br_none, 1},
    {0,"tv_set_closeflag",br_none, 1},
    {0,"tv_disable_switch",br_none, 1},
    {0,"tv_enable_switch",br_none, 1},
    {0,"Read_Acc0",br_none, 1},
    {0,"Memsic_SerialCommInit",br_none, 1},
    {0,"Read_Acc",br_none, 1},
    {0,"Custom_Memsic_test",br_none, 1},
    {0,"Get_X",br_none, 1},
    {0,"Get_Y",br_none, 1},
    {0,"sys_judge_event",br_sys_judge_event, 1},
    {0,"SysDisableBkLight",br_none, 1},
    {0,"SysEnableShutDownPower",br_none, 1},
    {0,"SysDisableCloseBkLight",br_none, 1},
    {0,"_kbd_get_key",br__kbd_get_key, 1},
    {0,"_waveout_open",br_none, 1},
    {0,"_waveout_set_volume",br_waveout_set_volume, 1},
    {0,"jz_pm_pllconvert",br_none, 1},
    {0,"strncasecmp",br_none, 1},
    {0,"sys_get_ccpmp_config",br_none, 1},
    {0,"vxGoHome",br_vxGoHome, 1},
    {0,"cmGetSysModel",br_none, 1},
    {0,"cmGetSysVersion",br_none, 1},
    {0,"fsys_fopen_flash",br_none, 1},
    {0,"fsys_fclose_flash",br_none, 1},
    {0,"get_dl_handle",br_get_dl_handle, 1},
    {0,"get_game_vol",br_get_game_vol, 0, 0, 0, 1, 31},
    {0,"get_current_language",br_get_current_language, 0, 0, 0, 1, 0},
    {0,"fsys_fopen",br_fsys_fopen, 1},
    {0,"fsys_fclose",br_fsys_fclose, 1},
    {0,"fsys_fread",br_fsys_fread, 1},
    {0,"fsys_remove",br_none, 1},
    {0,"fsys_fwrite",br_fsys_fwrite, 1},
    {0,"fsys_fseek",br_fsys_fseek, 1},
    {0,"fsys_ftell",br_fsys_ftell, 1},
    {0,"fsys_feof",br_fsys_feof, 1},
    {0,"fsys_ferror",br_none, 1},
    {0,"fsys_clearerr",br_none, 1},
    {0,"fsys_findfirst",br_none, 1},
    {0,"fsys_findnext",br_none, 1},
    {0,"fsys_findclose",br_none, 1},
    {0,"fsys_mkdir",br_none, 1},
    {0,"fsys_rename",br_none, 1},
    {0,"fsys_flush_cache",br_none, 1},
    {0,"fsys_RefreshCache",br_none, 1},
    {0,"fsys_fopenW",br_fsys_fopenW, 1},
    {0,"fsys_fcloseW",br_none, 1},
    {0,"fsys_removeW",br_none, 1},
    {0,"fsys_renameW",br_none, 1},
    {0,"USB_Connect",br_none, 1},
    {0,"USB_No_Connect",br_none, 1},
    {0,"tv_open",br_none, 1},
    {0,"tv_close",br_none, 1},
    {0,"isTVON",br_none, 1},
    {0,"pcm_ioctl",br_none, 1},
    {0,"mdelay",br_delay_ms, 1},
    {0,"HP_Mute_sw",br_HP_Mute_sw, 1},
    {0,"pcm_can_write",br_none, 1},
    {0,"waveout_open",br_waveout_open, 1},
    {0,"waveout_close_at_once",br_waveout_close, 1},
    {0,"waveout_write",br_waveout_write, 0},
    {0,"waveout_close",br_waveout_close, 1},
    {0,"waveout_can_write",br_waveout_can_write, 0},
    {0,"waveout_set_volume",br_waveout_set_volume, 1},
    {0,"av_reg_object",br_none, 1},
    {0,"av_unreg_object",br_none, 1},
    {0,"av_queue_get",br_none, 1},
    {0,"av_uft8_2_unicode",br_none, 1},
    {0,"av_resize_packet",br_none, 1},
    {0,"av_upper_4cc",br_none, 1},
    {0,"av_begin_thread",br_none, 1},
    {0,"av_end_thread",br_av_end_thread, 1},
    {0,"av_create_sem",br_none, 1},
    {0,"av_wait_sem",br_none, 1},
    {0,"av_wait_sem2",br_none, 1},
    {0,"av_give_sem",br_none, 1},
    {0,"av_destroy_sem",br_none, 1},
    {0,"av_create_flag",br_none, 1},
    {0,"av_wait_flag",br_none, 1},
    {0,"av_give_flag",br_none, 1},
    {0,"av_destroy_flag",br_none, 1},
    {0,"av_delay",br_none, 1},
    {0,"av_queue_init",br_none, 1},
    {0,"av_queue_flush",br_none, 1},
    {0,"av_queue_abort",br_av_queue_abort, 1},
    {0,"av_queue_end",br_none, 1},
    {0,"av_queue_put",br_none, 1},
    {0,"dl_load",br_none, 1},
    {0,"dl_free",br_none, 1},
    {0,"dl_res_open",br_dl_res_open, 1},
    {0,"dl_res_get_size",br_dl_res_get_size, 1},
    {0,"dl_res_get_data",br_dl_res_get_data, 1},
    {0,"dl_res_close",br_dl_res_close, 1},
    {0,"dl_get_proc",br_none, 1},
    {0,"memset",br_memset , 1},
    {0,"strlen",br_strlen , 1},
    {0,"abort",br_abort, 1},
    {0,"fprintf",br_none, 1},
    {0,"fseek",br_fseek, 1},
    {0,"fwrite",br_none, 1},
    {0,"printf",br_none, 1},
    {0,"realloc",br_realloc , 1},
    {0,"sprintf",br_sprintf , 1},
    {0,"sscanf",br_none, 1},
    {0,"vsprintf",br_none, 1},
    {0,"GUI_Exec",br_none, 1},
    {0,"GUI_Lock",br_none, 1},
    {0,"GUI_TIMER_Create",br_none, 1},
    {0,"GUI_TIMER_Delete",br_none, 1},
    {0,"GUI_TIMER_SetPeriod",br_none, 1},
    {0,"GUI_TIMER_Restart",br_none, 1},
    {0,"LCD_Color2Index",br_none, 1},
    {0,"LCD_GetXSize",br_none, 1},
    {0,"LCD_GetYSize",br_none, 1},
    {0,"WM_CreateWindow",br_none, 1},
    {0,"WM_DeleteWindow",br_none, 1},
    {0,"WM_SelectWindow",br_none, 1},
    {0,"WM_DefaultProc",br_none, 1},
    {0,"WM__SendMessage",br_none, 1},
    {0,"WM_SetFocus",br_none, 1},
    {0,"U8TOU32",br_none, 1},
    {0,"__icache_invalidate_all",br_cache_flush , 1},
    {0,"LcdGetDisMode",br_LcdGetDisMode, 1},
    {0,"free_irq",br_none, 1},
    {0,"spin_lock_irqsave",br_none, 1},
    {0,"spin_unlock_irqrestore",br_none, 1},
    {0,"detect_clock",br_none, 1},
    {0,"udelay",br_udelay, 1},
    {0,"serial_putc",br_none, 1},
    {0,"serial_puts",br_none, 1},
    {0,"serial_getc",br_none, 1},
    {0,"TaskMediaFunStop",br_TaskMediaFunStop, 1},
    {0,"StartSwTimer",br_none, 1},
    {0,"GetTickCount",br_GetTickCount , 1},
    {0,"OSCPUSaveSR",br_none, 1},
    {0,"OSCPURestoreSR",br_none, 1},
    {0,"OSFlagPost",br_none, 1},
    {0,"OSQCreate",br_none, 1},
    {0,"OSSemDel",br_none , 0},
    {0,"OSSemPend",br_OSSemPend, 0},
    {0,"OSSemPost",br_OSSemPost, 0},
    {0,"OSSemCreate",br_OSSemCreate , 1},
    {0,"OSTaskCreate",br_OSTaskCreate , 1},
    {0,"OSTaskDel",br_OSTaskDel, 1},
    {0,"OSTimeDly",br_OSTimeDly , 0},
    {0,"U8TOU16",br_none, 1},
    {0,"_tcscmp",br_none, 1},
    {0,"_tcscpy",br_none, 1},
    {0,"__to_unicode_le",br__to_unicode_le, 1},
    {0,"__to_locale_ansi",br__to_locale_ansi , 1},
    {0,"udc_attached",br_none, 1},
};

bool bridge_try_fast_return_hook(uint32_t address, uint32_t* returnValue)
{
    for (int i = 0; i < sizeof(_hook_code_func_map) / sizeof(_hook_code_func_map[0]); ++i)
    {
        if (_hook_code_func_map[i].fast_return_enabled && _hook_code_func_map[i].offset == address)
        {
            uint32_t restoreGeneration = pauseGateRestoreGeneration();
            if (pauseGateWaitForResume() &&
                restoreGeneration != pauseGateRestoreGeneration())
            {
                return false;
            }
            _hook_code_func_map[i].trigger_times++;
            _hook_code_func_map[i].profile_times++;
            if (returnValue)
            {
                *returnValue = _hook_code_func_map[i].fast_return_value;
            }
            return true;
        }
    }
    return false;
}

bool bridge_lookup_hook_address(const char* name, uint32_t* address)
{
    if (!name || !address)
    {
        return false;
    }

    for (int i = 0; i < sizeof(_hook_code_func_map) / sizeof(_hook_code_func_map[0]); ++i)
    {
        if (_hook_code_func_map[i].offset && strcmp(_hook_code_func_map[i].name, name) == 0)
        {
            *address = _hook_code_func_map[i].offset;
            return true;
        }
    }
    return false;
}

static void storeGuestLe32(NativeRuntime* runtime, uint32_t address, uint32_t value)
{
    uint8_t bytes[4];
    bytes[0] = (uint8_t)(value & 0xff);
    bytes[1] = (uint8_t)((value >> 8) & 0xff);
    bytes[2] = (uint8_t)((value >> 16) & 0xff);
    bytes[3] = (uint8_t)((value >> 24) & 0xff);
    nativeRuntimeWriteMemory(runtime, address, bytes, sizeof(bytes));
}

static bool fastReturnPatchEnabled(void)
{
    static int enabled = -1;
    if (enabled < 0)
    {
        const char* value = getenv("DINGOO_PIE_PATCH_FAST_RETURNS");
        enabled = (!value || !value[0] || strcmp(value, "0") != 0) ? 1 : 0;
    }
    return enabled != 0;
}

static bool installFastReturnStub(NativeRuntime* runtime, uint32_t address, uint32_t value)
{
    if (!runtime || !fastReturnPatchEnabled())
    {
        return false;
    }

    if (value <= 0xffffu)
    {
        // addiu v0, zero, imm; jr ra; nop
        storeGuestLe32(runtime, address, 0x24020000u | (value & 0xffffu));
        storeGuestLe32(runtime, address + 4, 0x03e00008u);
        storeGuestLe32(runtime, address + 8, 0x00000000u);
    }
    else
    {
        // lui v0, hi; ori v0, v0, lo; jr ra; nop
        storeGuestLe32(runtime, address, 0x3c020000u | ((value >> 16) & 0xffffu));
        storeGuestLe32(runtime, address + 4, 0x34420000u | (value & 0xffffu));
        storeGuestLe32(runtime, address + 8, 0x03e00008u);
        storeGuestLe32(runtime, address + 12, 0x00000000u);
    }
    return true;
}

static void profilePrintHookTopAndReset(void)
{
    struct TopHook
    {
        const char* name;
        uint32_t count;
    };

    TopHook top[5] = {};
    for (int i = 0; i < sizeof(_hook_code_func_map) / sizeof(_hook_code_func_map[0]); ++i)
    {
        uint32_t count = _hook_code_func_map[i].profile_times;
        if (!count)
        {
            continue;
        }

        for (int slot = 0; slot < 5; ++slot)
        {
            if (count > top[slot].count)
            {
                for (int move = 4; move > slot; --move)
                {
                    top[move] = top[move - 1];
                }
                top[slot].name = _hook_code_func_map[i].name;
                top[slot].count = count;
                break;
            }
        }
    }

    printf("profile hle top:");
    for (int i = 0; i < 5 && top[i].count; ++i)
    {
        printf(" %s=%u", top[i].name ? top[i].name : "<unnamed>", top[i].count);
    }
    printf("\n");

    for (int i = 0; i < sizeof(_hook_code_func_map) / sizeof(_hook_code_func_map[0]); ++i)
    {
        _hook_code_func_map[i].profile_times = 0;
    }
}

pthread_mutex_t hook_code_mutex = PTHREAD_MUTEX_INITIALIZER;

static void hook_code(NativeRuntime* runtime, uint64_t address, uint32_t size, void* user_data)
{
    (void)size;
    static uint64_t lastTicks = 0;
    static uint64_t bridgeCalls = 0;
    if (s_bridgeProfileEnabled.load())
    {
        uint64_t now = SDL_GetTicks64();
        if (!lastTicks)
        {
            lastTicks = now;
        }
        bridgeCalls++;
        if (now - lastTicks >= 1000)
        {
            printf("profile bridge: calls=%llu/s\n", (unsigned long long)bridgeCalls);
            profilePrintAndReset(now);
            profilePrintHookTopAndReset();
            bridgeCalls = 0;
            lastTicks = now;
        }
    }

    struct _hook_code_func_* hookFunc = (struct _hook_code_func_*)user_data;
    if (!hookFunc || hookFunc->offset != address)
    {
        return;
    }
    // Some games keep running through audio/input/timer SDK calls without
    // submitting a new frame. Gate every resolved HLE hook so pause is not
    // limited to LCD frame boundaries.
    uint32_t restoreGeneration = pauseGateRestoreGeneration();
    if (pauseGateWaitForResume() &&
        restoreGeneration != pauseGateRestoreGeneration())
    {
        uint32_t currentPc = address;
        nativeRuntimeReadRegister(runtime, RUNTIME_REG_PC, &currentPc);
        if (currentPc != address)
        {
            return;
        }
    }

    if (hookFunc->name)
    {
        if (strcmp(hookFunc->name, "waveout_write") == 0)
        {
            uint32_t instPtr;
            uint32_t bufferPtr;
            uint32_t count;
            nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &instPtr);
            nativeRuntimeReadRegister(runtime, RUNTIME_REG_A1, &bufferPtr);
            nativeRuntimeReadRegister(runtime, RUNTIME_REG_A2, &count);
            uint32_t ret = 1;
            if (bridge_fast_waveout_write(instPtr, bufferPtr, count, &ret))
            {
                nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &ret);
                returnToRa(runtime);
                return;
            }
        }
        else if (strcmp(hookFunc->name, "waveout_can_write") == 0)
        {
            uint32_t ret = bridge_fast_waveout_can_write();
            nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &ret);
            returnToRa(runtime);
            return;
        }
        else if (strcmp(hookFunc->name, "OSSemPend") == 0)
        {
            uint32_t eventVal;
            uint32_t timeout;
            uint32_t errorPtr;
            nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &eventVal);
            nativeRuntimeReadRegister(runtime, RUNTIME_REG_A1, &timeout);
            nativeRuntimeReadRegister(runtime, RUNTIME_REG_A2, &errorPtr);
            if (bridge_fast_os_sem_pend(eventVal, timeout, errorPtr))
            {
                uint32_t ret = OS_NO_ERR;
                nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &ret);
                returnToRa(runtime);
                return;
            }
        }
        else if (strcmp(hookFunc->name, "OSSemPost") == 0)
        {
            uint32_t eventVal;
            nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &eventVal);
            uint32_t retVal = OS_NO_ERR;
            if (bridge_fast_os_sem_post(eventVal, &retVal))
            {
                nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &retVal);
                returnToRa(runtime);
                return;
            }
        }
    }

    if (hookFunc->func)
    {
        hookFunc->trigger_times++;
        hookFunc->profile_times++;
        if (hookFunc->fast_return_enabled)
        {
            nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &hookFunc->fast_return_value);
            returnToRa(runtime);
            return;
        }

        uint32_t ra = 0;
        uint32_t pc = 0;
        uint32_t a0 = 0;
        nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &ra);
        nativeRuntimeReadRegister(runtime, RUNTIME_REG_PC, &pc);
        nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &a0);
        snprintf(s_lastHleSummary, sizeof(s_lastHleSummary),
            "%s pc=0x%08x hook=0x%08x ra=0x%08x a0=0x%08x",
            hookFunc->name ? hookFunc->name : "<unnamed>",
            pc, (uint32_t)address, ra, a0);

        if (hookFunc->lock)
        {
            pthread_mutex_lock(&hook_code_mutex);
        }
        hookFunc->func(runtime);

        if (hookFunc->lock)
        {
            pthread_mutex_unlock(&hook_code_mutex);
        }
    }
    else
    {
        printf("hle: missing implementation for %s at 0x%08x\n",
            hookFunc->name ? hookFunc->name : "<unnamed>", (uint32_t)address);
        dumpREG(runtime);
        dumpStackCall(runtime, 0xa0000000);
        dumpAsm(runtime);
        frontendRequestQuit();
    }
}

void nativeRuntimeInterruptHook(NativeRuntime* runtime, uint32_t intno, void* user_data)
{
    (void)runtime;
    (void)intno;
    (void)user_data;
}

static void hooks_init(NativeRuntime* runtime, app* _app)
{
    bridge_apply_runtime_settings();
    RuntimeError err;
    RuntimeHook trace;
    uint32_t hookCount = 0;
    uint32_t unknownCount = 0;

    for (int i = 0; i < _app->import_count; ++i)
    {
        app_import_entry* entry = _app->import_data[i];
        const char* name = entry->name;
        bool matched = false;
        for (int j = 0; j < sizeof(_hook_code_func_map) / sizeof(_hook_code_func_map[0]); ++j)
        {
            if (strcmp(name, _hook_code_func_map[j].name) == 0)
            {
                _hook_code_func_map[j].offset = entry->offset;
                _hook_code_func_map[j].trigger_times = 0;
                if (_hook_code_func_map[j].fast_return_enabled &&
                    installFastReturnStub(runtime, entry->offset, _hook_code_func_map[j].fast_return_value))
                {
                    hookCount++;
                    matched = true;
                    break;
                }
                err = nativeRuntimeAddHook(runtime, &trace, RUNTIME_HOOK_CODE, (void*)hook_code,
                    (void*)&_hook_code_func_map[j], entry->offset, entry->offset, 0);
                if (err != RUNTIME_OK)
                {
                    printf("add hook err %u (%s)\n", err, nativeRuntimeErrorString(err));
                    return;
                }
                hookCount++;
                matched = true;
                break;
            }
        }
        if (!matched)
        {
            unknownCount++;
        }
    }

    if (s_bridgeProfileEnabled.load())
    {
        printf("profile bridge: direct hooks=%u unknown_imports=%u\n", hookCount, unknownCount);
    }
}

RuntimeError bridge_init(NativeRuntime* runtime, app* _app)
{
    return bridge_init_task(runtime, _app, true);
}

RuntimeError bridge_init_task(NativeRuntime* runtime, app* _app, bool isMainRuntime)
{
    s_bridgeApp = _app;
    if (isMainRuntime)
    {
        s_tempTicks = 0;
    }
    fsys_set_app(_app);
    registerRuntimeContext(runtime, isMainRuntime);
	hooks_init(runtime, _app);

	return RUNTIME_OK;
}

