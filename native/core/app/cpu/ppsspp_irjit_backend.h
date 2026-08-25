#ifndef DINGOO_PIE_PPSSPP_IRJIT_BACKEND_H
#define DINGOO_PIE_PPSSPP_IRJIT_BACKEND_H

#include "native_runtime.h"

struct NativeRuntime;

bool ppssppIrJitBackendAvailable(void);
RuntimeError ppssppIrJitStart(NativeRuntime* runtime, uint64_t begin, uint64_t until, uint64_t timeout, size_t count);
RuntimeError ppssppIrJitFlushCodeCache(NativeRuntime* runtime);
void ppssppIrJitRequestStop(NativeRuntime* runtime);
void ppssppShimClearJitCache(NativeRuntime* runtime);
void ppssppShimSetFastMemoryOverride(int enabled);
void ppssppShimApplyRuntimeSettings(void);
void ppssppShimAttachRuntime(NativeRuntime* runtime);
void ppssppShimDetachRuntime(NativeRuntime* runtime);
void ppssppShimSetRuntimeLimit(uint64_t beginTicks, uint64_t maxTicks);
void ppssppShimRequestStop(void);
void ppssppShimRequestPause(NativeRuntime* runtime);
bool ppssppShimWaitForPauseResume(NativeRuntime* runtime);
void ppssppShimSyncStateToRuntime(NativeRuntime* runtime);
void ppssppShimSyncStateFromRuntime(NativeRuntime* runtime);
bool ppssppShimResolveEmuHack(uint32_t address, uint32_t value, uint32_t* original);
uint32_t ppssppShimRunCodeHook(uint32_t address);
void ppssppShimTraceJitExit(uint32_t pc);
void ppssppShimInterpretFallback(uint32_t op);
uint32_t ppssppShimRead8(uint32_t address);
uint32_t ppssppShimRead16(uint32_t address);
uint32_t ppssppShimRead32(uint32_t address);
uint64_t ppssppShimRead64(uint32_t address);
uint32_t ppssppShimLoad32Left(uint32_t address, uint32_t currentValue);
uint32_t ppssppShimLoad32Right(uint32_t address, uint32_t currentValue);
void ppssppShimWrite8(uint32_t address, uint32_t value);
void ppssppShimWrite16(uint32_t address, uint32_t value);
void ppssppShimWrite32(uint32_t address, uint32_t value);
void ppssppShimWrite64(uint32_t address, uint64_t value);
void ppssppShimStore32Left(uint32_t address, uint32_t value);
void ppssppShimStore32Right(uint32_t address, uint32_t value);
void ppssppShimReadBlock(uint32_t address, void* out, uint32_t size);
void ppssppShimWriteBlock(uint32_t address, const void* in, uint32_t size);
uint8_t* ppssppShimGetPointer(uint32_t address, uint32_t size);
uint32_t ppssppShimValidateAddress(uint32_t address, uint32_t alignment, uint32_t isWrite);
uint32_t ppssppShimIsValidRange(uint32_t address, uint32_t size);
uint32_t ppssppShimIsValid4AlignedAddress(uint32_t address);
uint32_t ppssppShimMaxSizeAtAddress(uint32_t address);
uintptr_t* ppssppShimFastPageBases(void);

#endif
