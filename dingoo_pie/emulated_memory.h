#ifndef DINGOO_PIE_EMULATED_MEMORY_H
#define DINGOO_PIE_EMULATED_MEMORY_H

#include <stdint.h>
#include "app_loader.h"
#include "native_runtime.h"


int InitVmMem(NativeRuntime* runtime, app* _app);

int InitVmMemSubTask(NativeRuntime* runtime);

uint32_t vm_malloc(uint32_t len);
void vm_free(uint32_t addr);
uint32_t vm_realloc(uint32_t addr, uint32_t len);

void* toHostPtr(uint32_t addr);
uint32_t toVmPtr(void* ptr);

#endif
