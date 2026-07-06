#ifndef DINGOO_PIE_EMULATOR_CORE_H
#define DINGOO_PIE_EMULATOR_CORE_H

#include "emulator_options.h"
#include "vm_heap_snapshot.h"

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

struct EmulatorRuntimeRegisterSnapshot
{
    bool running;
    uint32_t gpr[32];
    float fpr[32];
    float vfpu[128];
    uint32_t vfpuCtrl[16];
    uint32_t pc;
    uint32_t hi;
    uint32_t lo;
    uint32_t fcr31;
    uint32_t fpcond;
};

struct EmulatorRuntimeMemoryRegionInfo
{
    uint32_t start;
    uint32_t size;
    uint32_t perms;
};

struct EmulatorRuntimeAppInfo
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

struct EmulatorRuntimeStateRegion
{
    uint32_t start;
    uint32_t size;
    uint32_t perms;
    std::vector<uint8_t> data;
};

struct EmulatorRuntimeState
{
    EmulatorRuntimeRegisterSnapshot registers;
    VmHeapSnapshot heap;
    uint32_t osTicks;
    std::vector<uint32_t> hleSemaphoreCounts;
    std::vector<EmulatorRuntimeRegisterSnapshot> taskRegisters;
    std::vector<EmulatorRuntimeStateRegion> regions;
};

struct EmulatorRuntimeDisassemblyLine
{
    uint32_t address;
    uint32_t encoding;
    std::string text;
    bool valid;
};

struct EmulatorRuntimeDebugEntry
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

struct EmulatorRuntimeMemorySearchCandidate
{
    uint32_t address;
    uint32_t previous;
};

enum EmulatorRuntimeMemorySearchFilter
{
    EMULATOR_RUNTIME_MEMORY_SEARCH_EQUAL,
    EMULATOR_RUNTIME_MEMORY_SEARCH_INCREASED,
    EMULATOR_RUNTIME_MEMORY_SEARCH_DECREASED,
    EMULATOR_RUNTIME_MEMORY_SEARCH_UNCHANGED
};

// Starts the guest app on a background native runtime thread.
// If clearRecentOnStartupFailure is true, an initialization failure clears
// recent.last_app only when it still points at this app.
bool startDingooPie(
    const char* appPath,
    const EmulatorOptions& options,
    bool clearRecentOnStartupFailure,
    const std::vector<std::string>& enabledCheatFeatureKeys);
void stopDingooPie(void);
void suppressCurrentRunRecentAppSave(void);
bool emulatorRuntimeReadMemory(uint32_t address, void* out, size_t size);
bool emulatorRuntimeWriteMemory(uint32_t address, const void* in, size_t size);
bool emulatorRuntimeCaptureState(EmulatorRuntimeState* out);
bool emulatorRuntimeRestoreState(const EmulatorRuntimeState& state);
void emulatorRuntimeNotifyPauseRequested(void);
uint32_t emulatorRuntimeActiveThreadCount(void);
bool emulatorRuntimeForEachReadableRegion(bool (*callback)(uint32_t start, uint32_t size, void* userData), void* userData);
bool emulatorRuntimeGetRegisterSnapshot(EmulatorRuntimeRegisterSnapshot* out);
bool emulatorRuntimeDisassemble(uint32_t address, uint32_t instructionCount, std::vector<EmulatorRuntimeDisassemblyLine>* out);
bool emulatorRuntimeMemoryRegions(std::vector<EmulatorRuntimeMemoryRegionInfo>* out);
bool emulatorRuntimeGetAppInfo(EmulatorRuntimeAppInfo* out);
bool emulatorRuntimeEnableResourceMonitor(void);
bool emulatorRuntimeSearchMemoryValue(
    uint32_t begin,
    uint32_t end,
    int width,
    uint32_t target,
    size_t maxCandidates,
    std::vector<EmulatorRuntimeMemorySearchCandidate>* out,
    bool* capped);
bool emulatorRuntimeFilterMemorySearchCandidates(
    int width,
    uint32_t target,
    EmulatorRuntimeMemorySearchFilter filter,
    std::vector<EmulatorRuntimeMemorySearchCandidate>* candidates);
bool emulatorRuntimeAddPcHit(uint32_t address);
bool emulatorRuntimeRemovePcHit(uint32_t address);
void emulatorRuntimeClearPcHits(void);
std::vector<EmulatorRuntimeDebugEntry> emulatorRuntimePcHits(void);
bool emulatorRuntimeAddWriteHit(uint32_t address, uint32_t size);
bool emulatorRuntimeRemoveWriteHit(uint32_t address, uint32_t size);
void emulatorRuntimeClearWriteHits(void);
std::vector<EmulatorRuntimeDebugEntry> emulatorRuntimeWriteHits(void);

#endif
