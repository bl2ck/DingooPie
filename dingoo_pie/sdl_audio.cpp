#include "sdl_audio.h"

#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static SDL_AudioDeviceID g_audioDevice = 0;
static SDL_AudioSpec g_audioSpec;
static SDL_mutex* g_audioMutex = NULL;
static uint32_t g_volume = 100;
static int g_masterVolumePercent = 100;
static int g_bufferSamples = 2048;
static bool g_guestMuteRequested = false;
static bool g_frontendPauseRequested = false;
static bool g_dropAudio = false;
static uint64_t g_lastQueueBackpressureLogTicks = 0;

static const uint32_t kQueueBackpressureLogIntervalMs = 1000;
static const uint32_t kAudioQueueDropDisabledMs = 0;
static const uint32_t kAudioQueueDropMaxMs = 60000;

enum AudioQueueWaitResult
{
    AUDIO_QUEUE_READY,
    AUDIO_QUEUE_OUTPUT_STOPPED,
    AUDIO_QUEUE_DROP_BUFFER
};

static uint32_t parseBoundedUintEnv(const char* name, uint32_t defaultValue, uint32_t maxValue)
{
    const char* value = getenv(name);
    if (!value || !value[0])
    {
        return defaultValue;
    }

    char* end = NULL;
    unsigned long parsed = strtoul(value, &end, 10);
    if (end == value)
    {
        return defaultValue;
    }
    if (parsed > (unsigned long)maxValue)
    {
        parsed = (unsigned long)maxValue;
    }
    return (uint32_t)parsed;
}

static uint32_t audioQueueDropAfterMs(void)
{
    static int initialized = 0;
    static uint32_t dropAfterMs = kAudioQueueDropDisabledMs;
    if (!initialized)
    {
        // Dropping saturated PCM buffers shortens the guest audio timeline.
        // Keep lossless backpressure by default; set the env var to a timeout
        // only when a sample needs bounded audio latency more than exact pacing.
        dropAfterMs = parseBoundedUintEnv(
            "DINGOO_PIE_AUDIO_QUEUE_DROP_MS",
            kAudioQueueDropDisabledMs,
            kAudioQueueDropMaxMs);
        initialized = 1;
    }
    return dropAfterMs;
}

static void resetAudioBackpressureLog(void)
{
    g_lastQueueBackpressureLogTicks = 0;
}

static SDL_mutex* audioMutex(void)
{
    if (!g_audioMutex)
    {
        g_audioMutex = SDL_CreateMutex();
    }
    return g_audioMutex;
}

static void lockAudio(void)
{
    SDL_mutex* mutex = audioMutex();
    if (mutex)
    {
        SDL_LockMutex(mutex);
    }
}

static void unlockAudio(void)
{
    SDL_mutex* mutex = audioMutex();
    if (mutex)
    {
        SDL_UnlockMutex(mutex);
    }
}

static Uint16 convertFormat(uint16_t format)
{
    switch (format)
    {
    case AFMT_U8:
        return AUDIO_U8;
    case AFMT_S16_LE:
        return AUDIO_S16LSB;
    default:
        return AUDIO_S16LSB;
    }
}

static uint32_t audioBytesPerSample(Uint16 format)
{
    switch (format & 0xff)
    {
    case 8:
        return 1;
    case 16:
        return 2;
    case 32:
        return 4;
    default:
        return 2;
    }
}

static uint32_t audioBytesPerSecondLocked(void)
{
    uint32_t channels = g_audioSpec.channels ? g_audioSpec.channels : 1;
    uint32_t bytes = audioBytesPerSample(g_audioSpec.format);
    uint32_t freq = g_audioSpec.freq > 0 ? (uint32_t)g_audioSpec.freq : 16000;
    return freq * channels * bytes;
}

static uint32_t maxQueuedAudioBytesLocked(void)
{
    uint32_t quarterSecond = audioBytesPerSecondLocked() / 4;
    uint32_t deviceBuffer = g_audioSpec.size ? g_audioSpec.size : 4096;
    return quarterSecond > deviceBuffer ? quarterSecond : deviceBuffer;
}

static bool outputMutedLocked(void)
{
    return g_frontendPauseRequested || g_guestMuteRequested || g_volume == 0 || g_masterVolumePercent == 0;
}

static void logAudioBackpressure(uint64_t nowTicks, uint64_t waitBeginTicks, bool dropping)
{
    if (g_lastQueueBackpressureLogTicks &&
        nowTicks - g_lastQueueBackpressureLogTicks < kQueueBackpressureLogIntervalMs)
    {
        return;
    }

    SDL_Log(dropping ?
        "Audio queue saturated for %u ms; dropping guest buffer" :
        "Audio queue saturated for %u ms; waiting for playback",
        (unsigned int)(nowTicks - waitBeginTicks));
    g_lastQueueBackpressureLogTicks = nowTicks;
}

static AudioQueueWaitResult waitForAudioQueueSpaceLocked(uint32_t maxQueued)
{
    uint32_t dropAfterMs = audioQueueDropAfterMs();
    uint64_t waitBeginTicks = SDL_GetTicks64();
    while (g_audioDevice && SDL_GetQueuedAudioSize(g_audioDevice) >= maxQueued)
    {
        unlockAudio();
        SDL_Delay(1);
        lockAudio();

        if (!g_audioDevice || outputMutedLocked())
        {
            return AUDIO_QUEUE_OUTPUT_STOPPED;
        }

        uint64_t nowTicks = SDL_GetTicks64();
        if (dropAfterMs > 0 && nowTicks - waitBeginTicks >= dropAfterMs)
        {
            logAudioBackpressure(nowTicks, waitBeginTicks, true);
            return AUDIO_QUEUE_DROP_BUFFER;
        }
        if (dropAfterMs == 0)
        {
            logAudioBackpressure(nowTicks, waitBeginTicks, false);
        }
    }

    return AUDIO_QUEUE_READY;
}

static int clampIntLocal(int value, int minValue, int maxValue)
{
    if (value < minValue)
    {
        return minValue;
    }
    if (value > maxValue)
    {
        return maxValue;
    }
    return value;
}

static int normalizeBufferSamples(int samples)
{
    switch (samples)
    {
    case 512:
    case 1024:
    case 2048:
    case 4096:
    case 8192:
        return samples;
    default:
        return 2048;
    }
}

static int effectiveVolumePercentLocked(void)
{
    uint32_t guestVolume = g_volume > 255 ? 255 : g_volume;
    // Dingoo samples commonly pass 0-100, while some SDK layers document 0-255.
    // Treat 0-100 as direct percent and only normalize larger values from 255.
    int guestPercent = guestVolume <= 100 ? (int)guestVolume : (int)((guestVolume * 100u + 127u) / 255u);
    int masterVolume = clampIntLocal(g_masterVolumePercent, 0, 150);
    return (guestPercent * masterVolume + 50) / 100;
}

static bool dropAudioEnvEnabled(void)
{
    const char* dropAudio = getenv("DINGOO_PIE_DROP_AUDIO");
    return dropAudio && dropAudio[0] && dropAudio[0] != '0';
}

static int clampS16(int value)
{
    if (value < -32768)
    {
        return -32768;
    }
    if (value > 32767)
    {
        return 32767;
    }
    return value;
}

static void applyVolumeInPlaceLocked(char* buffer, int count)
{
    int volumePercent = effectiveVolumePercentLocked();
    if (!buffer || count <= 0 || volumePercent == 100)
    {
        return;
    }

    if (volumePercent <= 0)
    {
        memset(buffer, 0, (size_t)count);
        return;
    }

    if (g_audioSpec.format == AUDIO_U8)
    {
        uint8_t* samples = (uint8_t*)buffer;
        for (int i = 0; i < count; ++i)
        {
            int centered = (int)samples[i] - 128;
            int scaled = 128 + (centered * volumePercent) / 100;
            samples[i] = (uint8_t)clampIntLocal(scaled, 0, 255);
        }
        return;
    }

    if (g_audioSpec.format == AUDIO_S16LSB)
    {
        int sampleCount = count / 2;
        int16_t* samples = (int16_t*)buffer;
        for (int i = 0; i < sampleCount; ++i)
        {
            samples[i] = (int16_t)clampS16(((int)samples[i] * volumePercent) / 100);
        }
    }
}

uint32_t MixerOpen(waveout_args* args)
{
    if (!args)
    {
        return 0;
    }

    lockAudio();
    if (g_audioDevice)
    {
        SDL_ClearQueuedAudio(g_audioDevice);
        SDL_CloseAudioDevice(g_audioDevice);
        g_audioDevice = 0;
    }

    g_volume = args->volume;
    g_dropAudio = false;
    resetAudioBackpressureLog();
    int bufferSamples = normalizeBufferSamples(g_bufferSamples);
    SDL_Log("Audio waveout open requested sample_rate=%u format=%u channels=%u buffer_samples=%d guest_volume=%u master_volume=%d%% effective_volume=%d%%",
        (unsigned int)args->sample_rate,
        (unsigned int)args->format,
        (unsigned int)args->channel,
        bufferSamples,
        g_volume,
        g_masterVolumePercent,
        effectiveVolumePercentLocked());

    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq = args->sample_rate;
    want.format = convertFormat(args->format);
    want.channels = args->channel ? args->channel : 2;
    want.samples = (Uint16)bufferSamples;
    want.callback = NULL;

    g_audioDevice = SDL_OpenAudioDevice(NULL, 0, &want, &g_audioSpec, 0);
    if (!g_audioDevice)
    {
        SDL_Log("Couldn't open audio: %s", SDL_GetError());
        SDL_Log("Audio output disabled; guest audio buffers will be dropped");
        g_dropAudio = true;
        unlockAudio();
        return 1;
    }

    SDL_Log("Opened audio at %d Hz, format 0x%x, channels %d, samples %d, guest_volume=%u master_volume=%d%% effective_volume=%d%%",
        g_audioSpec.freq, g_audioSpec.format, g_audioSpec.channels, g_audioSpec.samples, g_volume,
        g_masterVolumePercent, effectiveVolumePercentLocked());
    SDL_PauseAudioDevice(g_audioDevice, outputMutedLocked() ? 1 : 0);
    unlockAudio();
    return 1;
}

uint32_t MixerClose()
{
    lockAudio();
    if (g_audioDevice)
    {
        SDL_ClearQueuedAudio(g_audioDevice);
        SDL_CloseAudioDevice(g_audioDevice);
        g_audioDevice = 0;
        SDL_Log("Closed audio");
    }
    g_dropAudio = false;
    resetAudioBackpressureLog();
    unlockAudio();
    return 1;
}

uint32_t MixerWriteBuff(char* buffer, int count)
{
    if (!buffer || count <= 0)
    {
        free(buffer);
        return 0;
    }

    if (dropAudioEnvEnabled() || g_dropAudio || !g_audioDevice)
    {
        free(buffer);
        return 1;
    }

    lockAudio();
    if (!g_audioDevice || outputMutedLocked())
    {
        unlockAudio();
        free(buffer);
        return 1;
    }

    AudioQueueWaitResult waitResult = waitForAudioQueueSpaceLocked(maxQueuedAudioBytesLocked());
    if (waitResult != AUDIO_QUEUE_READY)
    {
        unlockAudio();
        free(buffer);
        return 1;
    }

    applyVolumeInPlaceLocked(buffer, count);
    int queued = g_audioDevice ? SDL_QueueAudio(g_audioDevice, buffer, (Uint32)count) : 0;
    unlockAudio();
    free(buffer);
    return queued == 0 ? 1 : 0;
}

uint32_t MixerPlaying()
{
    lockAudio();
    uint32_t canWrite = 1;
    if (dropAudioEnvEnabled() || g_dropAudio)
    {
        canWrite = 1;
    }
    else if (g_audioDevice && !outputMutedLocked())
    {
        canWrite = SDL_GetQueuedAudioSize(g_audioDevice) < maxQueuedAudioBytesLocked() ? 1 : 0;
    }
    unlockAudio();
    if (!canWrite)
    {
        SDL_Delay(1);
    }
    return canWrite;
}

bool MixerDropsAudio()
{
    if (dropAudioEnvEnabled())
    {
        return true;
    }

    lockAudio();
    bool dropsAudio = g_dropAudio || outputMutedLocked() || !g_audioDevice;
    unlockAudio();
    return dropsAudio;
}

void MixerSetVolume(uint32_t vol)
{
    lockAudio();
    g_volume = vol > 255 ? 255 : vol;
    if (g_audioDevice)
    {
        bool muted = outputMutedLocked();
        SDL_PauseAudioDevice(g_audioDevice, muted ? 1 : 0);
        if (muted)
        {
            SDL_ClearQueuedAudio(g_audioDevice);
        }
    }
    SDL_Log("Audio guest volume set to %u, master=%d%%, effective=%d%%%s",
        g_volume, g_masterVolumePercent, effectiveVolumePercentLocked(), outputMutedLocked() ? " muted" : "");
    unlockAudio();
}

void MixerSetMuted(bool muted)
{
    lockAudio();
    g_guestMuteRequested = muted;
    if (g_audioDevice)
    {
        bool outputMuted = outputMutedLocked();
        SDL_PauseAudioDevice(g_audioDevice, outputMuted ? 1 : 0);
        if (outputMuted)
        {
            SDL_ClearQueuedAudio(g_audioDevice);
        }
    }
    SDL_Log("Audio mute %s", g_guestMuteRequested ? "on" : "off");
    unlockAudio();
}

void MixerSetFrontendPaused(bool paused)
{
    lockAudio();
    g_frontendPauseRequested = paused;
    if (g_audioDevice)
    {
        bool outputMuted = outputMutedLocked();
        SDL_PauseAudioDevice(g_audioDevice, outputMuted ? 1 : 0);
        if (paused)
        {
            // Avoid replaying stale guest audio when gameplay resumes.
            SDL_ClearQueuedAudio(g_audioDevice);
        }
    }
    SDL_Log("Audio frontend pause %s", g_frontendPauseRequested ? "on" : "off");
    unlockAudio();
}

void MixerSetMasterVolumePercent(int percent)
{
    lockAudio();
    g_masterVolumePercent = clampIntLocal(percent, 0, 150);
    if (g_audioDevice)
    {
        SDL_PauseAudioDevice(g_audioDevice, outputMutedLocked() ? 1 : 0);
        SDL_ClearQueuedAudio(g_audioDevice);
    }
    SDL_Log("Audio master volume set to %d%%, guest=%u, effective=%d%%%s",
        g_masterVolumePercent, g_volume, effectiveVolumePercentLocked(), outputMutedLocked() ? " muted" : "");
    unlockAudio();
}

void MixerSetBufferSamples(int samples)
{
    lockAudio();
    g_bufferSamples = normalizeBufferSamples(samples);
    SDL_Log("Audio buffer samples set to %d", g_bufferSamples);
    unlockAudio();
}
