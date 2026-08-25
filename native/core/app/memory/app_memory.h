#ifndef DINGOO_PIE_APP_MEMORY_APP_MEMORY_H
#define DINGOO_PIE_APP_MEMORY_APP_MEMORY_H

#include <stdint.h>
#include "app/runtime/app_loader.h"
#include "mips_runtime.h"
#include "app_heap_snapshot.h"

int appMemoryInitialize(NativeRuntime* runtime, app* _app);

int appMemoryMapTaskRuntime(NativeRuntime* runtime);

uint32_t vm_malloc(uint32_t len);
void vm_free(uint32_t addr);
uint32_t vm_realloc(uint32_t addr, uint32_t len);
bool vmHeapCaptureSnapshot(VmHeapSnapshot* out);
bool vmHeapRestoreSnapshot(const VmHeapSnapshot& snapshot);

void* toHostPtr(uint32_t addr);
void* toHostPtrRange(uint32_t addr, uint32_t size);
uint32_t toHostPtrRemaining(uint32_t addr, void** out);
const char* toHostString(uint32_t addr);
uint32_t toVmPtr(void* ptr);

#endif
