#ifndef DINGOO_PIE_SHARED_GAME_GAME_RUNTIME_H
#define DINGOO_PIE_SHARED_GAME_GAME_RUNTIME_H

#include "emulator_options.h"
#include "app_runtime.h"
#include "shared/game/game_paths.h"
#include "shared/save/save_slots.h"

#include <string>
#include <vector>

bool gameRuntimeStart(const char* gamePath, const EmulatorOptions& options,
    bool requireOptimizedBackend, bool resourceMonitorAutoOpen,
    const std::vector<std::string>& enabledCheatFeatureKeys);
void gameRuntimeStop(void);
GameFormat gameRuntimeActiveFormat(void);
uint32_t gameRuntimeActiveUnitCount(void);
void gameRuntimeNotifyPauseRequested(void);
bool gameRuntimeReadMemory(uint32_t address, void* out, size_t size);
bool gameRuntimeWriteMemory(uint32_t address, const void* in, size_t size);
bool gameRuntimeGetRegisterSnapshot(
    AppRuntimeRegisterSnapshot* out, bool* arm32 = 0);
bool gameRuntimeDisassemble(uint32_t address, uint32_t instructionCount,
    std::vector<AppRuntimeDisassemblyLine>* out);
bool gameRuntimeGetGameInfo(AppRuntimeInfo* out);
bool gameRuntimeSearchMemoryValue(uint32_t begin, uint32_t end, int width,
    uint32_t target, size_t maxCandidates,
    std::vector<AppRuntimeMemorySearchCandidate>* out, bool* capped);
bool gameRuntimeFilterMemorySearchCandidates(int width, uint32_t target,
    AppRuntimeMemorySearchFilter filter,
    std::vector<AppRuntimeMemorySearchCandidate>* candidates);
bool gameRuntimeSupportsBreakpoints(void);
bool gameRuntimeEnableResourceMonitor(void);
bool gameRuntimeWriteState(const std::string& gamePath, int slot,
    std::string* error, SaveStateProgressCallback progressCallback = 0,
    void* progressUserData = 0);
bool gameRuntimeReadState(const std::string& gamePath, int slot,
    std::string* error, SaveStateProgressCallback progressCallback = 0,
    void* progressUserData = 0);

#endif
