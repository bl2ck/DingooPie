#ifndef DINGOO_PIE_APP_RUNTIME_APP_RUNTIME_DEBUG_H
#define DINGOO_PIE_APP_RUNTIME_APP_RUNTIME_DEBUG_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "app/cpu/mips_runtime.h"
#include "app/runtime/app_runtime_state.h"
#include "shared/game/game_runtime_types.h"

#include <string>
#include <vector>

struct AppRuntimeMemoryRegionInfo
{
    uint32_t start;
    uint32_t size;
    uint32_t perms;
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

const char* appRuntimeMemoryAccessName(RuntimeMemoryAccess type);
void appRuntimeDebugReportInvalidMemory(NativeRuntime* runtime,
    RuntimeMemoryAccess type, uint64_t address, int size, int64_t value);
void appRuntimeDebugDumpRegisters(NativeRuntime* runtime);
void appRuntimeDebugDumpStack(NativeRuntime* runtime, uint32_t stackStartAddress);
void appRuntimeDebugDumpReturnDisassembly(NativeRuntime* runtime);
void dumpAsmRange(NativeRuntime* runtime, uint32_t address, uint32_t bytes);
void appRuntimeDebugDumpRegistersToFile(NativeRuntime* runtime, FILE* file);
void appRuntimeDebugDumpMemory(const void* buffer, uint32_t count);

void toHexString(void* buff, int count, char* out);

std::string WString2String(const std::wstring& ws);
std::wstring String2WString(const std::string& s);

bool appRuntimeReadMemory(uint32_t address, void* out, size_t size);
bool appRuntimeWriteMemory(uint32_t address, const void* in, size_t size);
bool appRuntimeForEachReadableRegion(
    bool (*callback)(uint32_t start, uint32_t size, void* userData),
    void* userData);
bool appRuntimeGetRegisterSnapshot(AppRuntimeRegisterSnapshot* out);
bool appRuntimeDisassemble(uint32_t address, uint32_t instructionCount,
    std::vector<AppRuntimeDisassemblyLine>* out);
bool appRuntimeMemoryRegions(std::vector<AppRuntimeMemoryRegionInfo>* out);
bool appRuntimeGetInfo(AppRuntimeInfo* out);
bool appRuntimeEnableResourceMonitor(void);
bool appRuntimeSearchMemoryValue(uint32_t begin, uint32_t end, int width,
    uint32_t target, size_t maxCandidates,
    std::vector<AppRuntimeMemorySearchCandidate>* out, bool* capped);
bool appRuntimeFilterMemorySearchCandidates(int width, uint32_t target,
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
