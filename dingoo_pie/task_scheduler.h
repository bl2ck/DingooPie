#ifndef DINGOO_PIE_TASK_SCHEDULER_H
#define DINGOO_PIE_TASK_SCHEDULER_H

#include <stdint.h>
#include <stdbool.h>
#include <vector>
#include "app_loader.h"
#include "native_runtime.h"

#define OS_NO_ERR 0x0

// Dingoo guest task creation passes VM addresses for task, data, and stack.
uint32_t OSTaskCreate(uint32_t taskFuncAddr, uint32_t dataPtr, uint32_t stackPtr, uint32_t priority);
void taskSchedulerResetShutdown(void);
void taskSchedulerRequestShutdown(const char* reason);
bool taskSchedulerIsShutdownRequested(void);
void taskSchedulerRegisterRuntime(NativeRuntime* runtime);
void taskSchedulerUnregisterRuntime(NativeRuntime* runtime);
size_t taskSchedulerRuntimeCount(void);
void taskSchedulerSnapshotRuntimes(std::vector<NativeRuntime*>* out);

#endif
