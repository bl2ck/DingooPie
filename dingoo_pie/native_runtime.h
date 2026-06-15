#ifndef DINGOO_PIE_NATIVE_RUNTIME_H
#define DINGOO_PIE_NATIVE_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include "execution_backend.h"

struct NativeRuntime;
typedef uint64_t RuntimeHook;

enum RuntimeError
{
    RUNTIME_OK = 0,
    RUNTIME_ERROR_NOMEM = 1,
    RUNTIME_ERROR_HANDLE = 2,
    RUNTIME_ERROR_READ_UNMAPPED = 3,
    RUNTIME_ERROR_WRITE_UNMAPPED = 4,
    RUNTIME_ERROR_FETCH_UNMAPPED = 5,
    RUNTIME_ERROR_HOOK = 6,
    RUNTIME_ERROR_INSN_INVALID = 7,
    RUNTIME_ERROR_ARG = 8,
    RUNTIME_ERROR_EXCEPTION = 9,
    RUNTIME_ERROR_BACKEND_UNAVAILABLE = 10
};

enum RuntimeProtection
{
    RUNTIME_PROT_NONE = 0,
    RUNTIME_PROT_READ = 1,
    RUNTIME_PROT_WRITE = 2,
    RUNTIME_PROT_EXEC = 4,
    RUNTIME_PROT_ALL = 7
};

enum RuntimeHookType
{
    RUNTIME_HOOK_CODE = 1,
    RUNTIME_HOOK_MEM_INVALID = 2,
    RUNTIME_HOOK_MEM_VALID = 4
};

enum RuntimeMemoryAccess
{
    RUNTIME_MEM_READ = 16,
    RUNTIME_MEM_WRITE = 17,
    RUNTIME_MEM_FETCH = 18,
    RUNTIME_MEM_READ_UNMAPPED = 19,
    RUNTIME_MEM_WRITE_UNMAPPED = 20,
    RUNTIME_MEM_FETCH_UNMAPPED = 21,
    RUNTIME_MEM_WRITE_PROT = 22,
    RUNTIME_MEM_READ_PROT = 23,
    RUNTIME_MEM_FETCH_PROT = 24,
    RUNTIME_MEM_READ_AFTER = 25
};

enum MipsRegister
{
    RUNTIME_REG_ZERO = 0,
    RUNTIME_REG_AT = 1,
    RUNTIME_REG_V0 = 2,
    RUNTIME_REG_V1 = 3,
    RUNTIME_REG_A0 = 4,
    RUNTIME_REG_A1 = 5,
    RUNTIME_REG_A2 = 6,
    RUNTIME_REG_A3 = 7,
    RUNTIME_REG_T0 = 8,
    RUNTIME_REG_T1 = 9,
    RUNTIME_REG_T2 = 10,
    RUNTIME_REG_T3 = 11,
    RUNTIME_REG_T4 = 12,
    RUNTIME_REG_T5 = 13,
    RUNTIME_REG_T6 = 14,
    RUNTIME_REG_T7 = 15,
    RUNTIME_REG_S0 = 16,
    RUNTIME_REG_S1 = 17,
    RUNTIME_REG_S2 = 18,
    RUNTIME_REG_S3 = 19,
    RUNTIME_REG_S4 = 20,
    RUNTIME_REG_S5 = 21,
    RUNTIME_REG_S6 = 22,
    RUNTIME_REG_S7 = 23,
    RUNTIME_REG_T8 = 24,
    RUNTIME_REG_T9 = 25,
    RUNTIME_REG_K0 = 26,
    RUNTIME_REG_K1 = 27,
    RUNTIME_REG_GP = 28,
    RUNTIME_REG_SP = 29,
    RUNTIME_REG_FP = 30,
    RUNTIME_REG_RA = 31,
    RUNTIME_REG_PC = 32,
    RUNTIME_REG_HI = 33,
    RUNTIME_REG_LO = 34
};

struct RuntimeMemoryRegion
{
    uint32_t start;
    uint32_t size;
    uint8_t* data;
    uint32_t perms;
};

typedef void (*RuntimeCodeHookCallback)(NativeRuntime* runtime, uint64_t address, uint32_t size, void* userData);
typedef bool (*RuntimeMemoryHookCallback)(NativeRuntime* runtime, RuntimeMemoryAccess type, uint64_t address, int size, int64_t value, void* userData);

RuntimeError nativeRuntimeCreate(NativeRuntime** runtime);
RuntimeError nativeRuntimeDestroy(NativeRuntime* runtime);
RuntimeError nativeRuntimeStart(NativeRuntime* runtime, uint64_t begin, uint64_t until, uint64_t timeout, size_t count);
RuntimeError nativeRuntimeRequestStop(NativeRuntime* runtime);
RuntimeError nativeRuntimeSetBackend(NativeRuntime* runtime, ExecutionBackend backend);
ExecutionBackend nativeRuntimeGetBackend(NativeRuntime* runtime);
RuntimeError nativeRuntimeReadRegister(NativeRuntime* runtime, int regid, void* value);
RuntimeError nativeRuntimeWriteRegister(NativeRuntime* runtime, int regid, const void* value);
RuntimeError nativeRuntimeMapMemory(NativeRuntime* runtime, uint64_t address, size_t size, uint32_t perms, void* ptr);
RuntimeError nativeRuntimeReadMemory(NativeRuntime* runtime, uint64_t address, void* bytes, size_t size);
RuntimeError nativeRuntimeWriteMemory(NativeRuntime* runtime, uint64_t address, const void* bytes, size_t size);
RuntimeError nativeRuntimeAddHook(NativeRuntime* runtime, RuntimeHook* hook, int type, void* callback, void* userData, uint64_t begin, uint64_t end, ...);
RuntimeError nativeRuntimeRemoveHook(NativeRuntime* runtime, RuntimeHook hook);
RuntimeError nativeRuntimeFlushCodeCache(NativeRuntime* runtime);
uint32_t* nativeRuntimeGpr(NativeRuntime* runtime);
uint32_t* nativeRuntimePc(NativeRuntime* runtime);
uint32_t* nativeRuntimeHi(NativeRuntime* runtime);
uint32_t* nativeRuntimeLo(NativeRuntime* runtime);
float* nativeRuntimeFpr(NativeRuntime* runtime);
float* nativeRuntimeVfpu(NativeRuntime* runtime);
uint32_t* nativeRuntimeVfpuCtrl(NativeRuntime* runtime);
uint32_t* nativeRuntimeFcr31(NativeRuntime* runtime);
uint32_t* nativeRuntimeFpCond(NativeRuntime* runtime);
size_t nativeRuntimeMemoryRegionCount(NativeRuntime* runtime);
bool nativeRuntimeGetMemoryRegion(NativeRuntime* runtime, size_t index, RuntimeMemoryRegion* out);
bool nativeRuntimeReadRaw(NativeRuntime* runtime, uint32_t address, void* out, size_t size);
bool nativeRuntimeWriteRaw(NativeRuntime* runtime, uint32_t address, const void* in, size_t size);
bool nativeRuntimeReadU8(NativeRuntime* runtime, uint32_t address, uint32_t* out);
bool nativeRuntimeReadU16(NativeRuntime* runtime, uint32_t address, uint32_t* out);
bool nativeRuntimeReadU32(NativeRuntime* runtime, uint32_t address, uint32_t* out);
bool nativeRuntimeWriteU8(NativeRuntime* runtime, uint32_t address, uint32_t value);
bool nativeRuntimeWriteU16(NativeRuntime* runtime, uint32_t address, uint32_t value);
bool nativeRuntimeWriteU32(NativeRuntime* runtime, uint32_t address, uint32_t value);
uint8_t* nativeRuntimeHostPointer(NativeRuntime* runtime, uint32_t address, size_t size);
bool nativeRuntimeHasCodeHook(NativeRuntime* runtime, uint32_t address);
void nativeRuntimeCallCodeHooks(NativeRuntime* runtime, uint32_t address);
const char* nativeRuntimeErrorString(RuntimeError err);

#endif
