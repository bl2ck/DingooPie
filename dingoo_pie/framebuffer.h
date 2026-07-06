#ifndef DINGOO_PIE_FRAMEBUFFER_H
#define DINGOO_PIE_FRAMEBUFFER_H

#include <stdint.h>
#include "native_runtime.h"
#include "emulator_config.h"
#include "app_loader.h"

// 320x240 RGB565 framebuffer, rounded up to a 4 KB page boundary.
#define VM_LCD_FB_SIZE  0x00026000

int InitFb(NativeRuntime* runtime);

uint32_t _lcd_get_frame(void);
bool framebufferHostPointer(uint32_t addr, void** out);
bool framebufferVmPointer(void* ptr, uint32_t* out);

void* getFramebuffPtr(void);
void* getPresentedFramebuffPtr(void);
void copyPresentedFramebuff(void* dst, uint32_t size);

void requestFbUpdate(void);
void framebufferPresentRestoredFrame(void);
int consumeFbUpdateRequest(void);
uint64_t consumeFramebufferSubmittedCount(void);
uint64_t consumeFramebufferCopyMicros(void);
void consumeFramebufferTimingStats(uint64_t* totalIntervalMicros, uint64_t* maxIntervalMicros,
    uint64_t* over25msCount, uint64_t* over33msCount);
void trackFramebufferWrite(uint32_t address, uint32_t size);
bool framebufferAddressOverlaps(uint32_t address, uint32_t size);
uint64_t consumeFramebufferWriteCount(void);
uint64_t consumeFramebufferWriteBytes(void);
void framebufferSetProfileEnabled(bool enabled);

#endif
