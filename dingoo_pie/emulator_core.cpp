#include "emulator_core.h"

#include "app_loader.h"
#include "app_paths.h"
#include "cheat_runtime.h"
#include "crash_log.h"
#include "debug_console.h"
#include "emulator_settings.h"
#include "sdk_hle.h"
#include "framebuffer.h"
#include "guest_format.h"
#include "compat_profile.h"
#include "pause_gate.h"
#include "platform_win32.h"
#include "instruction_compat.h"
#include "emulated_memory.h"
#include "ppsspp_irjit_backend.h"
#include "runtime_debug.h"
#include "runtime_resource_monitor.h"
#include "sdl_frontend.h"
#include "task_scheduler.h"
#include "guest_filesystem.h"
#include "Common/Crypto/sha256.h"

#include <algorithm>
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <atomic>
#include <chrono>
#include <list>
#include <string>
#include <string.h>
#include <thread>
#include <capstone/capstone.h>
#include "native_runtime.h"

static std::string g_appLoadPath;
static std::string g_appMainPath;
static std::string g_currentAppSha256;
static std::vector<std::string> g_enabledCheatFeatureKeys;
static EmulatorOptions g_options;
static bool g_clearRecentOnStartupFailure = false;
static pthread_mutex_t g_runtimeThreadMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_runtimeThread;
static bool g_runtimeThreadStarted = false;
static NativeRuntime* g_mainRuntime = NULL;
static std::atomic<bool> g_runtimeStopRequested(false);
static std::atomic<bool> g_lastRunExitedNormally(false);
static std::atomic<bool> g_suppressCurrentRunRecentAppSave(false);
static std::string g_lastRunAppPath;

uint32_t s_AppDataAddr = 0;
uint32_t s_AppDataBuffSize = 0;
void* s_AppDataBuff = 0;
app* s_app = NULL;

static uint32_t g_appMainEntry = 0;
static uint32_t g_appMainInitCheckAddress = 0;

struct RuntimeDebugHookEntry
{
    uint32_t address;
    uint32_t size;
    bool enabled;
    uint64_t hits;
    uint32_t lastPc;
    uint32_t lastAddress;
    uint32_t lastSize;
    uint64_t lastValue;
    RuntimeHook hook;
};

static pthread_mutex_t g_debuggerMutex = PTHREAD_MUTEX_INITIALIZER;
static std::list<RuntimeDebugHookEntry> g_debugPcHits;
static std::list<RuntimeDebugHookEntry> g_debugWriteHits;

static bool debuggerAutotestEnabled(void)
{
    static int enabled = -1;
    if (enabled < 0)
    {
        const char* value = getenv("DINGOO_PIE_DEBUGGER_AUTOTEST");
        enabled = value && value[0] && strcmp(value, "0") != 0 ? 1 : 0;
    }
    return enabled != 0;
}

static bool memorySearcherAutotestEnabled(void)
{
    static int enabled = -1;
    if (enabled < 0)
    {
        const char* value = getenv("DINGOO_PIE_MEMORY_SEARCHER_AUTOTEST");
        enabled = value && value[0] && strcmp(value, "0") != 0 ? 1 : 0;
    }
    return enabled != 0;
}

static bool rangesOverlap(uint32_t aStart, uint32_t aSize, uint32_t bStart, uint32_t bSize)
{
    uint64_t aEnd = (uint64_t)aStart + aSize;
    uint64_t bEnd = (uint64_t)bStart + bSize;
    return aStart < bEnd && bStart < aEnd;
}

static bool debuggerInclusiveRangeEnd(uint32_t address, uint32_t size, uint32_t* outEnd)
{
    if (size == 0)
    {
        return false;
    }

    uint64_t end = (uint64_t)address + (uint64_t)size - 1u;
    if (end > 0xffffffffull)
    {
        return false;
    }

    if (outEnd)
    {
        *outEnd = (uint32_t)end;
    }
    return true;
}

static uint32_t runtimeRegisterValue(NativeRuntime* runtime, int reg)
{
    if (!runtime)
    {
        return 0;
    }
    if (reg >= RUNTIME_REG_ZERO && reg <= RUNTIME_REG_RA)
    {
        uint32_t* gpr = nativeRuntimeGpr(runtime);
        return gpr ? gpr[reg] : 0;
    }
    if (reg == RUNTIME_REG_PC)
    {
        uint32_t* pc = nativeRuntimePc(runtime);
        return pc ? *pc : 0;
    }
    if (reg == RUNTIME_REG_HI)
    {
        uint32_t* hi = nativeRuntimeHi(runtime);
        return hi ? *hi : 0;
    }
    if (reg == RUNTIME_REG_LO)
    {
        uint32_t* lo = nativeRuntimeLo(runtime);
        return lo ? *lo : 0;
    }
    return 0;
}

static void debuggerCodeHook(NativeRuntime* runtime, uint64_t address, uint32_t size, void* userData)
{
    (void)size;
    RuntimeDebugHookEntry* entry = (RuntimeDebugHookEntry*)userData;
    if (!entry)
    {
        return;
    }

    pthread_mutex_lock(&g_debuggerMutex);
    if (entry->enabled)
    {
        entry->hits++;
        entry->lastPc = runtimeRegisterValue(runtime, RUNTIME_REG_PC);
        entry->lastAddress = (uint32_t)address;
        entry->lastSize = 4;
        entry->lastValue = 0;
        if (debuggerAutotestEnabled() && entry->hits == 1)
        {
            printf("debugger-autotest: pc-hit address=0x%08x pc=0x%08x hits=%llu\n",
                entry->address, entry->lastPc, (unsigned long long)entry->hits);
        }
    }
    pthread_mutex_unlock(&g_debuggerMutex);
}

static bool debuggerWriteHook(NativeRuntime* runtime, RuntimeMemoryAccess type, uint64_t address, int size, int64_t value, void* userData)
{
    RuntimeDebugHookEntry* entry = (RuntimeDebugHookEntry*)userData;
    if (!entry || type != RUNTIME_MEM_WRITE || size <= 0)
    {
        return false;
    }

    pthread_mutex_lock(&g_debuggerMutex);
    bool matched = entry->enabled &&
        rangesOverlap(entry->address, entry->size, (uint32_t)address, (uint32_t)size);
    if (matched)
    {
        entry->hits++;
        entry->lastPc = runtimeRegisterValue(runtime, RUNTIME_REG_PC);
        entry->lastAddress = (uint32_t)address;
        entry->lastSize = (uint32_t)size;
        entry->lastValue = (uint64_t)value;
        if (debuggerAutotestEnabled() && entry->hits == 1)
        {
            printf("debugger-autotest: write-hit range=0x%08x+0x%x pc=0x%08x write=0x%08x/%u value=0x%llx hits=%llu\n",
                entry->address, entry->size, entry->lastPc, entry->lastAddress,
                entry->lastSize, (unsigned long long)entry->lastValue,
                (unsigned long long)entry->hits);
        }
    }
    pthread_mutex_unlock(&g_debuggerMutex);
    return matched;
}

static void installDebuggerAutotestHooks(uint32_t pcHitAddress, uint32_t writeHitAddress, uint32_t writeHitSize)
{
    if (!debuggerAutotestEnabled())
    {
        return;
    }

    emulatorRuntimeClearPcHits();
    emulatorRuntimeClearWriteHits();
    bool pcHitOk = emulatorRuntimeAddPcHit(pcHitAddress);
    bool writeHitOk = emulatorRuntimeAddWriteHit(writeHitAddress, writeHitSize);
    printf("debugger-autotest: installed pc_hit=0x%08x ok=%u write_hit=0x%08x+0x%x ok=%u\n",
        pcHitAddress, pcHitOk ? 1u : 0u, writeHitAddress, writeHitSize, writeHitOk ? 1u : 0u);
}

static void uninstallDebuggerHooksLocked(NativeRuntime* runtime)
{
    if (!runtime)
    {
        return;
    }
    for (std::list<RuntimeDebugHookEntry>::iterator it = g_debugPcHits.begin();
        it != g_debugPcHits.end(); ++it)
    {
        if (it->hook)
        {
            nativeRuntimeRemoveHook(runtime, it->hook);
            it->hook = 0;
        }
    }
    for (std::list<RuntimeDebugHookEntry>::iterator it = g_debugWriteHits.begin();
        it != g_debugWriteHits.end(); ++it)
    {
        if (it->hook)
        {
            nativeRuntimeRemoveHook(runtime, it->hook);
            it->hook = 0;
        }
    }
}

static void installDebuggerHooksLocked(NativeRuntime* runtime)
{
    if (!runtime)
    {
        return;
    }
    for (std::list<RuntimeDebugHookEntry>::iterator it = g_debugPcHits.begin();
        it != g_debugPcHits.end();)
    {
        if (!it->enabled && !it->hook)
        {
            it = g_debugPcHits.erase(it);
        }
        else
        {
            ++it;
        }
    }
    for (std::list<RuntimeDebugHookEntry>::iterator it = g_debugWriteHits.begin();
        it != g_debugWriteHits.end();)
    {
        if (!it->enabled && !it->hook)
        {
            it = g_debugWriteHits.erase(it);
        }
        else
        {
            ++it;
        }
    }
    for (std::list<RuntimeDebugHookEntry>::iterator it = g_debugPcHits.begin();
        it != g_debugPcHits.end(); ++it)
    {
        RuntimeDebugHookEntry& entry = *it;
        if (entry.enabled && !entry.hook)
        {
            nativeRuntimeAddHook(runtime, &entry.hook, RUNTIME_HOOK_CODE,
                (void*)debuggerCodeHook, &entry, entry.address, entry.address);
        }
    }
    for (std::list<RuntimeDebugHookEntry>::iterator it = g_debugWriteHits.begin();
        it != g_debugWriteHits.end(); ++it)
    {
        RuntimeDebugHookEntry& entry = *it;
        if (entry.enabled && !entry.hook)
        {
            uint32_t end = 0;
            if (!debuggerInclusiveRangeEnd(entry.address, entry.size, &end))
            {
                entry.enabled = false;
                continue;
            }
            nativeRuntimeAddHook(runtime, &entry.hook, RUNTIME_HOOK_MEM_VALID,
                (void*)debuggerWriteHook, &entry, entry.address, end);
        }
    }
}

static EmulatorRuntimeDebugEntry exportDebugEntry(const RuntimeDebugHookEntry& entry)
{
    EmulatorRuntimeDebugEntry out;
    out.address = entry.address;
    out.size = entry.size;
    out.enabled = entry.enabled;
    out.hits = entry.hits;
    out.lastPc = entry.lastPc;
    out.lastAddress = entry.lastAddress;
    out.lastSize = entry.lastSize;
    out.lastValue = entry.lastValue;
    return out;
}

static void clearMainRuntimeIfCurrent(NativeRuntime* runtime)
{
    bool shouldUnbindCheats = false;
    pthread_mutex_lock(&g_runtimeThreadMutex);
    if (g_mainRuntime == runtime)
    {
        pthread_mutex_lock(&g_debuggerMutex);
        uninstallDebuggerHooksLocked(runtime);
        pthread_mutex_unlock(&g_debuggerMutex);
        g_mainRuntime = NULL;
        g_currentAppSha256.clear();
        shouldUnbindCheats = true;
    }
    pthread_mutex_unlock(&g_runtimeThreadMutex);
    if (shouldUnbindCheats)
    {
        // Cheat runtime has its own mutex and can flush JIT state; keep it
        // outside g_runtimeThreadMutex to avoid frontend/runtime lock inversion.
        cheatRuntimeUnbind(runtime);
    }
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

    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        printf("DingooPie: failed to size app: %s\n", path.c_str());
        return NULL;
    }

    long fileSize = ftell(file);
    if (fileSize <= 0 || (uint64_t)fileSize > UINT32_MAX || fseek(file, 0, SEEK_SET) != 0)
    {
        fclose(file);
        printf("DingooPie: invalid app size: %s\n", path.c_str());
        return NULL;
    }

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

static bool isPpssppRunBlockMarkerValue(uint32_t value)
{
    return (value & 0xff000000u) == 0x68000000u;
}

static uint32_t loadStateLe32(const uint8_t* p)
{
    return (uint32_t)p[0] |
        ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) |
        ((uint32_t)p[3] << 24);
}

static void storeStateLe32(uint8_t* p, uint32_t value)
{
    p[0] = (uint8_t)(value & 0xff);
    p[1] = (uint8_t)((value >> 8) & 0xff);
    p[2] = (uint8_t)((value >> 16) & 0xff);
    p[3] = (uint8_t)((value >> 24) & 0xff);
}

static bool addressInRange32(uint32_t address, uint32_t rangeStart, uint32_t rangeSize)
{
    return rangeSize != 0 &&
        address >= rangeStart &&
        address - rangeStart < rangeSize;
}

static bool addressInMappedAppCode(uint32_t address)
{
    if (s_AppDataBuffSize == 0)
    {
        return false;
    }

    uint32_t aliasStart = s_AppDataAddr & 0x1fffffffu;
    return addressInRange32(address, s_AppDataAddr, s_AppDataBuffSize) ||
        addressInRange32(address, aliasStart, s_AppDataBuffSize);
}

struct RestoredIrJitMarkerReplacement
{
    size_t offset;
    uint32_t original;
};

static uint32_t collectRestoredIrJitMarkerReplacements(
    const RuntimeMemoryRegion& target,
    const EmulatorRuntimeStateRegion& region,
    std::vector<RestoredIrJitMarkerReplacement>* replacements,
    uint32_t* unresolvedOut)
{
    if (!replacements)
    {
        return 0;
    }
    replacements->clear();
    if (unresolvedOut)
    {
        *unresolvedOut = 0;
    }
    if (!target.data || region.data.size() != region.size ||
        region.start < target.start || region.size > target.size ||
        region.start - target.start > target.size - region.size)
    {
        return 0;
    }

    size_t targetOffset = (size_t)(region.start - target.start);
    uint32_t markerCount = 0;
    for (size_t offset = 0; offset + sizeof(uint32_t) <= region.data.size(); offset += sizeof(uint32_t))
    {
        uint32_t value = loadStateLe32(region.data.data() + offset);
        if (!isPpssppRunBlockMarkerValue(value))
        {
            continue;
        }

        markerCount++;
        uint32_t original = 0;
        bool resolved = ppssppShimResolveEmuHack(region.start + (uint32_t)offset, value, &original) &&
            !isPpssppRunBlockMarkerValue(original);
        if (!resolved && addressInMappedAppCode(region.start + (uint32_t)offset))
        {
            original = loadStateLe32(target.data + targetOffset + offset);
            resolved = !isPpssppRunBlockMarkerValue(original);
        }
        if (resolved)
        {
            RestoredIrJitMarkerReplacement replacement;
            replacement.offset = offset;
            replacement.original = original;
            replacements->push_back(replacement);
        }
        else if (unresolvedOut)
        {
            (*unresolvedOut)++;
        }
    }
    return markerCount;
}

static void stripIrJitMarkersFromCapturedRegion(EmulatorRuntimeStateRegion* region)
{
    if (!region || region->data.size() != region->size || region->size < sizeof(uint32_t))
    {
        return;
    }

    // PPSSPP IRJIT writes transient runblock markers over guest code; savestates
    // must store the original guest instructions so newly created saves reload cleanly.
    uint32_t strippedCount = 0;
    for (size_t offset = 0; offset + sizeof(uint32_t) <= region->data.size(); offset += sizeof(uint32_t))
    {
        uint8_t* bytes = region->data.data() + offset;
        uint32_t value = loadStateLe32(bytes);
        if (!isPpssppRunBlockMarkerValue(value))
        {
            continue;
        }

        uint32_t original = 0;
        if (ppssppShimResolveEmuHack(region->start + (uint32_t)offset, value, &original))
        {
            storeStateLe32(bytes, original);
            strippedCount++;
        }
    }

    if (strippedCount && getenv("DINGOO_PIE_IRJIT_TRACE"))
    {
        printf("save-state: stripped %u IRJIT marker(s) in region 0x%08x\n",
            strippedCount, region->start);
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

static void seedResourceMonitorForAppLocked(app* loadedApp, bool preserveExistingSnapshot)
{
    if (!loadedApp || g_currentAppSha256.empty())
    {
        runtimeResourceMonitorReset(NULL, NULL);
        return;
    }

    app_parse_package_resource_indexes(loadedApp);
    // Auto-open can arm capture before app metadata exists; preserve those early
    // load events once the app identity is known.
    bool preserve = preserveExistingSnapshot &&
        runtimeResourceMonitorMatchesApp(
            g_appLoadPath.c_str(),
            g_currentAppSha256.c_str());
    if (!preserve)
    {
        runtimeResourceMonitorReset(g_appLoadPath.c_str(), g_currentAppSha256.c_str());
    }
    runtimeResourceMonitorSetAppResources(loadedApp);
}

static void seedResourceMonitorForAppIfCapturing(app* loadedApp)
{
    if (!runtimeResourceMonitorIsCapturing())
    {
        return;
    }

    pthread_mutex_lock(&g_runtimeThreadMutex);
    seedResourceMonitorForAppLocked(loadedApp, true);
    pthread_mutex_unlock(&g_runtimeThreadMutex);
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

static void logEmulationFailure(NativeRuntime* runtime, RuntimeError err, const CrashLogContext& context)
{
    printf("DingooPie: native runtime failed: %u (%s)\n", err, nativeRuntimeErrorString(err));
    std::string crashLogFileName;
    if (crashLogWriteGuestFailure(runtime, err, context, &crashLogFileName))
    {
        printf("crash-log:wrote file=%s reason=guest-runtime-failure\n", crashLogFileName.c_str());
    }
    else
    {
        printf("crash-log:failed reason=guest-runtime-failure\n");
    }
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

static void storeRuntimeSearchTestValue(uint8_t* bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value & 0xffu);
    bytes[1] = (uint8_t)((value >> 8) & 0xffu);
    bytes[2] = (uint8_t)((value >> 16) & 0xffu);
    bytes[3] = (uint8_t)((value >> 24) & 0xffu);
}

static bool writeRuntimeSearchTestValue(uint32_t address, uint32_t value)
{
    uint8_t bytes[4] = {};
    storeRuntimeSearchTestValue(bytes, value);
    return emulatorRuntimeWriteMemory(address, bytes, sizeof(bytes));
}

static bool candidatesContainAddress(
    const std::vector<EmulatorRuntimeMemorySearchCandidate>& candidates,
    uint32_t address)
{
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        if (candidates[i].address == address)
        {
            return true;
        }
    }
    return false;
}

static bool singleCandidateFilterPass(
    uint32_t address,
    uint32_t previous,
    uint32_t target,
    EmulatorRuntimeMemorySearchFilter filter)
{
    std::vector<EmulatorRuntimeMemorySearchCandidate> candidates;
    EmulatorRuntimeMemorySearchCandidate candidate;
    candidate.address = address;
    candidate.previous = previous;
    candidates.push_back(candidate);
    if (!emulatorRuntimeFilterMemorySearchCandidates(4, target, filter, &candidates))
    {
        return false;
    }
    return candidates.size() == 1 && candidates[0].address == address;
}

static void runMemorySearcherAutotest(uint32_t probeAddress)
{
    if (!memorySearcherAutotestEnabled())
    {
        return;
    }

    probeAddress &= ~3u;
    uint32_t original = 0;
    bool readOriginal = emulatorRuntimeReadMemory(probeAddress, &original, sizeof(original));
    if (!readOriginal)
    {
        printf("cheat-finder-autotest: result=fail read_original=0 address=0x%08x\n", probeAddress);
        return;
    }

    const uint32_t baseValue = 0x10203040u;
    const uint32_t increasedValue = 0x10203050u;
    const uint32_t decreasedValue = 0x10203048u;
    uint32_t pageBegin = probeAddress & ~0xfffu;
    uint32_t pageEnd = pageBegin <= 0xffffefffu ? pageBegin + 0x1000u : 0xffffffffu;

    bool writeBase = writeRuntimeSearchTestValue(probeAddress, baseValue);
    std::vector<EmulatorRuntimeMemorySearchCandidate> found;
    bool capped = false;
    bool searchOk = writeBase && emulatorRuntimeSearchMemoryValue(
        pageBegin, pageEnd, 4, baseValue, 4096, &found, &capped);
    bool contains = searchOk && candidatesContainAddress(found, probeAddress);
    bool unchanged = contains && singleCandidateFilterPass(
        probeAddress, baseValue, baseValue, EMULATOR_RUNTIME_MEMORY_SEARCH_UNCHANGED);

    bool writeIncreased = unchanged && writeRuntimeSearchTestValue(probeAddress, increasedValue);
    bool increased = writeIncreased && singleCandidateFilterPass(
        probeAddress, baseValue, 0, EMULATOR_RUNTIME_MEMORY_SEARCH_INCREASED);

    bool writeDecreased = increased && writeRuntimeSearchTestValue(probeAddress, decreasedValue);
    bool decreased = writeDecreased && singleCandidateFilterPass(
        probeAddress, increasedValue, 0, EMULATOR_RUNTIME_MEMORY_SEARCH_DECREASED);

    bool equal = decreased && singleCandidateFilterPass(
        probeAddress, decreasedValue, decreasedValue, EMULATOR_RUNTIME_MEMORY_SEARCH_EQUAL);

    bool restored = writeRuntimeSearchTestValue(probeAddress, original);
    bool pass = readOriginal && writeBase && searchOk && contains && !capped &&
        unchanged && writeIncreased && increased && writeDecreased && decreased &&
        equal && restored;

    printf("cheat-finder-autotest: probe=0x%08x original=0x%08x search_ok=%u candidates=%u contains=%u capped=%u\n",
        probeAddress, original, searchOk ? 1u : 0u, (unsigned)found.size(),
        contains ? 1u : 0u, capped ? 1u : 0u);
    printf("cheat-finder-autotest: unchanged=%u increased=%u decreased=%u equal=%u restored=%u result=%s\n",
        unchanged ? 1u : 0u, increased ? 1u : 0u, decreased ? 1u : 0u,
        equal ? 1u : 0u, restored ? 1u : 0u, pass ? "pass" : "fail");
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
    pthread_mutex_lock(&g_runtimeThreadMutex);
    g_currentAppSha256 = appSha256;
    pthread_mutex_unlock(&g_runtimeThreadMutex);
    bridge_set_app_identity(appSha256.c_str());
    fsys_set_app_identity(appSha256.c_str());
    cheatRuntimeLoadForApp(
        appSha256.c_str(),
        g_appLoadPath.c_str(),
        g_enabledCheatFeatureKeys);
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
    seedResourceMonitorForAppIfCapturing(loadedApp);
    cheatRuntimeBind(runtime);

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
    runMemorySearcherAutotest(loadedApp->bin_entry);

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
    installDebuggerAutotestHooks(loadedApp->bin_entry, 0x00000000u, 0xffffffffu);
    cheatRuntimeApplyStartup(runtime);

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

    CrashLogContext crashContext;
    memset(&crashContext, 0, sizeof(crashContext));
    crashContext.appPath = g_appLoadPath.c_str();
    crashContext.appMainPath = g_appMainPath.c_str();
    crashContext.appSha256 = appSha256.c_str();
    crashContext.compatProfile = compatProfileName(appSha256.c_str());
    crashContext.backend = effectiveBackend;
    crashContext.appEntry = appMainEntry;
    crashContext.bootEntry = loadedApp->bin_entry;
    crashContext.origin = loadedApp->origin;
    crashContext.appSize = loadedApp->bin_size;

    if (getenv("DINGOO_PIE_FORCE_GUEST_CRASH"))
    {
        nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &loadedApp->bin_entry);
        printf("DingooPie: forcing guest crash for diagnostics\n");
        g_lastRunExitedNormally.store(false, std::memory_order_release);
        logEmulationFailure(runtime, RUNTIME_ERROR_EXCEPTION, crashContext);
        frontendRequestQuit();
        return runtime;
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
        logEmulationFailure(runtime, err, crashContext);
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

bool startDingooPie(
    const char* appPath,
    const EmulatorOptions& options,
    bool clearRecentOnStartupFailure,
    bool enableResourceMonitor,
    const std::vector<std::string>& enabledCheatFeatureKeys)
{
    g_runtimeStopRequested.store(false, std::memory_order_release);
    g_lastRunExitedNormally.store(false, std::memory_order_release);
    g_lastRunAppPath.clear();
    pthread_mutex_lock(&g_runtimeThreadMutex);
    g_appMainEntry = 0;
    g_appMainInitCheckAddress = 0;
    g_currentAppSha256.clear();
    pthread_mutex_unlock(&g_runtimeThreadMutex);
    g_options = options;
    g_clearRecentOnStartupFailure = clearRecentOnStartupFailure;
    g_enabledCheatFeatureKeys = enabledCheatFeatureKeys;
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

    if (enableResourceMonitor)
    {
        // The runtime thread will seed app metadata after loadApp() succeeds.
        emulatorRuntimeEnableResourceMonitor();
    }

    pthread_t tid;
    int ret = pthread_create(&tid, NULL, dingoopieRun, NULL);
    if (ret)
    {
        printf("DingooPie: pthread_create failed\n");
        if (enableResourceMonitor)
        {
            runtimeResourceMonitorSetActive(false);
        }
        assert(0);
        return false;
    }
    pthread_mutex_lock(&g_runtimeThreadMutex);
    g_runtimeThread = tid;
    g_runtimeThreadStarted = true;
    pthread_mutex_unlock(&g_runtimeThreadMutex);

    return true;
}

void suppressCurrentRunRecentAppSave(void)
{
    // Clearing the recent list is an explicit user choice; do not let the
    // normal runtime-exit save path immediately re-add the running app.
    g_suppressCurrentRunRecentAppSave.store(true, std::memory_order_release);
}

bool emulatorRuntimeReadMemory(uint32_t address, void* out, size_t size)
{
    if (!out || size == 0)
    {
        return false;
    }

    pthread_mutex_lock(&g_runtimeThreadMutex);
    NativeRuntime* runtime = g_mainRuntime;
    bool ok = runtime && nativeRuntimeReadRaw(runtime, address, out, size);
    pthread_mutex_unlock(&g_runtimeThreadMutex);
    return ok;
}

bool emulatorRuntimeWriteMemory(uint32_t address, const void* in, size_t size)
{
    if (!in || size == 0)
    {
        return false;
    }

    pthread_mutex_lock(&g_runtimeThreadMutex);
    NativeRuntime* runtime = g_mainRuntime;
    bool ok = runtime && nativeRuntimeWriteRaw(runtime, address, in, size);
    if (ok)
    {
        int64_t value = 0;
        size_t valueBytes = size < sizeof(value) ? size : sizeof(value);
        memcpy(&value, in, valueBytes);
        nativeRuntimeNotifyMemoryAccess(runtime, RUNTIME_MEM_WRITE, address, (int)size, value);
        nativeRuntimeFlushCodeCache(runtime);
    }
    pthread_mutex_unlock(&g_runtimeThreadMutex);
    return ok;
}

static void readRuntimeRegisterSnapshotLocked(NativeRuntime* runtime, EmulatorRuntimeRegisterSnapshot* out)
{
    memset(out, 0, sizeof(*out));
    if (!runtime)
    {
        return;
    }

    if (nativeRuntimeGetBackend(runtime) == EXECUTION_BACKEND_PPSSPP_IRJIT)
    {
        ppssppShimSyncStateToRuntime(runtime);
    }

    out->running = true;
    for (int i = 0; i < 32; ++i)
    {
        nativeRuntimeReadRegister(runtime, i, &out->gpr[i]);
    }
    float* fpr = nativeRuntimeFpr(runtime);
    float* vfpu = nativeRuntimeVfpu(runtime);
    uint32_t* vfpuCtrl = nativeRuntimeVfpuCtrl(runtime);
    uint32_t* fcr31 = nativeRuntimeFcr31(runtime);
    uint32_t* fpcond = nativeRuntimeFpCond(runtime);
    if (fpr)
    {
        memcpy(out->fpr, fpr, sizeof(out->fpr));
    }
    if (vfpu)
    {
        memcpy(out->vfpu, vfpu, sizeof(out->vfpu));
    }
    if (vfpuCtrl)
    {
        memcpy(out->vfpuCtrl, vfpuCtrl, sizeof(out->vfpuCtrl));
    }
    if (fcr31)
    {
        out->fcr31 = *fcr31;
    }
    if (fpcond)
    {
        out->fpcond = *fpcond;
    }
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_PC, &out->pc);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_HI, &out->hi);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_LO, &out->lo);
}

static bool writeRuntimeRegisterSnapshotLocked(NativeRuntime* runtime, const EmulatorRuntimeRegisterSnapshot& in)
{
    if (!runtime)
    {
        return false;
    }

    uint32_t zero = 0;
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_ZERO, &zero);
    for (int i = 1; i < 32; ++i)
    {
        if (nativeRuntimeWriteRegister(runtime, i, &in.gpr[i]) != RUNTIME_OK)
        {
            return false;
        }
    }
    if (nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &in.pc) != RUNTIME_OK ||
        nativeRuntimeWriteRegister(runtime, RUNTIME_REG_HI, &in.hi) != RUNTIME_OK ||
        nativeRuntimeWriteRegister(runtime, RUNTIME_REG_LO, &in.lo) != RUNTIME_OK)
    {
        return false;
    }
    float* fpr = nativeRuntimeFpr(runtime);
    float* vfpu = nativeRuntimeVfpu(runtime);
    uint32_t* vfpuCtrl = nativeRuntimeVfpuCtrl(runtime);
    uint32_t* fcr31 = nativeRuntimeFcr31(runtime);
    uint32_t* fpcond = nativeRuntimeFpCond(runtime);
    if (fpr)
    {
        memcpy(fpr, in.fpr, sizeof(in.fpr));
    }
    if (vfpu)
    {
        memcpy(vfpu, in.vfpu, sizeof(in.vfpu));
    }
    if (vfpuCtrl)
    {
        memcpy(vfpuCtrl, in.vfpuCtrl, sizeof(in.vfpuCtrl));
    }
    if (fcr31)
    {
        *fcr31 = in.fcr31;
    }
    if (fpcond)
    {
        *fpcond = in.fpcond;
    }
    if (nativeRuntimeGetBackend(runtime) == EXECUTION_BACKEND_PPSSPP_IRJIT)
    {
        ppssppShimSyncStateFromRuntime(runtime);
    }
    return true;
}

bool emulatorRuntimeCaptureState(EmulatorRuntimeState* out)
{
    if (!out)
    {
        return false;
    }
    out->regions.clear();
    out->taskRegisters.clear();
    out->hleSemaphoreCounts.clear();
    memset(&out->registers, 0, sizeof(out->registers));
    memset(&out->heap, 0, sizeof(out->heap));
    out->osTicks = 0;

    pthread_mutex_lock(&g_runtimeThreadMutex);
    NativeRuntime* runtime = g_mainRuntime;
    if (!runtime)
    {
        pthread_mutex_unlock(&g_runtimeThreadMutex);
        return false;
    }

    readRuntimeRegisterSnapshotLocked(runtime, &out->registers);
    out->osTicks = bridge_capture_os_ticks();
    uint32_t semaphoreCount = bridge_semaphore_state_count();
    out->hleSemaphoreCounts.resize(semaphoreCount);
    bridge_capture_semaphore_counts(out->hleSemaphoreCounts.data(), semaphoreCount);
    if (!vmHeapCaptureSnapshot(&out->heap))
    {
        pthread_mutex_unlock(&g_runtimeThreadMutex);
        return false;
    }

    std::vector<NativeRuntime*> taskRuntimes;
    taskSchedulerSnapshotRuntimes(&taskRuntimes);
    out->taskRegisters.reserve(taskRuntimes.size());
    for (size_t i = 0; i < taskRuntimes.size(); ++i)
    {
        EmulatorRuntimeRegisterSnapshot taskSnapshot;
        readRuntimeRegisterSnapshotLocked(taskRuntimes[i], &taskSnapshot);
        if (taskSnapshot.running)
        {
            out->taskRegisters.push_back(taskSnapshot);
        }
    }

    size_t count = nativeRuntimeMemoryRegionCount(runtime);
    out->regions.reserve(count);
    std::vector<const uint8_t*> capturedDataPointers;
    capturedDataPointers.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        RuntimeMemoryRegion region;
        if (!nativeRuntimeGetMemoryRegion(runtime, i, &region) ||
            !region.data ||
            !(region.perms & RUNTIME_PROT_WRITE) ||
            region.size == 0)
        {
            continue;
        }
        if (std::find(capturedDataPointers.begin(), capturedDataPointers.end(), region.data) !=
            capturedDataPointers.end())
        {
            continue;
        }

        EmulatorRuntimeStateRegion stateRegion;
        stateRegion.start = region.start;
        stateRegion.size = region.size;
        stateRegion.perms = region.perms;
        stateRegion.data.resize(region.size);
        memcpy(stateRegion.data.data(), region.data, stateRegion.data.size());
        stripIrJitMarkersFromCapturedRegion(&stateRegion);
        out->regions.push_back(stateRegion);
        capturedDataPointers.push_back(region.data);
    }

    pthread_mutex_unlock(&g_runtimeThreadMutex);
    return out->registers.running && !out->regions.empty();
}

bool emulatorRuntimeRestoreState(const EmulatorRuntimeState& state)
{
    if (!state.registers.running)
    {
        return false;
    }

    pthread_mutex_lock(&g_runtimeThreadMutex);
    NativeRuntime* runtime = g_mainRuntime;
    if (!runtime)
    {
        pthread_mutex_unlock(&g_runtimeThreadMutex);
        return false;
    }
    std::vector<NativeRuntime*> taskRuntimes;
    taskSchedulerSnapshotRuntimes(&taskRuntimes);
    // A save-state can only be restored into the same runtime topology that
    // created it: main runtime plus the exact captured task runtime set.
    if (taskRuntimes.size() != state.taskRegisters.size())
    {
        printf("save-state: task count mismatch current=%u saved=%u\n",
            (unsigned)taskRuntimes.size(), (unsigned)state.taskRegisters.size());
        pthread_mutex_unlock(&g_runtimeThreadMutex);
        return false;
    }

    nativeRuntimeFlushCodeCache(runtime);
    for (size_t i = 0; i < taskRuntimes.size(); ++i)
    {
        nativeRuntimeFlushCodeCache(taskRuntimes[i]);
    }

    size_t count = nativeRuntimeMemoryRegionCount(runtime);
    std::vector<RuntimeMemoryRegion> runtimeRegions;
    runtimeRegions.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        RuntimeMemoryRegion region;
        if (nativeRuntimeGetMemoryRegion(runtime, i, &region) &&
            region.data &&
            (region.perms & RUNTIME_PROT_WRITE) &&
            region.size > 0)
        {
            runtimeRegions.push_back(region);
        }
    }

    for (size_t i = 0; i < state.regions.size(); ++i)
    {
        const EmulatorRuntimeStateRegion& region = state.regions[i];
        if (region.size == 0 || region.data.size() != region.size)
        {
            pthread_mutex_unlock(&g_runtimeThreadMutex);
            return false;
        }

        RuntimeMemoryRegion* target = NULL;
        for (size_t j = 0; j < runtimeRegions.size(); ++j)
        {
            RuntimeMemoryRegion& runtimeRegion = runtimeRegions[j];
            if (runtimeRegion.start == region.start && runtimeRegion.size >= region.size)
            {
                target = &runtimeRegion;
                break;
            }
        }
        if (!target)
        {
            pthread_mutex_unlock(&g_runtimeThreadMutex);
            return false;
        }
        std::vector<RestoredIrJitMarkerReplacement> markerReplacements;
        uint32_t unresolvedMarkers = 0;
        uint32_t restoredMarkers = collectRestoredIrJitMarkerReplacements(
            *target, region, &markerReplacements, &unresolvedMarkers);
        memcpy(target->data, region.data.data(), region.data.size());
        size_t targetOffset = (size_t)(region.start - target->start);
        for (size_t j = 0; j < markerReplacements.size(); ++j)
        {
            const RestoredIrJitMarkerReplacement& replacement = markerReplacements[j];
            storeStateLe32(target->data + targetOffset + replacement.offset, replacement.original);
        }
        if (restoredMarkers && getenv("DINGOO_PIE_IRJIT_TRACE"))
        {
            printf("save-state: restored region 0x%08x replaced %u/%u IRJIT marker(s), unresolved=%u\n",
                region.start,
                (unsigned)markerReplacements.size(),
                restoredMarkers,
                unresolvedMarkers);
        }
    }

    bool ok = vmHeapRestoreSnapshot(state.heap) &&
        writeRuntimeRegisterSnapshotLocked(runtime, state.registers);
    if (ok)
    {
        bridge_restore_os_ticks(state.osTicks);
        ok = bridge_restore_semaphore_counts(
            state.hleSemaphoreCounts.empty() ? NULL : state.hleSemaphoreCounts.data(),
            (uint32_t)state.hleSemaphoreCounts.size());
    }
    if (ok)
    {
        for (size_t i = 0; ok && i < state.taskRegisters.size(); ++i)
        {
            ok = writeRuntimeRegisterSnapshotLocked(taskRuntimes[i], state.taskRegisters[i]);
            if (ok)
            {
                nativeRuntimeFlushCodeCache(taskRuntimes[i]);
            }
        }
    }
    if (ok)
    {
        nativeRuntimeFlushCodeCache(runtime);
        framebufferPresentRestoredFrame();
        bridge_notify_state_restored();
        pauseGateMarkRuntimeRestored();
    }
    pthread_mutex_unlock(&g_runtimeThreadMutex);
    return ok;
}

void emulatorRuntimeNotifyPauseRequested(void)
{
    pthread_mutex_lock(&g_runtimeThreadMutex);
    NativeRuntime* runtime = g_mainRuntime;
    if (runtime)
    {
        ppssppShimRequestPause(runtime);
    }
    pthread_mutex_unlock(&g_runtimeThreadMutex);
}

uint32_t emulatorRuntimeActiveThreadCount(void)
{
    pthread_mutex_lock(&g_runtimeThreadMutex);
    uint32_t count = g_mainRuntime ? 1u : 0u;
    pthread_mutex_unlock(&g_runtimeThreadMutex);
    if (count)
    {
        count += (uint32_t)taskSchedulerRuntimeCount();
    }
    return count;
}

bool emulatorRuntimeForEachReadableRegion(bool (*callback)(uint32_t start, uint32_t size, void* userData), void* userData)
{
    if (!callback)
    {
        return false;
    }

    pthread_mutex_lock(&g_runtimeThreadMutex);
    NativeRuntime* runtime = g_mainRuntime;
    if (!runtime)
    {
        pthread_mutex_unlock(&g_runtimeThreadMutex);
        return false;
    }

    size_t count = nativeRuntimeMemoryRegionCount(runtime);
    std::vector<RuntimeMemoryRegion> regions;
    regions.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        RuntimeMemoryRegion region;
        if (nativeRuntimeGetMemoryRegion(runtime, i, &region) &&
            (region.perms & RUNTIME_PROT_READ) &&
            region.size > 0)
        {
            regions.push_back(region);
        }
    }
    pthread_mutex_unlock(&g_runtimeThreadMutex);

    for (size_t i = 0; i < regions.size(); ++i)
    {
        if (!callback(regions[i].start, regions[i].size, userData))
        {
            return false;
        }
    }
    return true;
}

bool emulatorRuntimeGetRegisterSnapshot(EmulatorRuntimeRegisterSnapshot* out)
{
    if (!out)
    {
        return false;
    }

    pthread_mutex_lock(&g_runtimeThreadMutex);
    NativeRuntime* runtime = g_mainRuntime;
    if (!runtime)
    {
        pthread_mutex_unlock(&g_runtimeThreadMutex);
        return false;
    }

    readRuntimeRegisterSnapshotLocked(runtime, out);
    pthread_mutex_unlock(&g_runtimeThreadMutex);
    return true;
}

bool emulatorRuntimeMemoryRegions(std::vector<EmulatorRuntimeMemoryRegionInfo>* out)
{
    if (!out)
    {
        return false;
    }
    out->clear();

    pthread_mutex_lock(&g_runtimeThreadMutex);
    NativeRuntime* runtime = g_mainRuntime;
    if (!runtime)
    {
        pthread_mutex_unlock(&g_runtimeThreadMutex);
        return false;
    }

    size_t count = nativeRuntimeMemoryRegionCount(runtime);
    out->reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        RuntimeMemoryRegion region;
        if (nativeRuntimeGetMemoryRegion(runtime, i, &region))
        {
            EmulatorRuntimeMemoryRegionInfo info;
            info.start = region.start;
            info.size = region.size;
            info.perms = region.perms;
            out->push_back(info);
        }
    }
    pthread_mutex_unlock(&g_runtimeThreadMutex);
    return true;
}

bool emulatorRuntimeGetAppInfo(EmulatorRuntimeAppInfo* out)
{
    if (!out)
    {
        return false;
    }
    *out = EmulatorRuntimeAppInfo();

    pthread_mutex_lock(&g_runtimeThreadMutex);
    NativeRuntime* runtime = g_mainRuntime;
    app* loadedApp = s_app;
    if (!runtime || !loadedApp || g_currentAppSha256.empty())
    {
        pthread_mutex_unlock(&g_runtimeThreadMutex);
        return false;
    }

    out->running = true;
    out->path = g_appLoadPath;
    out->fileName = appFileNameFromPath(g_appLoadPath);
    out->guestMainPath = g_appMainPath;
    out->sha256 = g_currentAppSha256;
    out->compatProfile = compatProfileName(g_currentAppSha256.c_str());
    out->backend = executionBackendName(nativeRuntimeGetBackend(runtime));
    out->fileSize = loadedApp->file_size;
    out->origin = loadedApp->origin;
    out->binSize = loadedApp->bin_size;
    out->progSize = loadedApp->prog_size;
    out->bssSize = loadedApp->bin_bss;
    out->bootEntry = loadedApp->bin_entry;
    out->appMainEntry = g_appMainEntry;
    out->importCount = loadedApp->import_count;
    out->exportCount = loadedApp->export_count;
    out->packageResourceCount = loadedApp->package_resource_count;
    out->resourceCount = loadedApp->resource_count;
    pthread_mutex_unlock(&g_runtimeThreadMutex);
    return true;
}

bool emulatorRuntimeEnableResourceMonitor(void)
{
    pthread_mutex_lock(&g_runtimeThreadMutex);
    app* loadedApp = s_app;
    bool running = g_mainRuntime && loadedApp && !g_currentAppSha256.empty();
    if (running)
    {
        seedResourceMonitorForAppLocked(loadedApp, runtimeResourceMonitorIsCapturing());
    }
    else
    {
        runtimeResourceMonitorReset(NULL, NULL);
    }
    runtimeResourceMonitorSetActive(true);
    pthread_mutex_unlock(&g_runtimeThreadMutex);
    return running;
}

static uint32_t runtimeSearchMaskForWidth(int width)
{
    if (width == 1)
    {
        return 0xffu;
    }
    if (width == 2)
    {
        return 0xffffu;
    }
    return 0xffffffffu;
}

static uint32_t runtimeSearchReadLeValue(const uint8_t* bytes, int width)
{
    uint32_t value = bytes[0];
    if (width >= 2)
    {
        value |= ((uint32_t)bytes[1]) << 8;
    }
    if (width >= 4)
    {
        value |= ((uint32_t)bytes[2]) << 16;
        value |= ((uint32_t)bytes[3]) << 24;
    }
    return value;
}

static bool runtimeSearchReadValue(uint32_t address, int width, uint32_t* out)
{
    uint8_t bytes[4] = {};
    if (!out || width != 1 && width != 2 && width != 4 ||
        !emulatorRuntimeReadMemory(address, bytes, (size_t)width))
    {
        return false;
    }
    *out = runtimeSearchReadLeValue(bytes, width);
    return true;
}

static bool runtimeSearchMatchesFilter(
    uint32_t previous,
    uint32_t current,
    uint32_t target,
    EmulatorRuntimeMemorySearchFilter filter)
{
    switch (filter)
    {
    case EMULATOR_RUNTIME_MEMORY_SEARCH_EQUAL:
        return current == target;
    case EMULATOR_RUNTIME_MEMORY_SEARCH_INCREASED:
        return current > previous;
    case EMULATOR_RUNTIME_MEMORY_SEARCH_DECREASED:
        return current < previous;
    case EMULATOR_RUNTIME_MEMORY_SEARCH_UNCHANGED:
        return current == previous;
    default:
        return false;
    }
}

bool emulatorRuntimeSearchMemoryValue(
    uint32_t begin,
    uint32_t end,
    int width,
    uint32_t target,
    size_t maxCandidates,
    std::vector<EmulatorRuntimeMemorySearchCandidate>* out,
    bool* capped)
{
    if (!out || (width != 1 && width != 2 && width != 4) || begin >= end)
    {
        return false;
    }
    out->clear();
    if (capped)
    {
        *capped = false;
    }
    if (maxCandidates == 0)
    {
        return true;
    }

    pthread_mutex_lock(&g_runtimeThreadMutex);
    NativeRuntime* runtime = g_mainRuntime;
    if (!runtime)
    {
        pthread_mutex_unlock(&g_runtimeThreadMutex);
        return false;
    }

    size_t count = nativeRuntimeMemoryRegionCount(runtime);
    std::vector<RuntimeMemoryRegion> regions;
    regions.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        RuntimeMemoryRegion region;
        if (nativeRuntimeGetMemoryRegion(runtime, i, &region) &&
            (region.perms & RUNTIME_PROT_READ) &&
            region.size > 0)
        {
            regions.push_back(region);
        }
    }
    pthread_mutex_unlock(&g_runtimeThreadMutex);

    target &= runtimeSearchMaskForWidth(width);
    const uint32_t chunkSize = 64 * 1024;
    std::vector<uint8_t> buffer(chunkSize + 4);
    for (size_t i = 0; i < regions.size(); ++i)
    {
        uint64_t regionBegin = regions[i].start;
        uint64_t regionEnd = regionBegin + regions[i].size;
        if (regionBegin < begin)
        {
            regionBegin = begin;
        }
        if (regionEnd > end)
        {
            regionEnd = end;
        }
        if (regionBegin >= regionEnd || regionEnd - regionBegin < (uint32_t)width)
        {
            continue;
        }

        uint32_t alignedBegin = (uint32_t)((regionBegin + (uint32_t)width - 1u) & ~(uint64_t)((uint32_t)width - 1u));
        if (alignedBegin >= regionEnd)
        {
            continue;
        }

        for (uint32_t address = alignedBegin; address < regionEnd;)
        {
            uint32_t remaining = (uint32_t)(regionEnd - address);
            uint32_t readSize = remaining > chunkSize ? chunkSize : remaining;
            if (readSize < (uint32_t)width)
            {
                break;
            }
            if (!emulatorRuntimeReadMemory(address, &buffer[0], readSize))
            {
                address += readSize;
                continue;
            }

            for (uint32_t offset = 0; offset + (uint32_t)width <= readSize; offset += (uint32_t)width)
            {
                uint32_t value = runtimeSearchReadLeValue(&buffer[offset], width);
                if (value == target)
                {
                    EmulatorRuntimeMemorySearchCandidate candidate;
                    candidate.address = address + offset;
                    candidate.previous = value;
                    out->push_back(candidate);
                    if (out->size() >= maxCandidates)
                    {
                        if (capped)
                        {
                            *capped = true;
                        }
                        return true;
                    }
                }
            }
            address += readSize - (readSize % (uint32_t)width);
        }
    }
    return true;
}

bool emulatorRuntimeFilterMemorySearchCandidates(
    int width,
    uint32_t target,
    EmulatorRuntimeMemorySearchFilter filter,
    std::vector<EmulatorRuntimeMemorySearchCandidate>* candidates)
{
    if (!candidates || (width != 1 && width != 2 && width != 4))
    {
        return false;
    }

    pthread_mutex_lock(&g_runtimeThreadMutex);
    bool hasRuntime = g_mainRuntime != NULL;
    pthread_mutex_unlock(&g_runtimeThreadMutex);
    if (!hasRuntime)
    {
        return false;
    }

    target &= runtimeSearchMaskForWidth(width);
    std::vector<EmulatorRuntimeMemorySearchCandidate> next;
    next.reserve(candidates->size());
    for (size_t i = 0; i < candidates->size(); ++i)
    {
        EmulatorRuntimeMemorySearchCandidate candidate = (*candidates)[i];
        uint32_t current = 0;
        if (!runtimeSearchReadValue(candidate.address, width, &current))
        {
            continue;
        }
        if (runtimeSearchMatchesFilter(candidate.previous, current, target, filter))
        {
            candidate.previous = current;
            next.push_back(candidate);
        }
    }
    candidates->swap(next);
    return true;
}

bool emulatorRuntimeDisassemble(uint32_t address, uint32_t instructionCount, std::vector<EmulatorRuntimeDisassemblyLine>* out)
{
    if (!out || instructionCount == 0)
    {
        return false;
    }
    out->clear();
    out->reserve(instructionCount);

    csh handle;
    if (cs_open(CS_ARCH_MIPS, CS_MODE_MIPS32, &handle) != CS_ERR_OK)
    {
        return false;
    }

    pthread_mutex_lock(&g_runtimeThreadMutex);
    NativeRuntime* runtime = g_mainRuntime;
    if (!runtime)
    {
        pthread_mutex_unlock(&g_runtimeThreadMutex);
        cs_close(&handle);
        return false;
    }

    for (uint32_t i = 0; i < instructionCount; ++i)
    {
        uint64_t lineAddress = (uint64_t)address + (uint64_t)i * 4u;
        if (lineAddress > 0xffffffffull)
        {
            break;
        }

        EmulatorRuntimeDisassemblyLine line;
        line.address = (uint32_t)lineAddress;
        line.encoding = 0;
        line.text.clear();
        line.valid = false;

        if (nativeRuntimeReadRaw(runtime, line.address, &line.encoding, sizeof(line.encoding)))
        {
            uint32_t originalEncoding = 0;
            if (ppssppShimResolveEmuHack(line.address, line.encoding, &originalEncoding))
            {
                line.encoding = originalEncoding;
            }

            cs_insn* insn = NULL;
            size_t count = cs_disasm(handle, (const uint8_t*)&line.encoding,
                sizeof(line.encoding), line.address, 1, &insn);
            if (count > 0)
            {
                line.text = insn[0].mnemonic;
                if (insn[0].op_str[0])
                {
                    line.text += " ";
                    line.text += insn[0].op_str;
                }
                line.valid = true;
                cs_free(insn, count);
            }
            else
            {
                line.text = "disasm-error";
            }
        }
        else
        {
            line.text = "unmapped";
        }
        out->push_back(line);
    }

    pthread_mutex_unlock(&g_runtimeThreadMutex);
    cs_close(&handle);
    return true;
}

static bool runtimeDebuggerHasRuntimeLocked(void)
{
    return g_mainRuntime != NULL;
}

bool emulatorRuntimeAddPcHit(uint32_t address)
{
    address &= ~3u;
    pthread_mutex_lock(&g_runtimeThreadMutex);
    pthread_mutex_lock(&g_debuggerMutex);
    for (std::list<RuntimeDebugHookEntry>::iterator it = g_debugPcHits.begin();
        it != g_debugPcHits.end(); ++it)
    {
        if (it->address == address)
        {
            RuntimeDebugHookEntry& entry = *it;
            if (!entry.enabled)
            {
                entry.enabled = true;
                if (g_mainRuntime && !entry.hook)
                {
                    nativeRuntimeAddHook(g_mainRuntime, &entry.hook, RUNTIME_HOOK_CODE,
                        (void*)debuggerCodeHook, &entry, entry.address, entry.address);
                }
            }
            pthread_mutex_unlock(&g_debuggerMutex);
            pthread_mutex_unlock(&g_runtimeThreadMutex);
            return true;
        }
    }

    RuntimeDebugHookEntry entry;
    memset(&entry, 0, sizeof(entry));
    entry.address = address;
    entry.size = 4;
    entry.enabled = true;
    g_debugPcHits.push_back(entry);
    if (runtimeDebuggerHasRuntimeLocked())
    {
        installDebuggerHooksLocked(g_mainRuntime);
    }
    pthread_mutex_unlock(&g_debuggerMutex);
    pthread_mutex_unlock(&g_runtimeThreadMutex);
    return true;
}

bool emulatorRuntimeRemovePcHit(uint32_t address)
{
    address &= ~3u;
    pthread_mutex_lock(&g_runtimeThreadMutex);
    pthread_mutex_lock(&g_debuggerMutex);
    for (std::list<RuntimeDebugHookEntry>::iterator it = g_debugPcHits.begin();
        it != g_debugPcHits.end(); ++it)
    {
        if (it->address == address)
        {
            if (g_mainRuntime && it->hook)
            {
                nativeRuntimeRemoveHook(g_mainRuntime, it->hook);
                it->hook = 0;
                it->enabled = false;
            }
            else
            {
                g_debugPcHits.erase(it);
            }
            pthread_mutex_unlock(&g_debuggerMutex);
            pthread_mutex_unlock(&g_runtimeThreadMutex);
            return true;
        }
    }
    pthread_mutex_unlock(&g_debuggerMutex);
    pthread_mutex_unlock(&g_runtimeThreadMutex);
    return false;
}

void emulatorRuntimeClearPcHits(void)
{
    pthread_mutex_lock(&g_runtimeThreadMutex);
    pthread_mutex_lock(&g_debuggerMutex);
    if (g_mainRuntime)
    {
        for (std::list<RuntimeDebugHookEntry>::iterator it = g_debugPcHits.begin();
            it != g_debugPcHits.end(); ++it)
        {
            if (it->hook)
            {
                nativeRuntimeRemoveHook(g_mainRuntime, it->hook);
                it->hook = 0;
            }
            it->enabled = false;
        }
    }
    else
    {
        g_debugPcHits.clear();
    }
    pthread_mutex_unlock(&g_debuggerMutex);
    pthread_mutex_unlock(&g_runtimeThreadMutex);
}

std::vector<EmulatorRuntimeDebugEntry> emulatorRuntimePcHits(void)
{
    std::vector<EmulatorRuntimeDebugEntry> out;
    pthread_mutex_lock(&g_debuggerMutex);
    out.reserve(g_debugPcHits.size());
    for (std::list<RuntimeDebugHookEntry>::const_iterator it = g_debugPcHits.begin();
        it != g_debugPcHits.end(); ++it)
    {
        if (it->enabled)
        {
            out.push_back(exportDebugEntry(*it));
        }
    }
    pthread_mutex_unlock(&g_debuggerMutex);
    return out;
}

bool emulatorRuntimeAddWriteHit(uint32_t address, uint32_t size)
{
    uint32_t end = 0;
    if (!debuggerInclusiveRangeEnd(address, size, &end))
    {
        return false;
    }

    pthread_mutex_lock(&g_runtimeThreadMutex);
    pthread_mutex_lock(&g_debuggerMutex);
    for (std::list<RuntimeDebugHookEntry>::iterator it = g_debugWriteHits.begin();
        it != g_debugWriteHits.end(); ++it)
    {
        if (it->address == address && it->size == size)
        {
            RuntimeDebugHookEntry& entry = *it;
            if (!entry.enabled)
            {
                entry.enabled = true;
                if (g_mainRuntime && !entry.hook)
                {
                    nativeRuntimeAddHook(g_mainRuntime, &entry.hook, RUNTIME_HOOK_MEM_VALID,
                        (void*)debuggerWriteHook, &entry, entry.address, end);
                }
            }
            pthread_mutex_unlock(&g_debuggerMutex);
            pthread_mutex_unlock(&g_runtimeThreadMutex);
            return true;
        }
    }

    RuntimeDebugHookEntry entry;
    memset(&entry, 0, sizeof(entry));
    entry.address = address;
    entry.size = size;
    entry.enabled = true;
    g_debugWriteHits.push_back(entry);
    if (runtimeDebuggerHasRuntimeLocked())
    {
        installDebuggerHooksLocked(g_mainRuntime);
    }
    pthread_mutex_unlock(&g_debuggerMutex);
    pthread_mutex_unlock(&g_runtimeThreadMutex);
    return true;
}

bool emulatorRuntimeRemoveWriteHit(uint32_t address, uint32_t size)
{
    pthread_mutex_lock(&g_runtimeThreadMutex);
    pthread_mutex_lock(&g_debuggerMutex);
    for (std::list<RuntimeDebugHookEntry>::iterator it = g_debugWriteHits.begin();
        it != g_debugWriteHits.end(); ++it)
    {
        if (it->address == address && it->size == size)
        {
            if (g_mainRuntime && it->hook)
            {
                nativeRuntimeRemoveHook(g_mainRuntime, it->hook);
                it->hook = 0;
                it->enabled = false;
            }
            else
            {
                g_debugWriteHits.erase(it);
            }
            pthread_mutex_unlock(&g_debuggerMutex);
            pthread_mutex_unlock(&g_runtimeThreadMutex);
            return true;
        }
    }
    pthread_mutex_unlock(&g_debuggerMutex);
    pthread_mutex_unlock(&g_runtimeThreadMutex);
    return false;
}

void emulatorRuntimeClearWriteHits(void)
{
    pthread_mutex_lock(&g_runtimeThreadMutex);
    pthread_mutex_lock(&g_debuggerMutex);
    if (g_mainRuntime)
    {
        for (std::list<RuntimeDebugHookEntry>::iterator it = g_debugWriteHits.begin();
            it != g_debugWriteHits.end(); ++it)
        {
            if (it->hook)
            {
                nativeRuntimeRemoveHook(g_mainRuntime, it->hook);
                it->hook = 0;
            }
            it->enabled = false;
        }
    }
    else
    {
        g_debugWriteHits.clear();
    }
    pthread_mutex_unlock(&g_debuggerMutex);
    pthread_mutex_unlock(&g_runtimeThreadMutex);
}

std::vector<EmulatorRuntimeDebugEntry> emulatorRuntimeWriteHits(void)
{
    std::vector<EmulatorRuntimeDebugEntry> out;
    pthread_mutex_lock(&g_debuggerMutex);
    out.reserve(g_debugWriteHits.size());
    for (std::list<RuntimeDebugHookEntry>::const_iterator it = g_debugWriteHits.begin();
        it != g_debugWriteHits.end(); ++it)
    {
        if (it->enabled)
        {
            out.push_back(exportDebugEntry(*it));
        }
    }
    pthread_mutex_unlock(&g_debuggerMutex);
    return out;
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

    bool exitedNormally = joinedRuntime && g_lastRunExitedNormally.load(std::memory_order_acquire);
    bool suppressRecentSave = g_suppressCurrentRunRecentAppSave.exchange(false, std::memory_order_acq_rel);

    if (shouldJoin && !joinedRuntime)
    {
        printf("DingooPie: recent app not saved because runtime thread did not join\n");
    }
    else if (shouldJoin && !exitedNormally)
    {
        printf("DingooPie: recent app not saved because runtime did not exit normally\n");
    }
    if (exitedNormally && suppressRecentSave)
    {
        printf("DingooPie: recent app not saved because recent list was cleared\n");
    }
    else if (exitedNormally)
    {
        saveRecentAppPath(g_lastRunAppPath, "normal runtime exit");
    }
}
