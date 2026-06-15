#ifndef DINGOO_PIE_TASK_SCHEDULER_H
#define DINGOO_PIE_TASK_SCHEDULER_H

#include <stdint.h>
#include <stdbool.h>
#include "app_loader.h"
#include "native_runtime.h"

#define OS_NO_ERR 0x0

////typedef unsigned int OS_STK;
////uint8_t OSTaskCreate(void (*task)(void* data), void* data, OS_STK* stack, uint8_t priority);
uint32_t OSTaskCreate(uint32_t taskFuncAddr, uint32_t dataPtr, uint32_t stackPtr, uint32_t priority);
void taskSchedulerResetShutdown(void);
void taskSchedulerRequestShutdown(const char* reason);
bool taskSchedulerIsShutdownRequested(void);

#endif
