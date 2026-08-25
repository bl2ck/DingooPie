#include "shared/services/guest_audio.h"
#include "shared/services/audio_output.h"
#include <stdlib.h>

uint32_t waveout_open(waveout_args* args)
{
	printf("audio: waveout_open channel=%d format=%d sample_rate=%d volume=%d\n",
		args->channel, args->format, args->sample_rate, args->volume);

    uint32_t ret = audioOutputOpen(args);
    free(args);
	return ret;
}

uint32_t waveout_write(uint32_t inst, char* buffer, int count)
{
    return audioOutputWriteBuffer(buffer, count);
}

uint32_t waveout_try_write(uint32_t inst, char* buffer, int count)
{
    return audioOutputTryWriteBuffer(buffer, count);
}

uint32_t waveout_can_write()
{
    return audioOutputIsPlaying();
}

uint32_t waveout_can_write_nonblocking()
{
    return audioOutputCanWriteNonBlocking();
}

bool waveout_skips_audio_output()
{
    return audioOutputSkipsGuestOutput();
}

uint32_t waveout_set_volume(uint32_t vol)
{
    audioOutputSetGuestVolume(vol);
    return 1;
}

uint32_t waveout_close(uint32_t inst)
{
    (void)inst;
    return audioOutputClose();
}

uint32_t waveout_mute(uint32_t muted)
{
    audioOutputSetMuted(muted != 0);
    return 1;
}
