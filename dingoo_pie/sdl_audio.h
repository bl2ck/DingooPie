#ifndef DINGOO_PIE_SDL_AUDIO_H
#define DINGOO_PIE_SDL_AUDIO_H

#include <stdint.h>

#include "emulator_settings.h"
#include "guest_audio.h"

uint32_t MixerOpen(waveout_args* args);
uint32_t MixerClose();
uint32_t MixerWriteBuff(char* buffer, int count);
uint32_t MixerPlaying();
bool MixerSkipsAudioOutput();
void MixerSetVolume(uint32_t vol);
void MixerSetMuted(bool muted);
void MixerSetFrontendPaused(bool paused);
void MixerSetMasterVolumePercent(int percent);
void MixerSetBufferSamples(int samples);
void MixerSetAudioEffect(AudioEffectMode effect);

#endif
