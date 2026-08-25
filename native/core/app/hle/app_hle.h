#ifndef DINGOO_PIE_APP_HLE_APP_HLE_H
#define DINGOO_PIE_APP_HLE_APP_HLE_H

#include <ctype.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_runtime_debug.h"
#include "app/runtime/app_loader.h"
#include "shared/config/runtime_constants.h"

// Wires Dingoo SDK imports to host-side HLE implementations.
void bridge_set_game_identity(const char* sha256Hex);
const char* bridge_get_game_identity(void);
const char* bridge_get_last_task_stop_summary(void);
const char* bridge_get_last_hle_summary(void);
void bridge_apply_runtime_settings(void);
uint32_t bridge_capture_os_ticks(void);
void bridge_restore_os_ticks(uint32_t ticks);
uint32_t bridge_semaphore_state_count(void);
void bridge_capture_semaphore_counts(uint32_t* out, uint32_t count);
bool bridge_restore_semaphore_counts(const uint32_t* counts, uint32_t count);
void bridge_notify_state_restored(void);
bool bridge_try_fast_return_hook(uint32_t address, uint32_t* returnValue);
bool bridge_lookup_hook_address(const char* name, uint32_t* address);
void bridge_profile_tick(void);
bool bridge_fast_waveout_write(uint32_t instPtr, uint32_t bufferPtr, uint32_t count, uint32_t* returnValue);
uint32_t bridge_fast_waveout_can_write(void);
bool bridge_fast_os_sem_pend(uint32_t eventVal, uint32_t timeout, uint32_t errorPtr,
    NativeRuntime* runtime, bool* interrupted);
bool bridge_fast_os_sem_post(uint32_t eventVal, uint32_t* returnValue);
void bridge_release_game_resources(void);
RuntimeError bridge_init(NativeRuntime* runtime, app* _app);
RuntimeError bridge_init_task(NativeRuntime* runtime, app* _app, bool isMainRuntime);

#endif
