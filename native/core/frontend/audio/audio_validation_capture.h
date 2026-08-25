#ifndef DINGOO_PIE_FRONTEND_AUDIO_AUDIO_VALIDATION_CAPTURE_H
#define DINGOO_PIE_FRONTEND_AUDIO_AUDIO_VALIDATION_CAPTURE_H

#include <SDL2/SDL.h>
#include <stdint.h>

void audioValidationSetEnabled(bool enabled);
void audioValidationBegin(const SDL_AudioSpec& audioSpec);
void audioValidationClose(void);
void audioValidationRecordAudio(const void* data, uint32_t bytes,
    const char* eventName, uint32_t queuedBytes, uint32_t pendingBytes);
void audioValidationRecordEvent(const char* eventName, uint32_t bytes,
    uint32_t queuedBytes, uint32_t pendingBytes, uint64_t waitMs);
void audioValidationRecordWait(uint32_t queuedBytes, uint32_t pendingBytes,
    uint64_t waitMs);
void audioValidationRecordDrop(uint32_t queuedBytes, uint32_t pendingBytes,
    uint64_t waitMs);

#endif
