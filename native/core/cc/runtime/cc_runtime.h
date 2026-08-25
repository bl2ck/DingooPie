#ifndef DINGOO_PIE_CC_RUNTIME_CC_RUNTIME_H
#define DINGOO_PIE_CC_RUNTIME_CC_RUNTIME_H

#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>

struct CcRuntimeState;

struct CcRuntimeRegisterSnapshot
{
    bool running;
    uint32_t r[16];
    uint32_t cpsr;
    uint32_t currentTaskIndex;
};

struct CcRuntimeGameInfo
{
    bool running;
    std::string path;
    std::string sha256;
    uint32_t fileSize;
    uint32_t origin;
    uint32_t programSize;
    uint32_t importCount;
    uint32_t exportCount;
    uint32_t resourceCount;
    uint32_t taskCount;
};

typedef bool (*CcRuntimeMemoryRegionCallback)(
    uint32_t start, uint32_t size, void* userData);

struct CcRuntimeStats
{
    uint64_t instructions;
    uint32_t importCalls;
    uint32_t unknownImports;
    uint32_t framesSubmitted;
    uint32_t tasksCreated;
    uint32_t faultAddress;
    uint32_t faultSize;
    uint32_t unsupportedPc;
    uint32_t lastImportPc;
    uint32_t lastImportReturnAddress;
    uint32_t failedTaskIndex;
    uint32_t failedTaskEntry;
    uint32_t failedTaskStack;
    uint32_t failedTaskPriority;
    uint32_t failedTaskDelayTicks;
    bool faultWrite;
    bool faultFetch;
    bool guestCompleted;
    char lastImport[96];
    char error[160];
};

// CC uses the persisted runtime controls shared with APP, but executes through
// an isolated ARM32 interpreter so APP backend behavior remains unchanged.
bool ccRuntimeRunFile(const char* path,
    const std::vector<std::string>& enabledCheatFeatureKeys,
    CcRuntimeStats* stats);
void ccRuntimeApplySettings(void);
void ccRuntimePrepareRun(void);
void ccRuntimeRequestStop(void);
bool ccRuntimeIsRunning(void);
uint32_t ccRuntimeActiveTaskCount(void);
bool ccRuntimeCaptureState(CcRuntimeState* out, std::string* error = 0);
bool ccRuntimeRestoreState(const CcRuntimeState& state, std::string* error = 0);
bool ccRuntimeReadMemory(uint32_t address, void* out, size_t size);
bool ccRuntimeWriteMemory(uint32_t address, const void* in, size_t size);
bool ccRuntimeForEachReadableRegion(
    CcRuntimeMemoryRegionCallback callback, void* userData);
bool ccRuntimeGetRegisterSnapshot(CcRuntimeRegisterSnapshot* out);
bool ccRuntimeGetGameInfo(CcRuntimeGameInfo* out);

#endif
