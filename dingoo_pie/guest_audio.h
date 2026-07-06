#ifndef DINGOO_PIE_GUEST_AUDIO_H
#define DINGOO_PIE_GUEST_AUDIO_H

#include <stdint.h>
#include <stdbool.h>
#include "app_loader.h"
#include "native_runtime.h"

// Dingoo SDK audio sample format values.
#define	AFMT_U8			8
#define AFMT_S16_LE		16

// Guest waveout structures mirrored by the HLE audio bridge.
typedef struct {
	uint32_t sample_rate;
	uint16_t format;
	uint8_t  channel;
	uint8_t  volume;
} waveout_args;

typedef void waveout_inst;

// Host-side implementations of the guest waveout API.
extern uint32_t waveout_open(waveout_args* args);
extern uint32_t waveout_write(uint32_t inst, char* buffer, int count);
extern uint32_t waveout_close(uint32_t inst);
extern uint32_t waveout_can_write();
extern bool waveout_skips_audio_output();
extern uint32_t waveout_set_volume(uint32_t vol);
extern uint32_t waveout_mute(uint32_t muted);

#endif
