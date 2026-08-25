#include "shared/game/game_runtime.h"

#include "app_runtime.h"
#include "cc/runtime/cc_runtime.h"
#include "cc/save/cc_save_state.h"
#include "app_save_state.h"
#include "runtime_resource_monitor.h"

#include <mutex>
#include <condition_variable>
#include <chrono>
#include <limits>
#include <stdio.h>
#include <string.h>
#include <thread>

static std::mutex g_gameRuntimeMutex;
static std::condition_variable g_ccRuntimeCondition;
static std::thread g_ccRuntimeThread;
static GameFormat g_activeFormat = GAME_FORMAT_UNKNOWN;
static bool g_ccRuntimeThreadComplete = true;
static std::string g_ccGamePath;
static std::vector<std::string> g_ccCheatFeatureKeys;

static void runCcGame(void)
{
    CcRuntimeStats stats = {};
    bool ok = ccRuntimeRunFile(g_ccGamePath.c_str(), g_ccCheatFeatureKeys, &stats);
    if (!ok)
    {
        printf("game-runtime: CC execution failed: %s\n",
            stats.error[0] ? stats.error : "unknown error");
    }
    else
    {
        printf("game-runtime: CC execution stopped instructions=%llu frames=%u tasks=%u\n",
            (unsigned long long)stats.instructions,
            stats.framesSubmitted,
            stats.tasksCreated);
    }
    {
        std::lock_guard<std::mutex> lock(g_gameRuntimeMutex);
        if (g_activeFormat == GAME_FORMAT_CC)
        {
            g_activeFormat = GAME_FORMAT_UNKNOWN;
        }
        g_ccRuntimeThreadComplete = true;
    }
    g_ccRuntimeCondition.notify_all();
}

static void joinCcRuntimeThread(void)
{
    if (!g_ccRuntimeThread.joinable())
    {
        return;
    }
    {
        std::unique_lock<std::mutex> lock(g_gameRuntimeMutex);
        if (!g_ccRuntimeCondition.wait_for(lock, std::chrono::seconds(5), [] {
                return g_ccRuntimeThreadComplete;
            }))
        {
            printf("game-runtime: CC runtime stop is taking longer than expected\n");
        }
    }
    g_ccRuntimeThread.join();
}

bool gameRuntimeStart(const char* gamePath, const EmulatorOptions& options,
    bool requireOptimizedBackend, bool resourceMonitorAutoOpen,
    const std::vector<std::string>& enabledCheatFeatureKeys)
{
    const std::string normalizedPath = gamePathNormalize(gamePath);
    const GameFormat format = gameFormatFromPath(normalizedPath);
    if (gameRuntimeActiveFormat() != GAME_FORMAT_UNKNOWN ||
        g_ccRuntimeThread.joinable())
    {
        gameRuntimeStop();
    }
    if (format == GAME_FORMAT_APP)
    {
        bool started = appRuntimeStart(normalizedPath.c_str(), options,
            requireOptimizedBackend, resourceMonitorAutoOpen,
            enabledCheatFeatureKeys);
        if (started)
        {
            std::lock_guard<std::mutex> lock(g_gameRuntimeMutex);
            g_activeFormat = GAME_FORMAT_APP;
        }
        return started;
    }
    if (format != GAME_FORMAT_CC)
    {
        printf("game-runtime: unsupported game path: %s\n",
            normalizedPath.empty() ? "(empty)" : normalizedPath.c_str());
        return false;
    }

    ccRuntimePrepareRun();
    runtimeResourceMonitorReset(normalizedPath.c_str(), NULL);
    runtimeResourceMonitorSetActive(resourceMonitorAutoOpen);
    g_ccGamePath = normalizedPath;
    g_ccCheatFeatureKeys = enabledCheatFeatureKeys;
    {
        std::lock_guard<std::mutex> lock(g_gameRuntimeMutex);
        g_activeFormat = GAME_FORMAT_CC;
        g_ccRuntimeThreadComplete = false;
    }
    try
    {
        g_ccRuntimeThread = std::thread(runCcGame);
    }
    catch (...)
    {
        std::lock_guard<std::mutex> lock(g_gameRuntimeMutex);
        g_activeFormat = GAME_FORMAT_UNKNOWN;
        g_ccRuntimeThreadComplete = true;
        printf("game-runtime: failed to create CC runtime thread\n");
        return false;
    }
    printf("game-runtime: started CC game: %s\n", normalizedPath.c_str());
    return true;
}

void gameRuntimeStop(void)
{
    GameFormat format;
    {
        std::lock_guard<std::mutex> lock(g_gameRuntimeMutex);
        format = g_activeFormat;
    }
    if (format == GAME_FORMAT_APP)
    {
        appRuntimeStop();
    }
    if (format == GAME_FORMAT_CC || g_ccRuntimeThread.joinable())
    {
        ccRuntimeRequestStop();
        joinCcRuntimeThread();
    }
    std::lock_guard<std::mutex> lock(g_gameRuntimeMutex);
    g_activeFormat = GAME_FORMAT_UNKNOWN;
}

GameFormat gameRuntimeActiveFormat(void)
{
    std::lock_guard<std::mutex> lock(g_gameRuntimeMutex);
    return g_activeFormat;
}

uint32_t gameRuntimeActiveUnitCount(void)
{
    GameFormat format = gameRuntimeActiveFormat();
    if (format == GAME_FORMAT_CC)
    {
        // CC schedules all guest tasks on one host runtime thread, so the
        // pause gate observes one waiter regardless of guest task count.
        return ccRuntimeIsRunning() ? 1u : 0u;
    }
    return appRuntimeActiveThreadCount();
}

void gameRuntimeNotifyPauseRequested(void)
{
    if (gameRuntimeActiveFormat() == GAME_FORMAT_APP)
    {
        appRuntimeNotifyPauseRequested();
    }
}

bool gameRuntimeReadMemory(uint32_t address, void* out, size_t size)
{
    return gameRuntimeActiveFormat() == GAME_FORMAT_CC ?
        ccRuntimeReadMemory(address, out, size) :
        appRuntimeReadMemory(address, out, size);
}

bool gameRuntimeWriteMemory(uint32_t address, const void* in, size_t size)
{
    return gameRuntimeActiveFormat() == GAME_FORMAT_CC ?
        ccRuntimeWriteMemory(address, in, size) :
        appRuntimeWriteMemory(address, in, size);
}

bool gameRuntimeGetRegisterSnapshot(
    AppRuntimeRegisterSnapshot* out, bool* arm32)
{
    if (arm32)
    {
        *arm32 = false;
    }
    if (gameRuntimeActiveFormat() != GAME_FORMAT_CC)
    {
        return appRuntimeGetRegisterSnapshot(out);
    }
    if (!out)
    {
        return false;
    }
    CcRuntimeRegisterSnapshot cc = {};
    if (!ccRuntimeGetRegisterSnapshot(&cc))
    {
        return false;
    }
    *out = AppRuntimeRegisterSnapshot();
    out->running = cc.running;
    memcpy(out->gpr, cc.r, sizeof(cc.r));
    out->pc = cc.r[15];
    out->fcr31 = cc.cpsr;
    if (arm32)
    {
        *arm32 = true;
    }
    return true;
}

bool gameRuntimeDisassemble(uint32_t address, uint32_t instructionCount,
    std::vector<AppRuntimeDisassemblyLine>* out)
{
    return gameRuntimeActiveFormat() != GAME_FORMAT_CC &&
        appRuntimeDisassemble(address, instructionCount, out);
}

bool gameRuntimeGetGameInfo(AppRuntimeInfo* out)
{
    if (gameRuntimeActiveFormat() != GAME_FORMAT_CC)
    {
        return appRuntimeGetInfo(out);
    }
    if (!out)
    {
        return false;
    }
    CcRuntimeGameInfo cc;
    if (!ccRuntimeGetGameInfo(&cc))
    {
        return false;
    }
    *out = AppRuntimeInfo();
    out->running = cc.running;
    out->path = cc.path;
    out->fileName = gameFileNameFromPath(cc.path);
    out->sha256 = cc.sha256;
    out->backend = "arm32_interpreter";
    out->fileSize = cc.fileSize;
    out->origin = cc.origin;
    out->progSize = cc.programSize;
    out->bootEntry = cc.origin;
    out->appMainEntry = cc.origin;
    out->importCount = cc.importCount;
    out->exportCount = cc.exportCount;
    out->packageResourceCount = cc.resourceCount;
    out->resourceCount = cc.resourceCount;
    return true;
}

struct CcMemorySearchContext
{
    uint32_t begin;
    uint32_t end;
    int width;
    uint32_t target;
    size_t maxCandidates;
    std::vector<AppRuntimeMemorySearchCandidate>* out;
    bool* capped;
};

static uint32_t readLeValue(const uint8_t* bytes, int width)
{
    uint32_t value = 0;
    for (int i = 0; i < width; ++i)
    {
        value |= (uint32_t)bytes[i] << (i * 8);
    }
    return value;
}

static bool searchCcMemoryRegion(uint32_t start, uint32_t size, void* userData)
{
    CcMemorySearchContext* context = (CcMemorySearchContext*)userData;
    uint64_t regionEnd = (uint64_t)start + size;
    uint64_t scanStart = start > context->begin ? start : context->begin;
    uint64_t scanEnd = regionEnd < (uint64_t)context->end + 1u ?
        regionEnd : (uint64_t)context->end + 1u;
    if (scanEnd <= scanStart || scanEnd - scanStart < (uint64_t)context->width)
    {
        return true;
    }
    scanStart = (scanStart + (uint32_t)context->width - 1u) &
        ~((uint64_t)context->width - 1u);
    uint8_t bytes[4] = {};
    for (uint64_t address = scanStart;
         address + (uint32_t)context->width <= scanEnd;
         address += (uint32_t)context->width)
    {
        if (ccRuntimeReadMemory((uint32_t)address, bytes, context->width) &&
            readLeValue(bytes, context->width) == context->target)
        {
            if (context->out->size() >= context->maxCandidates)
            {
                *context->capped = true;
                return false;
            }
            AppRuntimeMemorySearchCandidate candidate = {};
            candidate.address = (uint32_t)address;
            candidate.previous = context->target;
            context->out->push_back(candidate);
        }
    }
    return true;
}

bool gameRuntimeSearchMemoryValue(uint32_t begin, uint32_t end, int width,
    uint32_t target, size_t maxCandidates,
    std::vector<AppRuntimeMemorySearchCandidate>* out, bool* capped)
{
    if (gameRuntimeActiveFormat() != GAME_FORMAT_CC)
    {
        return appRuntimeSearchMemoryValue(begin, end, width, target,
            maxCandidates, out, capped);
    }
    if (!out || !capped || begin > end ||
        (width != 1 && width != 2 && width != 4) || maxCandidates == 0)
    {
        return false;
    }
    out->clear();
    *capped = false;
    CcMemorySearchContext context = {
        begin, end, width, target, maxCandidates, out, capped
    };
    bool completed = ccRuntimeForEachReadableRegion(searchCcMemoryRegion, &context);
    return completed || *capped;
}

bool gameRuntimeFilterMemorySearchCandidates(int width, uint32_t target,
    AppRuntimeMemorySearchFilter filter,
    std::vector<AppRuntimeMemorySearchCandidate>* candidates)
{
    if (gameRuntimeActiveFormat() != GAME_FORMAT_CC)
    {
        return appRuntimeFilterMemorySearchCandidates(
            width, target, filter, candidates);
    }
    if (!candidates || (width != 1 && width != 2 && width != 4))
    {
        return false;
    }
    std::vector<AppRuntimeMemorySearchCandidate> filtered;
    filtered.reserve(candidates->size());
    uint8_t bytes[4] = {};
    for (size_t i = 0; i < candidates->size(); ++i)
    {
        AppRuntimeMemorySearchCandidate candidate = (*candidates)[i];
        if (!ccRuntimeReadMemory(candidate.address, bytes, width))
        {
            continue;
        }
        uint32_t value = readLeValue(bytes, width);
        bool keep = filter == APP_RUNTIME_MEMORY_SEARCH_EQUAL ? value == target :
            filter == APP_RUNTIME_MEMORY_SEARCH_INCREASED ? value > candidate.previous :
            filter == APP_RUNTIME_MEMORY_SEARCH_DECREASED ? value < candidate.previous :
            value == candidate.previous;
        if (keep)
        {
            candidate.previous = value;
            filtered.push_back(candidate);
        }
    }
    candidates->swap(filtered);
    return true;
}

bool gameRuntimeSupportsBreakpoints(void)
{
    return gameRuntimeActiveFormat() == GAME_FORMAT_APP;
}

bool gameRuntimeEnableResourceMonitor(void)
{
    if (gameRuntimeActiveFormat() == GAME_FORMAT_CC)
    {
        runtimeResourceMonitorSetActive(true);
        return ccRuntimeIsRunning();
    }
    return appRuntimeEnableResourceMonitor();
}

bool gameRuntimeWriteState(const std::string& gamePath, int slot,
    std::string* error, SaveStateProgressCallback progressCallback,
    void* progressUserData)
{
    if (gameRuntimeActiveFormat() == GAME_FORMAT_CC)
    {
        CcRuntimeState state;
        return ccRuntimeCaptureState(&state, error) &&
            saveStateWriteCcSlot(gamePath, slot, state, error,
                progressCallback, progressUserData);
    }
    AppRuntimeState state;
    return appRuntimeCaptureState(&state) &&
        saveStateWriteSlot(gamePath, slot, state, error,
            progressCallback, progressUserData);
}

bool gameRuntimeReadState(const std::string& gamePath, int slot,
    std::string* error, SaveStateProgressCallback progressCallback,
    void* progressUserData)
{
    if (gameRuntimeActiveFormat() == GAME_FORMAT_CC)
    {
        CcRuntimeState state;
        return saveStateReadCcSlot(gamePath, slot, &state, error,
            progressCallback, progressUserData) &&
            ccRuntimeRestoreState(state, error);
    }
    AppRuntimeState state;
    return saveStateReadSlot(gamePath, slot, &state, error,
        progressCallback, progressUserData) &&
        appRuntimeRestoreState(state);
}
