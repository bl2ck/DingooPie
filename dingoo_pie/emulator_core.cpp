#include "emulator_core.h"

#include "app_loader.h"
#include "app_paths.h"
#include "debug_console.h"
#include "emulator_settings.h"
#include "sdk_hle.h"
#include "framebuffer.h"
#include "guest_format.h"
#include "compat_profile.h"
#include "platform_win32.h"
#include "instruction_compat.h"
#include "emulated_memory.h"
#include "ppsspp_irjit_backend.h"
#include "sdl_frontend.h"
#include "task_scheduler.h"
#include "guest_filesystem.h"
#include "Common/Crypto/sha256.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <atomic>
#include <chrono>
#include <string>
#include <string.h>
#include <thread>
#include "native_runtime.h"

static std::string g_appLoadPath;
static std::string g_appMainPath;
static EmulatorOptions g_options;
static bool g_clearRecentOnStartupFailure = false;
static pthread_mutex_t g_runtimeThreadMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_runtimeThread;
static bool g_runtimeThreadStarted = false;
static NativeRuntime* g_mainRuntime = NULL;
static std::atomic<bool> g_runtimeStopRequested(false);
static std::atomic<bool> g_lastRunExitedNormally(false);
static std::string g_lastRunAppPath;

uint32_t s_AppDataAddr = 0;
uint32_t s_AppDataBuffSize = 0;
void* s_AppDataBuff = 0;
app* s_app = NULL;

static uint32_t g_appMainEntry = 0;
static uint32_t g_appMainInitCheckAddress = 0;

static void clearMainRuntimeIfCurrent(NativeRuntime* runtime)
{
    pthread_mutex_lock(&g_runtimeThreadMutex);
    if (g_mainRuntime == runtime)
    {
        g_mainRuntime = NULL;
    }
    pthread_mutex_unlock(&g_runtimeThreadMutex);
}

static void destroyMainRuntime(NativeRuntime* runtime)
{
    if (!runtime)
    {
        return;
    }
    clearMainRuntimeIfCurrent(runtime);
    nativeRuntimeDestroy(runtime);
}

static std::string sha256Hex(const uint8_t* data, uint32_t size)
{
    static const char kHex[] = "0123456789ABCDEF";
    uint8_t digest[32];
    sha256_context context;
    sha256_starts(&context);
    sha256_update(&context, data, size);
    sha256_finish(&context, digest);

    std::string out;
    out.resize(64);
    for (size_t i = 0; i < sizeof(digest); ++i)
    {
        out[i * 2] = kHex[digest[i] >> 4];
        out[i * 2 + 1] = kHex[digest[i] & 0x0f];
    }
    return out;
}

static app* loadApp(const char* appPath)
{
    std::string path = appNormalizePath(appPath);

#ifdef _WIN32
    std::wstring pathW = platformUtf8ToWide(path);
    FILE* file = _wfopen(pathW.c_str(), L"rb");
#else
    FILE* file = fopen(path.c_str(), "rb");
#endif

    if (!file)
    {
        printf("DingooPie: failed to open app: %s\n", path.c_str());
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    uintptr_t fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);

    app* loadedApp = app_create(file, (uint32_t)fileSize);
    fclose(file);

    if (!loadedApp)
    {
        printf("DingooPie: failed to parse app: %s\n", path.c_str());
        return NULL;
    }

    return loadedApp;
}

static bool appPathsMatch(const std::string& a, const std::string& b)
{
#ifdef _WIN32
    return _stricmp(appNormalizePath(a.c_str()).c_str(), appNormalizePath(b.c_str()).c_str()) == 0;
#else
    return appNormalizePath(a.c_str()) == appNormalizePath(b.c_str());
#endif
}

static void clearRecentAppIfStillCurrent(const char* reason)
{
    if (!g_clearRecentOnStartupFailure || g_appLoadPath.empty())
    {
        return;
    }

    EmulatorSettings settings = emulatorLoadSettings();
    if (settings.lastAppPath.empty() || !appPathsMatch(settings.lastAppPath, g_appLoadPath))
    {
        printf("DingooPie: recent app was already changed; not clearing after %s\n", reason);
        return;
    }

    emulatorRemoveRecentApp(&settings, g_appLoadPath);
    if (emulatorSaveSettings(settings))
    {
        printf("DingooPie: cleared recent app after %s: %s\n", reason, g_appLoadPath.c_str());
    }
    else
    {
        printf("DingooPie: failed to clear recent app after %s: %s\n", reason, g_appLoadPath.c_str());
    }
}

static void saveRecentAppPath(const std::string& appPath, const char* reason)
{
    if (appPath.empty())
    {
        return;
    }

    EmulatorSettings settings = emulatorLoadSettings();
    if (!emulatorRememberRecentApp(&settings, appPath))
    {
        printf("DingooPie: recent app already current after %s: %s\n", reason, appPath.c_str());
        return;
    }

    if (emulatorSaveSettings(settings))
    {
        printf("DingooPie: saved recent app after %s: %s\n", reason, appPath.c_str());
    }
    else
    {
        printf("DingooPie: failed to save recent app after %s: %s\n", reason, appPath.c_str());
    }
}

static bool joinRuntimeThreadWithTimeout(pthread_t tid, uint32_t timeoutMs)
{
#ifdef _WIN32
    // The PPSSPP JIT can occasionally stop polling after a guest-side quit path
    // fails. Keep frontend shutdown bounded instead of blocking the UI forever.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    void* result = NULL;
    for (;;)
    {
        int ret = _pthread_tryjoin(tid, &result);
        if (ret == 0)
        {
            return true;
        }
        if (std::chrono::steady_clock::now() >= deadline)
        {
            printf("DingooPie: runtime thread join timed out after %u ms ret=%d\n",
                timeoutMs, ret);
            pthread_detach(tid);
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
#else
    (void)timeoutMs;
    pthread_join(tid, NULL);
    return true;
#endif
}

static void hookDingooPieDebug(NativeRuntime* runtime, uint64_t address, uint32_t size, void* userData)
{
    (void)address;
    (void)size;
    (void)userData;
    dingoo_debug(runtime);
}

static bool hookMemInvalid(NativeRuntime* runtime, RuntimeMemoryAccess type, uint64_t address, int size, int64_t value, void* userData)
{
    (void)userData;
    FILE* debugLog = debugLogFile();

    fprintf(debugLog, ">>> mem_invalid type:%s addr:0x%" PRIx64 " size:0x%x value:0x%" PRIx64 "\n",
        memTypeStr(type), address, size, value);
    dumpREG2File(runtime, debugLog);
    dumpAsm(runtime);

    printf(">>> mem_invalid type:%s addr:0x%" PRIx64 " size:0x%x value:0x%" PRIx64 "\n",
        memTypeStr(type), address, size, value);
    dumpREG(runtime);
    return false;
}

static void hookForceAppInitSuccess(NativeRuntime* runtime, uint64_t address, uint32_t size, void* userData)
{
    (void)address;
    (void)size;
    (void)userData;

    uint32_t forceSuccess = 1;
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &forceSuccess);
}

static uint32_t parseHexEnvValue(const char* name)
{
    const char* value = getenv(name);
    if (!value || !value[0])
    {
        return 0;
    }

    return (uint32_t)strtoul(value, NULL, 0);
}

static bool traceCopyOverlaps(uint32_t address, uint32_t size)
{
    static bool initialized = false;
    static bool enabled = false;
    static uint32_t traceStart = 0;
    static uint32_t traceEnd = 0;

    if (!initialized)
    {
        enabled = getenv("DINGOO_PIE_TRACE_COPY") != NULL;
        traceStart = parseHexEnvValue("DINGOO_PIE_TRACE_MEM_START");
        traceEnd = parseHexEnvValue("DINGOO_PIE_TRACE_MEM_END");
        initialized = true;
    }

    if (!enabled)
    {
        return false;
    }
    if (!traceStart || !traceEnd)
    {
        return true;
    }

    uint64_t begin = address;
    uint64_t end = begin + size;
    return begin < traceEnd && end > traceStart;
}

static void hookInternalMemcpy(NativeRuntime* runtime, uint64_t address, uint32_t size, void* userData)
{
    (void)address;
    (void)size;
    (void)userData;

    uint32_t dstPtr = 0;
    uint32_t srcPtr = 0;
    uint32_t length = 0;
    uint32_t ra = 0;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &dstPtr);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A1, &srcPtr);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A2, &length);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &ra);

    if (length == 0)
    {
        // Zero-length copies are no-ops even when the guest passes placeholder pointers.
        nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &dstPtr);
        nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &ra);
        return;
    }

    void* dst = toHostPtr(dstPtr);
    void* src = toHostPtr(srcPtr);
    if (!dst || !src)
    {
        printf("DingooPie: internal memcpy invalid dst=0x%08x src=0x%08x len=%u\n",
            dstPtr, srcPtr, length);
        frontendRequestQuit();
        return;
    }

    if (traceCopyOverlaps(dstPtr, length) || traceCopyOverlaps(srcPtr, length))
    {
        printf("trace-copy: pc=0x%08x dst=0x%08x src=0x%08x len=%u ra=0x%08x\n",
            (uint32_t)address, dstPtr, srcPtr, length, ra);
        printf("trace-copy-src:\n");
        dumpMem(src, length < 0x40 ? length : 0x40);
    }

    memmove(dst, src, length);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &dstPtr);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &ra);
}

static void hookAppMain(NativeRuntime* runtime, uint64_t address, uint32_t size, void* userData)
{
    (void)size;
    RuntimeError err;

    std::wstring appPathW = platformUtf8ToWide(g_appMainPath);
    uint32_t pathBytes = (uint32_t)((appPathW.size() + 1) * sizeof(wchar_t));
    uint32_t pathPtr = vm_malloc(pathBytes);
    if (!pathPtr)
    {
        printf("DingooPie: vm_malloc failed for AppMain path, size=%u\n", pathBytes);
        exit(1);
    }

    err = nativeRuntimeWriteMemory(runtime, pathPtr, appPathW.c_str(), pathBytes);
    if (err)
    {
        printf("DingooPie: nativeRuntimeWriteMemory(AppMain path) failed: %u (%s)\n", err, nativeRuntimeErrorString(err));
        exit(1);
    }

    err = nativeRuntimeWriteRegister(runtime, RUNTIME_REG_A0, &pathPtr);
    if (err)
    {
        printf("DingooPie: nativeRuntimeWriteRegister(A0) failed: %u (%s)\n", err, nativeRuntimeErrorString(err));
        exit(1);
    }

    uint32_t appMainEntry = (uint32_t)address;
    err = nativeRuntimeWriteRegister(runtime, RUNTIME_REG_T9, &appMainEntry);
    if (err)
    {
        printf("DingooPie: nativeRuntimeWriteRegister(T9) failed: %u (%s)\n", err, nativeRuntimeErrorString(err));
        exit(1);
    }

    RuntimeHook* hookHandle = (RuntimeHook*)userData;
    err = nativeRuntimeRemoveHook(runtime, *hookHandle);
    if (err)
    {
        printf("DingooPie: nativeRuntimeRemoveHook(AppMain) failed: %u (%s)\n", err, nativeRuntimeErrorString(err));
        exit(1);
    }

    free(userData);
}

static bool mapAppMemory(NativeRuntime* runtime, app* loadedApp)
{
    s_AppDataAddr = loadedApp->origin;
    s_AppDataBuffSize = loadedApp->bin_size;
    s_AppDataBuff = loadedApp->bin_data;
    s_app = loadedApp;

    RuntimeError err = nativeRuntimeMapMemory(runtime, s_AppDataAddr, s_AppDataBuffSize, RUNTIME_PROT_ALL, s_AppDataBuff);
    if (err)
    {
        printf("DingooPie: failed to map app memory: %u (%s)\n", err, nativeRuntimeErrorString(err));
        return false;
    }

    uint32_t aliasAddr = s_AppDataAddr & 0x1fffffff;
    err = nativeRuntimeMapMemory(runtime, aliasAddr, s_AppDataBuffSize, RUNTIME_PROT_ALL, s_AppDataBuff);
    if (err)
    {
        printf("DingooPie: failed to map app alias: %u (%s)\n", err, nativeRuntimeErrorString(err));
        return false;
    }

    return true;
}

static uint32_t findAppMainEntry(app* loadedApp)
{
    for (uint32_t i = 0; i < loadedApp->export_count; i++)
    {
        if (strcmp(loadedApp->export_data[i]->name, "AppMain") == 0)
        {
            return loadedApp->export_data[i]->offset;
        }
    }

    return 0;
}

static bool installCoreHooks(NativeRuntime* runtime, app* loadedApp, uint32_t appMainEntry, RuntimeHook** appMainHook)
{
    RuntimeError err;
    RuntimeHook trace;

    err = nativeRuntimeAddHook(runtime, &trace, RUNTIME_HOOK_MEM_INVALID, (void*)hookMemInvalid, NULL, 1, 0);
    if (err != RUNTIME_OK)
    {
        printf("DingooPie: failed to add mem invalid hook: %u (%s)\n", err, nativeRuntimeErrorString(err));
        return false;
    }

    nativeRuntimeAddHook(runtime, &trace, RUNTIME_HOOK_CODE, (void*)hookDingooPieDebug, NULL, 0x80B0F060, 0x80B0F060);

    err = runtimeCompatInstallHooks(runtime, loadedApp, g_options);
    if (err != RUNTIME_OK)
    {
        printf("DingooPie: failed to add runtime compat hook: %u (%s)\n", err, nativeRuntimeErrorString(err));
        return false;
    }

    g_appMainEntry = appMainEntry;
    g_appMainInitCheckAddress = appMainEntry + 0x34;

    err = nativeRuntimeAddHook(runtime, &trace, RUNTIME_HOOK_CODE, (void*)hookForceAppInitSuccess, NULL,
        g_appMainInitCheckAddress, g_appMainInitCheckAddress, 0);
    if (err != RUNTIME_OK)
    {
        printf("DingooPie: failed to add AppMain init hook: %u (%s)\n", err, nativeRuntimeErrorString(err));
        return false;
    }

    *appMainHook = (RuntimeHook*)malloc(sizeof(RuntimeHook));
    if (!*appMainHook)
    {
        printf("DingooPie: failed to allocate AppMain hook handle\n");
        return false;
    }

    err = nativeRuntimeAddHook(runtime, *appMainHook, RUNTIME_HOOK_CODE, (void*)hookAppMain, (void*)*appMainHook,
        appMainEntry, appMainEntry, 0);
    if (err != RUNTIME_OK)
    {
        printf("DingooPie: failed to add AppMain hook: %u (%s)\n", err, nativeRuntimeErrorString(err));
        free(*appMainHook);
        *appMainHook = NULL;
        return false;
    }

    return true;
}

static void logEmulationFailure(NativeRuntime* runtime, RuntimeError err)
{
    printf("DingooPie: native runtime failed: %u (%s)\n", err, nativeRuntimeErrorString(err));
    FILE* debugLog = debugLogFile();
    if (debugLog)
    {
        fprintf(debugLog, "native runtime failed: %u (%s)\n", err, nativeRuntimeErrorString(err));
        dumpREG2File(runtime, debugLog);
        fflush(debugLog);
    }

    dumpREG(runtime);

    uint32_t failPc = 0;
    uint32_t failRa = 0;
    uint32_t failS4 = 0;
    uint32_t failV0 = 0;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_PC, &failPc);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &failRa);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_S4, &failS4);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_V0, &failV0);
    printf("DingooPie: fail object s4=0x%08x callback=0x%08x\n", failS4, failV0);
    if (failPc >= 0x20)
    {
        dumpAsmRange(runtime, failPc - 0x20, 0x80);
    }
    if (failRa >= 0x20)
    {
        dumpAsmRange(runtime, failRa - 0x20, 0x80);
    }
    if (failS4)
    {
        printf("DingooPie: object dump:\n");
        dumpMem(toHostPtr(failS4), 0x40);
    }
    if (failV0)
    {
        printf("DingooPie: callback target dump:\n");
        dumpMem(toHostPtr(failV0), 0x80);
    }
    if (getenv("DINGOO_PIE_TRACE_FAIL_MODULE") && failS4)
    {
        uint32_t moduleHeader = failS4 + 0x58;
        uint32_t moduleRaw = 0;
        uint32_t moduleEntry = 0;
        uint32_t moduleRawSize = 0;
        uint32_t moduleProgSize = 0;
        void* header = toHostPtr(moduleHeader);
        if (header)
        {
            uint8_t* bytes = (uint8_t*)header;
            moduleEntry = *(uint32_t*)(bytes + 0x60 + 0x14);
            moduleRaw = *(uint32_t*)(bytes + 0x60 + 0x18);
            moduleProgSize = *(uint32_t*)(bytes + 0x60 + 0x1c);
            moduleRawSize = *(uint32_t*)(bytes + 0x60 + 0x0c);
            printf("DingooPie: fail module header at 0x%08x:\n", moduleHeader);
            dumpMem(header, 0xa0);
            printf("DingooPie: fail module raw=0x%08x entry=0x%08x raw_size=0x%08x prog_size=0x%08x\n",
                moduleRaw, moduleEntry, moduleRawSize, moduleProgSize);
        }
    }
    dumpAsm(runtime);
}

static CompatGuestExitDecision noGuestExitDecision(void)
{
    CompatGuestExitDecision decision;
    decision.matched = false;
    decision.shouldExit = false;
    decision.label = NULL;
    return decision;
}

static CompatGuestExitDecision runtimeExceptionGuestExitDecision(NativeRuntime* runtime, const std::string& appSha256)
{
    CompatRuntimeExceptionExitContext context;
    context.pc = 0;
    context.returnAddress = 0;
    context.v0 = 0;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_PC, &context.pc);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &context.returnAddress);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_V0, &context.v0);

    return compatRuntimeExceptionGuestExitDecision(appSha256.c_str(), &context);
}

static NativeRuntime* initDingooPie(void)
{
    taskSchedulerResetShutdown();

    NativeRuntime* runtime;
    RuntimeError err = nativeRuntimeCreate(&runtime);
    if (err)
    {
        printf("DingooPie: nativeRuntimeCreate failed: %u (%s)\n", err, nativeRuntimeErrorString(err));
        return NULL;
    }
    pthread_mutex_lock(&g_runtimeThreadMutex);
    g_mainRuntime = runtime;
    pthread_mutex_unlock(&g_runtimeThreadMutex);
    if (g_runtimeStopRequested.load(std::memory_order_acquire))
    {
        nativeRuntimeRequestStop(runtime);
    }

    app* loadedApp = loadApp(g_appLoadPath.c_str());
    if (!loadedApp)
    {
        destroyMainRuntime(runtime);
        return NULL;
    }
    std::string appSha256 = sha256Hex(loadedApp->file_data, loadedApp->file_size);
    bridge_set_app_identity(appSha256.c_str());
    fsys_set_app_identity(appSha256.c_str());
    printf("DingooPie: app sha256: %s\n", appSha256.c_str());
    printf("DingooPie: compat profile: %s\n", compatProfileName(appSha256.c_str()));
    ppssppShimSetFastMemoryOverride(-1);

    ExecutionBackend effectiveBackend = g_options.backend;
    if (compatForcedBackend(appSha256.c_str(), &effectiveBackend))
    {
        printf("DingooPie: compat backend override: requested=%s effective=%s\n",
            executionBackendName(g_options.backend), executionBackendName(effectiveBackend));
    }

    err = nativeRuntimeSetBackend(runtime, effectiveBackend);
    if (err)
    {
        printf("DingooPie: nativeRuntimeSetBackend failed: %u (%s)\n", err, nativeRuntimeErrorString(err));
        destroyMainRuntime(runtime);
        return NULL;
    }
    printf("DingooPie: execution backend effective: %s\n", executionBackendName(effectiveBackend));

    if (!mapAppMemory(runtime, loadedApp))
    {
        destroyMainRuntime(runtime);
        return NULL;
    }

    printf("DingooPie: init bridge begin\n");
    err = bridge_init(runtime, loadedApp);
    if (err)
    {
        printf("DingooPie: bridge_init failed: %u (%s)\n", err, nativeRuntimeErrorString(err));
        destroyMainRuntime(runtime);
        return NULL;
    }
    printf("DingooPie: init bridge done\n");

    printf("DingooPie: init vm memory begin\n");
    if (InitVmMem(runtime, loadedApp))
    {
        printf("DingooPie: InitVmMem failed\n");
        destroyMainRuntime(runtime);
        return NULL;
    }
    printf("DingooPie: init vm memory done\n");

    printf("DingooPie: init framebuffer begin\n");
    if (InitFb(runtime))
    {
        printf("DingooPie: InitFb failed\n");
        destroyMainRuntime(runtime);
        return NULL;
    }
    printf("DingooPie: init framebuffer done\n");

    uint32_t value = 0;
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_ZERO, &value);

    uint32_t appMainEntry = findAppMainEntry(loadedApp);
    if (!appMainEntry)
    {
        printf("DingooPie: AppMain export not found\n");
        destroyMainRuntime(runtime);
        return NULL;
    }
    printf("DingooPie: AppMain entry=0x%08x boot entry=0x%08x origin=0x%08x size=0x%08x\n",
        appMainEntry, loadedApp->bin_entry, loadedApp->origin, loadedApp->bin_size);

    RuntimeHook* appMainHook = NULL;
    printf("DingooPie: install core hooks begin\n");
    if (!installCoreHooks(runtime, loadedApp, appMainEntry, &appMainHook))
    {
        destroyMainRuntime(runtime);
        return NULL;
    }
    printf("DingooPie: install core hooks done\n");

    err = nativeRuntimeWriteRegister(runtime, RUNTIME_REG_RA, &appMainEntry);
    if (err)
    {
        printf("DingooPie: nativeRuntimeWriteRegister(RA) failed: %u (%s)\n", err, nativeRuntimeErrorString(err));
        destroyMainRuntime(runtime);
        return NULL;
    }

    uint32_t mallocLcdBuffer = 0;
    err = nativeRuntimeWriteRegister(runtime, RUNTIME_REG_A1, &mallocLcdBuffer);
    if (err)
    {
        printf("DingooPie: nativeRuntimeWriteRegister(A1) failed: %u (%s)\n", err, nativeRuntimeErrorString(err));
        destroyMainRuntime(runtime);
        return NULL;
    }

    err = nativeRuntimeWriteRegister(runtime, RUNTIME_REG_T9, &loadedApp->bin_entry);
    if (err)
    {
        printf("DingooPie: nativeRuntimeWriteRegister(T9 entry) failed: %u (%s)\n", err, nativeRuntimeErrorString(err));
        destroyMainRuntime(runtime);
        return NULL;
    }

    printf("DingooPie: native runtime start pc=0x%08x until=0xffffffff\n", loadedApp->bin_entry);
    err = nativeRuntimeStart(runtime, loadedApp->bin_entry, 0xFFFFFFFF, 0, 0);
    printf("DingooPie: native runtime returned err=%u (%s)\n", err, nativeRuntimeErrorString(err));
    if (err)
    {
        CompatGuestExitDecision exitDecision = (err == RUNTIME_ERROR_EXCEPTION)
            ? runtimeExceptionGuestExitDecision(runtime, appSha256)
            : noGuestExitDecision();
        if (exitDecision.shouldExit)
        {
            printf("DingooPie: treating %s as normal guest exit\n",
                exitDecision.label ? exitDecision.label : "compat exception");
            g_lastRunAppPath = g_appLoadPath;
            g_lastRunExitedNormally.store(true, std::memory_order_release);
            frontendRequestQuit();
            return runtime;
        }
        g_lastRunExitedNormally.store(false, std::memory_order_release);
        logEmulationFailure(runtime, err);
        frontendRequestQuit();
        return runtime;
    }

    g_lastRunAppPath = g_appLoadPath;
    g_lastRunExitedNormally.store(true, std::memory_order_release);
    return runtime;
}

static void* dingoopieRun(void* data)
{
    (void)data;
    NativeRuntime* runtime = initDingooPie();
    if (!runtime)
    {
        clearRecentAppIfStillCurrent("startup failure");
    }
    printf("DingooPie: runtime thread exited runtime=%p\n", (void*)runtime);
    if (runtime)
    {
        destroyMainRuntime(runtime);
        printf("DingooPie: native runtime destroyed runtime=%p\n", (void*)runtime);
    }
    return 0;
}

bool startDingooPie(const char* appPath, const EmulatorOptions& options, bool clearRecentOnStartupFailure)
{
    g_runtimeStopRequested.store(false, std::memory_order_release);
    g_lastRunExitedNormally.store(false, std::memory_order_release);
    g_lastRunAppPath.clear();
    g_options = options;
    g_clearRecentOnStartupFailure = clearRecentOnStartupFailure;
    g_appLoadPath = appNormalizePath(appPath);
    if (g_appLoadPath.empty())
    {
        printf("DingooPie: no app path provided\n");
        return false;
    }
    g_appMainPath = appGuestMainPathFromPath(g_appLoadPath);

    printf("DingooPie: start app: %s\n", g_appLoadPath.c_str());
    printf("DingooPie: AppMain path: %s\n", g_appMainPath.c_str());
    printf("DingooPie: execution backend: %s\n", executionBackendName(options.backend));

    pthread_t tid;
    int ret = pthread_create(&tid, NULL, dingoopieRun, NULL);
    if (ret)
    {
        printf("DingooPie: pthread_create failed\n");
        assert(0);
        return false;
    }
    pthread_mutex_lock(&g_runtimeThreadMutex);
    g_runtimeThread = tid;
    g_runtimeThreadStarted = true;
    pthread_mutex_unlock(&g_runtimeThreadMutex);

    return true;
}

void stopDingooPie(void)
{
    pthread_t tid = {};
    NativeRuntime* runtime = NULL;
    bool shouldJoin = false;
    bool joinedRuntime = false;
    bool requestStop = !g_runtimeStopRequested.exchange(true, std::memory_order_acq_rel);

    pthread_mutex_lock(&g_runtimeThreadMutex);
    if (g_runtimeThreadStarted)
    {
        tid = g_runtimeThread;
        shouldJoin = true;
        g_runtimeThreadStarted = false;
    }
    runtime = g_mainRuntime;
    if (runtime && requestStop)
    {
        printf("DingooPie: stop requested runtime=%p\n", (void*)runtime);
        taskSchedulerRequestShutdown("frontend exit");
        nativeRuntimeRequestStop(runtime);
    }
    pthread_mutex_unlock(&g_runtimeThreadMutex);

    if (shouldJoin)
    {
        joinedRuntime = joinRuntimeThreadWithTimeout(tid, 5000);
        if (joinedRuntime)
        {
            printf("DingooPie: runtime thread joined\n");
        }
        else
        {
            printf("DingooPie: runtime thread left running during process shutdown\n");
        }
    }

    if (shouldJoin && !joinedRuntime)
    {
        printf("DingooPie: recent app not saved because runtime thread did not join\n");
    }
    else if (shouldJoin && !g_lastRunExitedNormally.load(std::memory_order_acquire))
    {
        printf("DingooPie: recent app not saved because runtime did not exit normally\n");
    }
    if (joinedRuntime && g_lastRunExitedNormally.load(std::memory_order_acquire))
    {
        saveRecentAppPath(g_lastRunAppPath, "normal runtime exit");
    }
}

