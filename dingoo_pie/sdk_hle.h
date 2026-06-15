#ifndef DINGOO_PIE_SDK_HLE_H
#define DINGOO_PIE_SDK_HLE_H

#include <ctype.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "runtime_debug.h"
#include "app_loader.h"
#include "emulator_config.h"

// Wires Dingoo SDK imports to host-side HLE implementations.
void bridge_set_app_identity(const char* sha256Hex);
const char* bridge_get_app_identity(void);
const char* bridge_get_last_task_stop_summary(void);
const char* bridge_get_last_hle_summary(void);
void bridge_apply_runtime_settings(void);
bool bridge_try_fast_return_hook(uint32_t address, uint32_t* returnValue);
bool bridge_lookup_hook_address(const char* name, uint32_t* address);
void bridge_profile_tick(void);
bool bridge_fast_waveout_write(uint32_t instPtr, uint32_t bufferPtr, uint32_t count, uint32_t* returnValue);
uint32_t bridge_fast_waveout_can_write(void);
bool bridge_fast_os_sem_pend(uint32_t eventVal, uint32_t timeout, uint32_t errorPtr);
bool bridge_fast_os_sem_post(uint32_t eventVal, uint32_t* returnValue);
RuntimeError bridge_init(NativeRuntime* runtime, app* _app);
RuntimeError bridge_init_task(NativeRuntime* runtime, app* _app, bool isMainRuntime);


#endif

