#ifndef DINGOO_PIE_APP_RUNTIME_APP_RUNTIME_H
#define DINGOO_PIE_APP_RUNTIME_APP_RUNTIME_H

#include "emulator_options.h"
#include "app_runtime_state.h"

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

struct AppRuntimeMemoryRegionInfo
{
    uint32_t start;
    uint32_t size;
    uint32_t perms;
};

struct AppRuntimeInfo
{
    bool running;
    std::string path;
    std::string fileName;
    std::string guestMainPath;
    std::string sha256;
    std::string compatProfile;
    std::string backend;
    uint32_t fileSize;
    uint32_t origin;
    uint32_t binSize;
    uint32_t progSize;
    uint32_t bssSize;
    uint32_t bootEntry;
    uint32_t appMainEntry;
    uint32_t importCount;
    uint32_t exportCount;
    uint32_t packageResourceCount;
    uint32_t resourceCount;
};

struct AppRuntimeDisassemblyLine
{
    uint32_t address;
    uint32_t encoding;
    std::string text;
    bool valid;
};

struct AppRuntimeDebugEntry
{
    uint32_t address;
    uint32_t size;
    bool enabled;
    uint64_t hits;
    uint32_t lastPc;
    uint32_t lastAddress;
    uint32_t lastSize;
    uint64_t lastValue;
};

struct AppRuntimeMemorySearchCandidate
{
    uint32_t address;
    uint32_t previous;
};

enum AppRuntimeMemorySearchFilter
{
    APP_RUNTIME_MEMORY_SEARCH_EQUAL,
    APP_RUNTIME_MEMORY_SEARCH_INCREASED,
    APP_RUNTIME_MEMORY_SEARCH_DECREASED,
    APP_RUNTIME_MEMORY_SEARCH_UNCHANGED
};

// Starts the guest app on a background native runtime thread.
// clearRecentOnStartupFailure only clears recent.last_app if it still points here.
// enableResourceMonitor arms capture before the runtime thread starts.
bool appRuntimeStart(
    const char* appPath,
    const EmulatorOptions& options,
    bool clearRecentOnStartupFailure,
    bool enableResourceMonitor,
    const std::vector<std::string>& enabledCheatFeatureKeys);
void appRuntimeStop(void);
void appRuntimeSuppressRecentGameSave(void);
bool appRuntimeReadMemory(uint32_t address, void* out, size_t size);
bool appRuntimeWriteMemory(uint32_t address, const void* in, size_t size);
bool appRuntimeCaptureState(AppRuntimeState* out);
bool appRuntimeRestoreState(const AppRuntimeState& state);
void appRuntimeNotifyPauseRequested(void);
uint32_t appRuntimeActiveThreadCount(void);
bool appRuntimeForEachReadableRegion(bool (*callback)(uint32_t start, uint32_t size, void* userData), void* userData);
bool appRuntimeGetRegisterSnapshot(AppRuntimeRegisterSnapshot* out);
bool appRuntimeDisassemble(uint32_t address, uint32_t instructionCount, std::vector<AppRuntimeDisassemblyLine>* out);
bool appRuntimeMemoryRegions(std::vector<AppRuntimeMemoryRegionInfo>* out);
bool appRuntimeGetInfo(AppRuntimeInfo* out);
bool appRuntimeEnableResourceMonitor(void);
bool appRuntimeSearchMemoryValue(
    uint32_t begin,
    uint32_t end,
    int width,
    uint32_t target,
    size_t maxCandidates,
    std::vector<AppRuntimeMemorySearchCandidate>* out,
    bool* capped);
bool appRuntimeFilterMemorySearchCandidates(
    int width,
    uint32_t target,
    AppRuntimeMemorySearchFilter filter,
    std::vector<AppRuntimeMemorySearchCandidate>* candidates);
bool appRuntimeAddPcHit(uint32_t address);
bool appRuntimeRemovePcHit(uint32_t address);
void appRuntimeClearPcHits(void);
std::vector<AppRuntimeDebugEntry> appRuntimePcHits(void);
bool appRuntimeAddWriteHit(uint32_t address, uint32_t size);
bool appRuntimeRemoveWriteHit(uint32_t address, uint32_t size);
void appRuntimeClearWriteHits(void);
std::vector<AppRuntimeDebugEntry> appRuntimeWriteHits(void);

#endif
