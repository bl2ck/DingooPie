#ifndef DINGOO_PIE_SHARED_SERVICES_AUDIO_OUTPUT_H
#define DINGOO_PIE_SHARED_SERVICES_AUDIO_OUTPUT_H

#include <stdint.h>

#include "config/settings/emulator_settings.h"
#include "shared/services/guest_audio.h"

uint32_t audioOutputOpen(waveout_args* args);
uint32_t audioOutputClose();
void audioOutputReleaseGameResources(void);
void audioOutputResetAfterRuntimeStop(void);
void audioOutputPrepareApplicationExit(void);
uint32_t audioOutputWriteBuffer(char* buffer, int count);
uint32_t audioOutputTryWriteBuffer(char* buffer, int count);
uint32_t audioOutputIsPlaying();
uint32_t audioOutputCanWriteNonBlocking();
bool audioOutputSkipsGuestOutput();
void audioOutputSetGuestVolume(uint32_t vol);
void audioOutputSetMuted(bool muted);
void audioOutputSetFrontendPaused(bool paused);
void audioOutputSetMasterVolumePercent(int percent);
void audioOutputSetBufferSamples(int samples);
void audioOutputSetBufferLatencyMode(AudioBufferLatencyMode mode);
void audioOutputSetEffect(AudioEffectMode effect);
void audioOutputSetNoiseReduction(DigitalNoiseReductionLevel level);
void audioOutputRecordInput(uint32_t controlMask);
void audioOutputSetValidationCaptureEnabled(bool enabled);

#endif
