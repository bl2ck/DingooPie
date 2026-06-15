#ifndef DINGOO_PIE_GUEST_AUDIO_H
#define DINGOO_PIE_GUEST_AUDIO_H

#include <stdint.h>
#include <stdbool.h>
#include "app_loader.h"
#include "native_runtime.h"


/* Audio Sample Format */
#define	AFMT_U8			8
#define AFMT_S16_LE		16

// Waveout types.
typedef struct {
	uint32_t sample_rate;
	uint16_t format;
	uint8_t  channel;
	uint8_t  volume;
} waveout_args;

typedef void waveout_inst;

// The following come from joyrider and from disassembly.
extern uint32_t waveout_open(waveout_args* args);
extern uint32_t waveout_write(uint32_t inst, char* buffer, int count);
extern uint32_t waveout_close(uint32_t inst);
extern uint32_t waveout_can_write();
extern bool waveout_drops_audio();
extern uint32_t waveout_set_volume(uint32_t vol);
extern uint32_t waveout_mute(uint32_t muted);

#endif
