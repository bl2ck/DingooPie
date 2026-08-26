#ifndef DINGOO_PIE_APP_RUNTIME_APP_RUNTIME_DEBUG_H
#define DINGOO_PIE_APP_RUNTIME_APP_RUNTIME_DEBUG_H

#include <ctype.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app/cpu/mips_runtime.h"
#include "app/runtime/app_runtime_state.h"
#include "shared/game/game_runtime_types.h"

#include <locale.h>
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

#ifndef EM_PORT_API
#define EM_PORT_API(rettype) rettype
#endif

#ifndef NULL
#include <stddef.h>
#endif

#ifndef offsetof
#define offsetof(type, field) ((size_t) & ((type *)0)->field)
#endif
#ifndef countof
#define countof(x) (sizeof(x) / sizeof((x)[0]))
#endif

#ifdef __x86_64__
#define PRId "I64d"
#define PRIX "I64X"
#elif __i386__
#define PRId "d"
#define PRIX "X"
#endif

#define ALIGN(x, align) (((x) + ((align)-1)) & ~((align)-1))

#define MAKERGB565(r, g, b) (uint16_t)(((uint32_t)(r >> 3) << 11) | ((uint32_t)(g >> 2) << 5) | ((uint32_t)(b >> 3)))
#define PIXEL565R(v) ((((uint32_t)v >> 11) << 3) & 0xff)
#define PIXEL565G(v) ((((uint32_t)v >> 5) << 2) & 0xff)
#define PIXEL565B(v) (((uint32_t)v << 3) & 0xff)

int wstrlen(char* txt);
void cpsrToStr(uint32_t v, char* out);
const char* appRuntimeMemoryAccessName(RuntimeMemoryAccess type);
void appRuntimeDebugReportInvalidMemory(NativeRuntime* runtime,
    RuntimeMemoryAccess type, uint64_t address, int size, int64_t value);
void appRuntimeDebugDumpRegisters(NativeRuntime* runtime);
void appRuntimeDebugDumpStack(NativeRuntime* runtime, uint32_t stackStartAddress);
void appRuntimeDebugDumpReturnDisassembly(NativeRuntime* runtime);
void dumpAsmRange(NativeRuntime* runtime, uint32_t address, uint32_t bytes);
void appRuntimeDebugDumpRegistersToFile(NativeRuntime* runtime, FILE* file);
void dumpMemStr(void* ptr, size_t len);
void appRuntimeDebugDumpMemory(const void* buffer, uint32_t count);
char* getSplitStr(char* str, char split, int n);

void toHexString(void* buff, int count, char* out);

uint32_t copyWstrToMrp(char* str);
uint32_t copyStrToMrp(char* str);
void printScreen(char* filename, uint16_t* buf);

int64_t get_uptime_ms(void);
int64_t get_time_ms(void);

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
