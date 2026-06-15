#include "guest_audio.h"
#include "sdl_audio.h"
#include <stdlib.h>

uint32_t waveout_open(waveout_args* args)
{
	printf("args channel %d format %d sample_rate %d volume %d, channel %d\n",
		args->channel, args->format, args->sample_rate, args->volume, args->channel);

    uint32_t ret = MixerOpen(args);
    free(args);
	return ret;
}

uint32_t waveout_write(uint32_t inst, char* buffer, int count)
{
    return MixerWriteBuff(buffer, count);
}

uint32_t waveout_can_write()
{
    return MixerPlaying();
}

bool waveout_drops_audio()
{
    return MixerDropsAudio();
}

uint32_t waveout_set_volume(uint32_t vol)
{
    MixerSetVolume(vol);
    return 1;
}

uint32_t waveout_close(uint32_t inst)
{
    (void)inst;
    return MixerClose();
}

uint32_t waveout_mute(uint32_t muted)
{
    MixerSetMuted(muted != 0);
    return 1;
}

