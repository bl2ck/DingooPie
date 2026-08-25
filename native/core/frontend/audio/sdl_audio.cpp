#include "frontend/audio/sdl_audio.h"
#include "frontend/audio/audio_validation_capture.h"

#include <SDL2/SDL.h>
#include <math.h>
#include <stdint.h>
#include <deque>
#include <vector>
#include <stdlib.h>
#include <string.h>
#include <utility>

static const uint32_t kQueueBackpressureLogIntervalMs = 1000;
static const uint32_t kAudioQueueDropDisabledMs = 0;
static const uint32_t kAudioQueueDropMaxMs = 60000;
static const uint32_t kDefaultAudioBufferLatencyMs = 130;
static const uint32_t kPendingAudioMaxBytes = 512 * 1024;
static const int kAudioEffectStateChannels = 8;
static const int kStableHostSampleRate = 48000;
static const Uint8 kStableHostChannels = 2;
static const int kOutputConditionerChannels = 8;
static const int kOutputDiscontinuityThreshold = 12000;
static const int32_t kNoiseSuppressorCloseThreshold = 64;
static const int32_t kNoiseSuppressorOpenThreshold = 256;
static const int32_t kNoiseSuppressorFloorGain = 1024;

static SDL_AudioDeviceID g_audioDevice = 0;
static SDL_AudioSpec g_audioSpec;
static SDL_AudioSpec g_guestAudioSpec;
static SDL_AudioStream* g_audioStream = NULL;
static SDL_mutex* g_audioMutex = NULL;
static uint32_t g_volume = 100;
static int g_masterVolumePercent = 100;
static int g_bufferSamples = 2048;
static AudioBufferLatencyMode g_audioBufferLatencyMode = AUDIO_BUFFER_LATENCY_AUTO;
static AudioEffectMode g_audioEffect = AUDIO_EFFECT_OFF;
static DigitalNoiseReductionLevel g_digitalNoiseReduction =
    DIGITAL_NOISE_REDUCTION_HIGH;
static int32_t g_audioEffectState[kAudioEffectStateChannels] = {};
static bool g_audioEffectStateValid[kAudioEffectStateChannels] = {};
static bool g_guestMuteRequested = false;
static bool g_frontendPauseRequested = false;
static bool g_audioOutputUnavailable = false;
static bool g_gameAudioResourcesActive = false;
static uint64_t g_lastQueueBackpressureLogTicks = 0;
static std::deque<std::vector<char> > g_pendingAudio;
static uint32_t g_pendingAudioBytes = 0;
static std::vector<char> g_guestAudioRemainder;
static bool g_resampleLowPassEnabled = false;
static double g_resampleLowPassB0 = 0.0;
static double g_resampleLowPassB1 = 0.0;
static double g_resampleLowPassB2 = 0.0;
static double g_resampleLowPassA1 = 0.0;
static double g_resampleLowPassA2 = 0.0;
static uint64_t g_audioQueueExpectedEndTicks = 0;
static int32_t g_dcBlockPreviousInput[kOutputConditionerChannels] = {};
static int32_t g_dcBlockPreviousOutput[kOutputConditionerChannels] = {};
static bool g_dcBlockStateValid[kOutputConditionerChannels] = {};
static int16_t g_outputPreviousSample[kOutputConditionerChannels] = {};
static bool g_outputPreviousSampleValid[kOutputConditionerChannels] = {};
static int32_t g_noiseSuppressorEnvelope = 0;
static int32_t g_noiseSuppressorGain = 32768;

struct ResampleLowPassState
{
    double input1;
    double input2;
    double output1;
    double output2;
};

static ResampleLowPassState g_resampleLowPassState[kAudioEffectStateChannels] = {};

enum AudioQueueWaitResult
{
    AUDIO_QUEUE_READY,
    AUDIO_QUEUE_OUTPUT_STOPPED,
    AUDIO_QUEUE_DROP_BUFFER
};

static void applyQueueRecoveryFadeInLocked(char* buffer, int count,
    uint32_t queuedBytes);
static int audioFrameChannelsLocked(void);

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

static bool audioQueueTraceEnabled(void)
{
    static const bool enabled = []() {
        const char* value = getenv("DINGOO_PIE_AUDIO_QUEUE_TRACE");
        return value && value[0] && value[0] != '0';
    }();
    return enabled;
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

static uint32_t audioBufferLatencyMillisecondsLocked(void)
{
    if (g_audioBufferLatencyMode == AUDIO_BUFFER_LATENCY_AUTO)
    {
        return kDefaultAudioBufferLatencyMs;
    }
    return (uint32_t)emulatorAudioBufferLatencyMilliseconds(
        g_audioBufferLatencyMode);
}

static uint32_t maxQueuedAudioBytesLocked(void)
{
    uint32_t latencyTarget =
        (audioBytesPerSecondLocked() * audioBufferLatencyMillisecondsLocked()) / 1000;
    uint32_t deviceBuffer = g_audioSpec.size ? g_audioSpec.size : 4096;
    return latencyTarget > deviceBuffer ? latencyTarget : deviceBuffer;
}

static uint32_t maxPendingAudioBytesLocked(void)
{
    return kPendingAudioMaxBytes;
}

static void resetResampleLowPassLocked(void)
{
    memset(g_resampleLowPassState, 0, sizeof(g_resampleLowPassState));
}

static void configureResampleLowPassLocked(void)
{
    g_resampleLowPassEnabled = false;
    resetResampleLowPassLocked();
    if (g_digitalNoiseReduction != DIGITAL_NOISE_REDUCTION_HIGH ||
        g_guestAudioSpec.freq <= 0 || g_audioSpec.freq <= g_guestAudioSpec.freq ||
        g_audioSpec.format != AUDIO_S16LSB)
    {
        return;
    }

    const double cutoff = (double)g_guestAudioSpec.freq * 0.40;
    const double hostNyquist = (double)g_audioSpec.freq * 0.5;
    if (cutoff <= 0.0 || cutoff >= hostNyquist)
    {
        return;
    }

    const double pi = 3.14159265358979323846;
    const double omega = 2.0 * pi * cutoff / (double)g_audioSpec.freq;
    const double cosine = cos(omega);
    const double alpha = sin(omega) / sqrt(2.0);
    const double a0 = 1.0 + alpha;
    g_resampleLowPassB0 = ((1.0 - cosine) * 0.5) / a0;
    g_resampleLowPassB1 = (1.0 - cosine) / a0;
    g_resampleLowPassB2 = g_resampleLowPassB0;
    g_resampleLowPassA1 = (-2.0 * cosine) / a0;
    g_resampleLowPassA2 = (1.0 - alpha) / a0;
    g_resampleLowPassEnabled = true;
}

static void clearPendingAudioLocked(void)
{
    g_pendingAudio.clear();
    g_pendingAudioBytes = 0;
}

static void clearAudioStreamLocked(void)
{
    g_guestAudioRemainder.clear();
    g_resampleLowPassEnabled = false;
    resetResampleLowPassLocked();
    g_audioQueueExpectedEndTicks = 0;
    if (g_audioStream)
    {
        SDL_AudioStreamClear(g_audioStream);
    }
}

static bool configureAudioStreamLocked(const waveout_args* args)
{
    g_guestAudioRemainder.clear();
    g_audioQueueExpectedEndTicks = 0;
    if (g_audioStream)
    {
        SDL_FreeAudioStream(g_audioStream);
        g_audioStream = NULL;
    }

    SDL_zero(g_guestAudioSpec);
    g_guestAudioSpec.freq = args->sample_rate;
    g_guestAudioSpec.format = convertFormat(args->format);
    g_guestAudioSpec.channels = args->channel ? args->channel : 2;
    configureResampleLowPassLocked();
    if (g_guestAudioSpec.freq == g_audioSpec.freq &&
        g_guestAudioSpec.format == g_audioSpec.format &&
        g_guestAudioSpec.channels == g_audioSpec.channels)
    {
        return true;
    }

    g_audioStream = SDL_NewAudioStream(
        g_guestAudioSpec.format, g_guestAudioSpec.channels, g_guestAudioSpec.freq,
        g_audioSpec.format, g_audioSpec.channels, g_audioSpec.freq);
    if (!g_audioStream)
    {
        SDL_Log("Couldn't create audio conversion stream: %s", SDL_GetError());
        return false;
    }
    SDL_Log("Audio conversion enabled guest=%dHz/0x%x/%uch host=%dHz/0x%x/%uch",
        g_guestAudioSpec.freq, g_guestAudioSpec.format,
        (unsigned int)g_guestAudioSpec.channels,
        g_audioSpec.freq, g_audioSpec.format,
        (unsigned int)g_audioSpec.channels);
    return true;
}

static bool convertAudioBufferLocked(const char* buffer, int count,
    std::vector<char>* output)
{
    output->clear();
    if (!g_audioStream)
    {
        output->assign(buffer, buffer + count);
        return true;
    }

    const uint32_t frameBytes = audioBytesPerSample(g_guestAudioSpec.format) *
        (g_guestAudioSpec.channels ? g_guestAudioSpec.channels : 1u);
    const size_t totalBytes = g_guestAudioRemainder.size() + (size_t)count;
    const size_t alignedBytes = frameBytes ? totalBytes - totalBytes % frameBytes : totalBytes;
    std::vector<char> combined;
    const char* input = buffer;
    if (!g_guestAudioRemainder.empty() || alignedBytes != (size_t)count)
    {
        combined.reserve(totalBytes);
        combined.insert(combined.end(), g_guestAudioRemainder.begin(),
            g_guestAudioRemainder.end());
        combined.insert(combined.end(), buffer, buffer + count);
        input = combined.data();
    }
    if (!alignedBytes)
    {
        g_guestAudioRemainder.assign(input, input + totalBytes);
        return true;
    }
    if (SDL_AudioStreamPut(g_audioStream, input, (int)alignedBytes) != 0)
    {
        SDL_Log("Audio conversion input failed: %s", SDL_GetError());
        return false;
    }
    g_guestAudioRemainder.assign(input + alignedBytes, input + totalBytes);
    int available = SDL_AudioStreamAvailable(g_audioStream);
    if (available <= 0)
    {
        return available == 0;
    }
    output->resize((size_t)available);
    int converted = SDL_AudioStreamGet(g_audioStream, output->data(), available);
    if (converted < 0)
    {
        SDL_Log("Audio conversion output failed: %s", SDL_GetError());
        output->clear();
        return false;
    }
    output->resize((size_t)converted);
    return true;
}

static void flushPendingAudioLocked(void)
{
    while (g_audioDevice && !g_pendingAudio.empty() &&
        SDL_GetQueuedAudioSize(g_audioDevice) < maxQueuedAudioBytesLocked())
    {
        std::vector<char>& pending = g_pendingAudio.front();
        uint32_t queuedBytes = SDL_GetQueuedAudioSize(g_audioDevice);
        applyQueueRecoveryFadeInLocked(pending.data(), (int)pending.size(),
            queuedBytes);
        if (SDL_QueueAudio(g_audioDevice, pending.data(), (Uint32)pending.size()) != 0)
        {
            g_audioQueueExpectedEndTicks = 0;
            break;
        }
        g_pendingAudioBytes -= (uint32_t)pending.size();
        g_pendingAudio.pop_front();
    }
}

static bool outputMutedLocked(void)
{
    return g_frontendPauseRequested || g_guestMuteRequested || g_volume == 0 || g_masterVolumePercent == 0;
}

static bool outputMutedWithoutFrontendPauseLocked(void)
{
    return g_guestMuteRequested || g_volume == 0 || g_masterVolumePercent == 0;
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
    bool traceWaits = dropAfterMs == 0 && audioQueueTraceEnabled();
    uint64_t waitBeginTicks = SDL_GetTicks64();
    while (g_audioDevice && SDL_GetQueuedAudioSize(g_audioDevice) >= maxQueued)
    {
        unlockAudio();
        SDL_Delay(1);
        lockAudio();

        if (!g_audioDevice || !g_gameAudioResourcesActive || outputMutedLocked())
        {
            return AUDIO_QUEUE_OUTPUT_STOPPED;
        }

        uint64_t nowTicks = SDL_GetTicks64();
        if (dropAfterMs > 0 && nowTicks - waitBeginTicks >= dropAfterMs)
        {
            logAudioBackpressure(nowTicks, waitBeginTicks, true);
            audioValidationRecordDrop(SDL_GetQueuedAudioSize(g_audioDevice),
                g_pendingAudioBytes, nowTicks - waitBeginTicks);
            return AUDIO_QUEUE_DROP_BUFFER;
        }
        // Some games normally stream at the queue cap. Keep that path quiet
        // unless audio queue tracing is explicitly requested.
        if (traceWaits)
        {
            logAudioBackpressure(nowTicks, waitBeginTicks, false);
        }
    }

    uint64_t waitMs = SDL_GetTicks64() - waitBeginTicks;
    if (waitMs > 0)
    {
        audioValidationRecordWait(
            g_audioDevice ? SDL_GetQueuedAudioSize(g_audioDevice) : 0,
            g_pendingAudioBytes, waitMs);
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

static AudioEffectMode normalizeAudioEffect(AudioEffectMode effect)
{
    switch (effect)
    {
    case AUDIO_EFFECT_OFF:
    case AUDIO_EFFECT_SOFT:
    case AUDIO_EFFECT_CLEAR:
    case AUDIO_EFFECT_BASS_BOOST:
    case AUDIO_EFFECT_MONO:
        return effect;
    default:
        return AUDIO_EFFECT_OFF;
    }
}

static DigitalNoiseReductionLevel normalizeDigitalNoiseReduction(
    DigitalNoiseReductionLevel level)
{
    switch (level)
    {
    case DIGITAL_NOISE_REDUCTION_HIGH:
    case DIGITAL_NOISE_REDUCTION_MEDIUM:
    case DIGITAL_NOISE_REDUCTION_LOW:
        return level;
    default:
        return DIGITAL_NOISE_REDUCTION_HIGH;
    }
}

static int effectiveVolumePercentLocked(void)
{
    uint32_t guestVolume = g_volume > 255 ? 255 : g_volume;
    // Games mix percentage and legacy byte-scale values. Preserve explicit
    // 0-100 controls and treat larger legacy values as full volume so crossing
    // 100 cannot cause a sudden output drop.
    int guestPercent = guestVolume <= 100 ? (int)guestVolume : 100;
    int masterVolume = clampIntLocal(g_masterVolumePercent, 0, 150);
    return (guestPercent * masterVolume + 50) / 100;
}

static bool audioDisabledEnvEnabled(void)
{
    const char* audioDisabled = getenv("DINGOO_PIE_AUDIO_DISABLED");
    return audioDisabled && audioDisabled[0] && audioDisabled[0] != '0';
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

static void resetOutputConditionerLocked(void)
{
    memset(g_dcBlockPreviousInput, 0, sizeof(g_dcBlockPreviousInput));
    memset(g_dcBlockPreviousOutput, 0, sizeof(g_dcBlockPreviousOutput));
    memset(g_dcBlockStateValid, 0, sizeof(g_dcBlockStateValid));
    memset(g_outputPreviousSample, 0, sizeof(g_outputPreviousSample));
    memset(g_outputPreviousSampleValid, 0, sizeof(g_outputPreviousSampleValid));
    g_noiseSuppressorEnvelope = 0;
    g_noiseSuppressorGain = 32768;
}

static int32_t noiseSuppressorTargetGainLocked(int32_t envelope)
{
    if (envelope <= kNoiseSuppressorCloseThreshold)
    {
        return kNoiseSuppressorFloorGain;
    }
    if (envelope >= kNoiseSuppressorOpenThreshold)
    {
        return 32768;
    }

    return kNoiseSuppressorFloorGain +
        (envelope - kNoiseSuppressorCloseThreshold) *
        (32768 - kNoiseSuppressorFloorGain) /
        (kNoiseSuppressorOpenThreshold - kNoiseSuppressorCloseThreshold);
}

static int16_t softLimitS16(int32_t sample)
{
    const int32_t threshold = 28672;
    const int32_t ceiling = 32000;
    const int32_t magnitude = sample < 0 ? -sample : sample;
    if (magnitude <= threshold)
    {
        return (int16_t)sample;
    }

    const int32_t headroom = ceiling - threshold;
    const int32_t excess = magnitude - threshold;
    const int32_t compressed = threshold +
        (excess * headroom) / (excess + headroom);
    return (int16_t)(sample < 0 ? -compressed : compressed);
}

static void applyOutputConditionerS16Locked(char* buffer, int count)
{
    if (!buffer || count <= 0 || g_audioSpec.format != AUDIO_S16LSB)
    {
        return;
    }

    const int channels = audioFrameChannelsLocked();
    const int frameCount = count / (int)(sizeof(int16_t) * channels);
    if (channels <= 0 || frameCount <= 0)
    {
        return;
    }

    int16_t* samples = (int16_t*)buffer;
    const int smoothingFrames = g_audioSpec.freq > 0 ?
        clampIntLocal(g_audioSpec.freq / 2000, 8, 32) : 24;
    int32_t correction[kOutputConditionerChannels] = {};
    for (int channel = 0;
        channel < channels && channel < kOutputConditionerChannels; ++channel)
    {
        const int32_t first = samples[channel];
        if (g_outputPreviousSampleValid[channel] &&
            abs((int)first - (int)g_outputPreviousSample[channel]) >=
                kOutputDiscontinuityThreshold)
        {
            correction[channel] =
                (int32_t)g_outputPreviousSample[channel] - first;
        }
    }

    for (int frame = 0; frame < frameCount; ++frame)
    {
        int32_t frameOutput[kOutputConditionerChannels] = {};
        int32_t framePeak = 0;
        for (int channel = 0; channel < channels; ++channel)
        {
            const int stateChannel = channel % kOutputConditionerChannels;
            const int index = frame * channels + channel;
            int32_t input = samples[index];
            int32_t output;
            if (g_digitalNoiseReduction == DIGITAL_NOISE_REDUCTION_LOW)
            {
                output = input;
            }
            else if (!g_dcBlockStateValid[stateChannel])
            {
                g_dcBlockPreviousInput[stateChannel] = input;
                g_dcBlockPreviousOutput[stateChannel] = 0;
                g_dcBlockStateValid[stateChannel] = true;
                output = 0;
            }
            else
            {
                output = input - g_dcBlockPreviousInput[stateChannel] +
                    (g_dcBlockPreviousOutput[stateChannel] * 32700) / 32768;
                g_dcBlockPreviousInput[stateChannel] = input;
                g_dcBlockPreviousOutput[stateChannel] = output;
            }
            if (frame < smoothingFrames)
            {
                output += correction[stateChannel] *
                    (smoothingFrames - frame) / smoothingFrames;
            }
            frameOutput[stateChannel] = output;
            framePeak = std::max(framePeak, (int32_t)abs(output));
        }

        int32_t noiseSuppressorGain = 32768;
        if (g_digitalNoiseReduction == DIGITAL_NOISE_REDUCTION_HIGH)
        {
            if (framePeak >= g_noiseSuppressorEnvelope)
            {
                g_noiseSuppressorEnvelope = framePeak;
            }
            else
            {
                g_noiseSuppressorEnvelope = std::max(framePeak,
                    (g_noiseSuppressorEnvelope * 32760) / 32768);
            }
            const int32_t targetGain =
                noiseSuppressorTargetGainLocked(g_noiseSuppressorEnvelope);
            if (targetGain >= g_noiseSuppressorGain)
            {
                g_noiseSuppressorGain = targetGain;
            }
            else
            {
                g_noiseSuppressorGain +=
                    (targetGain - g_noiseSuppressorGain) / 512;
            }
            noiseSuppressorGain = g_noiseSuppressorGain;
        }

        for (int channel = 0; channel < channels; ++channel)
        {
            const int stateChannel = channel % kOutputConditionerChannels;
            const int index = frame * channels + channel;
            samples[index] = softLimitS16(
                frameOutput[stateChannel] * noiseSuppressorGain / 32768);
        }
    }

    for (int channel = 0;
        channel < channels && channel < kOutputConditionerChannels; ++channel)
    {
        g_outputPreviousSample[channel] =
            samples[(frameCount - 1) * channels + channel];
        g_outputPreviousSampleValid[channel] = true;
    }
}

static void applyResampleLowPassInPlaceLocked(char* buffer, int count)
{
    if (!g_resampleLowPassEnabled || !buffer || count <= 0)
    {
        return;
    }

    const int channels = audioFrameChannelsLocked();
    const int sampleCount = count / (int)sizeof(int16_t);
    int16_t* samples = (int16_t*)buffer;
    for (int index = 0; index < sampleCount; ++index)
    {
        ResampleLowPassState& state =
            g_resampleLowPassState[index % channels % kAudioEffectStateChannels];
        const double input = samples[index];
        const double output = g_resampleLowPassB0 * input +
            g_resampleLowPassB1 * state.input1 +
            g_resampleLowPassB2 * state.input2 -
            g_resampleLowPassA1 * state.output1 -
            g_resampleLowPassA2 * state.output2;
        state.input2 = state.input1;
        state.input1 = input;
        state.output2 = state.output1;
        state.output1 = output;
        samples[index] = (int16_t)clampS16((int)output);
    }
}

static void resetAudioEffectStateLocked(void)
{
    memset(g_audioEffectState, 0, sizeof(g_audioEffectState));
    memset(g_audioEffectStateValid, 0, sizeof(g_audioEffectStateValid));
}

static void resetAudioProcessingStateLocked(void)
{
    resetAudioEffectStateLocked();
    resetOutputConditionerLocked();
}

static int audioFrameChannelsLocked(void)
{
    int channels = g_audioSpec.channels > 0 ? (int)g_audioSpec.channels : 1;
    return channels > 0 ? channels : 1;
}

static int16_t applyAudioEffectSampleLocked(int16_t sample, int channel)
{
    const int stateChannel = channel % kAudioEffectStateChannels;
    if (!g_audioEffectStateValid[stateChannel])
    {
        g_audioEffectState[stateChannel] = sample;
        g_audioEffectStateValid[stateChannel] = true;
    }

    const int32_t previous = g_audioEffectState[stateChannel];
    int32_t output = sample;
    switch (g_audioEffect)
    {
    case AUDIO_EFFECT_SOFT:
        output = (previous * 3 + sample) / 4;
        g_audioEffectState[stateChannel] = output;
        break;
    case AUDIO_EFFECT_CLEAR:
    {
        const int32_t low = (previous * 3 + sample) / 4;
        output = sample + (sample - low) / 2;
        g_audioEffectState[stateChannel] = low;
        break;
    }
    case AUDIO_EFFECT_BASS_BOOST:
    {
        const int32_t low = (previous * 15 + sample) / 16;
        output = sample + low / 4;
        g_audioEffectState[stateChannel] = low;
        break;
    }
    default:
        break;
    }
    return (int16_t)clampS16((int)output);
}

static void applyMonoEffectS16Locked(int16_t* samples, int sampleCount, int channels)
{
    if (!samples || sampleCount <= 0 || channels < 2)
    {
        return;
    }

    for (int frame = 0; frame + channels <= sampleCount; frame += channels)
    {
        int32_t sum = 0;
        for (int channel = 0; channel < channels; ++channel)
        {
            sum += samples[frame + channel];
        }
        const int16_t mixed = (int16_t)clampS16((int)(sum / channels));
        for (int channel = 0; channel < channels; ++channel)
        {
            samples[frame + channel] = mixed;
        }
    }
}

static void applyMonoEffectU8Locked(uint8_t* samples, int sampleCount, int channels)
{
    if (!samples || sampleCount <= 0 || channels < 2)
    {
        return;
    }

    for (int frame = 0; frame + channels <= sampleCount; frame += channels)
    {
        int32_t sum = 0;
        for (int channel = 0; channel < channels; ++channel)
        {
            sum += (int)samples[frame + channel] - 128;
        }
        const int mixed = clampIntLocal(128 + (int)(sum / channels), 0, 255);
        for (int channel = 0; channel < channels; ++channel)
        {
            samples[frame + channel] = (uint8_t)mixed;
        }
    }
}

static void applyAudioEffectInPlaceLocked(char* buffer, int count)
{
    if (!buffer || count <= 0 || g_audioEffect == AUDIO_EFFECT_OFF)
    {
        return;
    }

    const int channels = audioFrameChannelsLocked();
    switch (g_audioSpec.format)
    {
    case AUDIO_U8:
    {
        uint8_t* samples = (uint8_t*)buffer;
        const int sampleCount = count;
        if (g_audioEffect == AUDIO_EFFECT_MONO)
        {
            applyMonoEffectU8Locked(samples, sampleCount, channels);
            return;
        }

        for (int i = 0; i < sampleCount; ++i)
        {
            const int16_t centered = (int16_t)(((int)samples[i] - 128) << 8);
            const int16_t processed = applyAudioEffectSampleLocked(centered, i % channels);
            samples[i] = (uint8_t)clampIntLocal(128 + ((int)processed >> 8), 0, 255);
        }
        return;
    }
    case AUDIO_S16LSB:
    {
        const int sampleCount = count / 2;
        int16_t* samples = (int16_t*)buffer;
        if (g_audioEffect == AUDIO_EFFECT_MONO)
        {
            applyMonoEffectS16Locked(samples, sampleCount, channels);
            return;
        }

        for (int i = 0; i < sampleCount; ++i)
        {
            samples[i] = applyAudioEffectSampleLocked(samples[i], i % channels);
        }
        break;
    }
    default:
        break;
    }
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

static void applyQueueRecoveryFadeInLocked(char* buffer, int count,
    uint32_t queuedBytes)
{
    if (!buffer || count <= 0 || g_audioSpec.freq <= 0)
    {
        return;
    }

    const uint64_t now = SDL_GetTicks64();
    const uint64_t deviceBufferGraceMs = g_audioSpec.samples ?
        ((uint64_t)g_audioSpec.samples * 1000u + (uint64_t)g_audioSpec.freq - 1u) /
            (uint64_t)g_audioSpec.freq : 0;
    const bool hardwareBufferMayBePlaying =
        g_audioQueueExpectedEndTicks != 0 &&
        (g_audioQueueExpectedEndTicks >= now ||
            now - g_audioQueueExpectedEndTicks <= deviceBufferGraceMs);
    const uint64_t bytesPerSecond = audioBytesPerSecondLocked();
    const uint64_t bufferedBytes = (uint64_t)queuedBytes + (uint64_t)count;
    g_audioQueueExpectedEndTicks = now + (bytesPerSecond ?
        (bufferedBytes * 1000u + bytesPerSecond - 1u) / bytesPerSecond : 0);
    if (queuedBytes != 0 || hardwareBufferMayBePlaying)
    {
        return;
    }

    const int channels = audioFrameChannelsLocked();
    const int fadeFrames = g_audioSpec.freq * 4 / 1000;
    if (channels <= 0 || fadeFrames <= 1)
    {
        return;
    }

    if (g_audioSpec.format == AUDIO_S16LSB)
    {
        int16_t* samples = (int16_t*)buffer;
        const int frameCount = count / (int)(sizeof(int16_t) * channels);
        const int frames = frameCount < fadeFrames ? frameCount : fadeFrames;
        for (int frame = 0; frame < frames; ++frame)
        {
            for (int channel = 0; channel < channels; ++channel)
            {
                int index = frame * channels + channel;
                samples[index] = (int16_t)(((int32_t)samples[index] * frame) /
                    (fadeFrames - 1));
            }
        }
    }
    else if (g_audioSpec.format == AUDIO_U8)
    {
        uint8_t* samples = (uint8_t*)buffer;
        const int frameCount = count / channels;
        const int frames = frameCount < fadeFrames ? frameCount : fadeFrames;
        for (int frame = 0; frame < frames; ++frame)
        {
            for (int channel = 0; channel < channels; ++channel)
            {
                int index = frame * channels + channel;
                int centered = (int)samples[index] - 128;
                samples[index] = (uint8_t)(128 + centered * frame /
                    (fadeFrames - 1));
            }
        }
    }
}

uint32_t audioOutputOpen(waveout_args* args)
{
    if (!args)
    {
        return 0;
    }

    lockAudio();
    if (g_audioDevice)
    {
        g_volume = args->volume;
        g_audioOutputUnavailable = false;
        g_gameAudioResourcesActive = true;
        resetAudioProcessingStateLocked();
        resetAudioBackpressureLog();
        clearPendingAudioLocked();
        SDL_ClearQueuedAudio(g_audioDevice);
        if (!configureAudioStreamLocked(args))
        {
            g_audioOutputUnavailable = true;
            unlockAudio();
            return 1;
        }
        audioValidationBegin(g_audioSpec);
        SDL_PauseAudioDevice(g_audioDevice, outputMutedLocked() ? 1 : 0);
        SDL_Log("Reusing audio device for new guest runtime");
        unlockAudio();
        return 1;
    }

    g_volume = args->volume;
    g_audioOutputUnavailable = false;
    g_gameAudioResourcesActive = true;
    resetAudioProcessingStateLocked();
    resetAudioBackpressureLog();
    int bufferSamples = normalizeBufferSamples(g_bufferSamples);
    SDL_Log(
        "Audio waveout open requested sample_rate=%u format=%u channels=%u "
        "buffer_samples=%d guest_volume=%u master_volume=%d%% effective_volume=%d%%",
        (unsigned int)args->sample_rate,
        (unsigned int)args->format,
        (unsigned int)args->channel,
        bufferSamples,
        g_volume,
        g_masterVolumePercent,
        effectiveVolumePercentLocked());

    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq = kStableHostSampleRate;
    want.format = AUDIO_S16LSB;
    want.channels = kStableHostChannels;
    want.samples = (Uint16)bufferSamples;
    want.callback = NULL;

    SDL_Log("Audio output profile=stable_host output_request=%dHz/0x%x/%uch",
        want.freq, want.format, (unsigned int)want.channels);

    const int allowedChanges = SDL_AUDIO_ALLOW_FREQUENCY_CHANGE |
        SDL_AUDIO_ALLOW_FORMAT_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE;
    g_audioDevice = SDL_OpenAudioDevice(NULL, 0, &want, &g_audioSpec, allowedChanges);
    if (!g_audioDevice)
    {
        SDL_Log("Guest audio device open failed: %s; retrying standard host format",
            SDL_GetError());
        SDL_AudioSpec hostWant;
        SDL_zero(hostWant);
        hostWant.freq = 48000;
        hostWant.format = AUDIO_S16LSB;
        hostWant.channels = 2;
        hostWant.samples = (Uint16)bufferSamples;
        hostWant.callback = NULL;
        g_audioDevice = SDL_OpenAudioDevice(
            NULL, 0, &hostWant, &g_audioSpec, allowedChanges);
    }
    if (!g_audioDevice)
    {
        SDL_Log("Couldn't open audio: %s", SDL_GetError());
        SDL_Log("Audio output disabled; guest audio buffers will be dropped");
        g_audioOutputUnavailable = true;
        unlockAudio();
        return 1;
    }

    if (!configureAudioStreamLocked(args))
    {
        SDL_CloseAudioDevice(g_audioDevice);
        g_audioDevice = 0;
        g_audioOutputUnavailable = true;
        unlockAudio();
        return 1;
    }

    audioValidationBegin(g_audioSpec);
    SDL_Log("Opened audio at %d Hz, format 0x%x, channels %d, samples %d, guest_volume=%u master_volume=%d%% effective_volume=%d%%",
        g_audioSpec.freq, g_audioSpec.format, g_audioSpec.channels, g_audioSpec.samples, g_volume,
        g_masterVolumePercent, effectiveVolumePercentLocked());
    SDL_PauseAudioDevice(g_audioDevice, outputMutedLocked() ? 1 : 0);
    unlockAudio();
    return 1;
}

uint32_t audioOutputClose()
{
    audioOutputReleaseGameResources();
    return 1;
}

void audioOutputReleaseGameResources(void)
{
    lockAudio();
    audioValidationClose();
    g_gameAudioResourcesActive = false;
    if (g_audioDevice)
    {
        SDL_PauseAudioDevice(g_audioDevice, 1);
        SDL_ClearQueuedAudio(g_audioDevice);
    }
    clearPendingAudioLocked();
    clearAudioStreamLocked();
    resetAudioProcessingStateLocked();
    resetAudioBackpressureLog();
    SDL_Log("Released game audio resources");
    unlockAudio();
}

void audioOutputResetAfterRuntimeStop(void)
{
    SDL_mutex* previousMutex = g_audioMutex;
    // Keep the SDL device alive across guest restarts because reopening some
    // host audio drivers can block for a noticeable amount of time.
    // Only reset guest-owned state and replace a mutex left locked by forced stop.
    g_audioOutputUnavailable = false;
    g_gameAudioResourcesActive = false;
    g_frontendPauseRequested = false;
    g_guestMuteRequested = false;
    resetAudioProcessingStateLocked();
    resetAudioBackpressureLog();
    audioValidationClose();
    clearPendingAudioLocked();
    clearAudioStreamLocked();
    g_audioMutex = SDL_CreateMutex();

    if (previousMutex)
    {
        if (SDL_TryLockMutex(previousMutex) == 0)
        {
            SDL_UnlockMutex(previousMutex);
            SDL_DestroyMutex(previousMutex);
        }
    }
}

void audioOutputPrepareApplicationExit(void)
{
    SDL_AudioDeviceID audioDevice = 0;
    SDL_AudioStream* audioStream = NULL;

    lockAudio();
    audioValidationClose();
    audioDevice = g_audioDevice;
    audioStream = g_audioStream;
    if (audioDevice)
    {
        SDL_PauseAudioDevice(audioDevice, 1);
        SDL_ClearQueuedAudio(audioDevice);
    }
    g_audioDevice = 0;
    g_audioStream = NULL;
    SDL_zero(g_audioSpec);
    SDL_zero(g_guestAudioSpec);
    g_audioOutputUnavailable = false;
    g_gameAudioResourcesActive = false;
    g_frontendPauseRequested = false;
    g_guestMuteRequested = false;
    clearPendingAudioLocked();
    resetAudioProcessingStateLocked();
    resetAudioBackpressureLog();
    unlockAudio();

    if (audioStream)
    {
        SDL_FreeAudioStream(audioStream);
    }
    if (audioDevice)
    {
        SDL_CloseAudioDevice(audioDevice);
    }
    SDL_Log("Application audio state released and device closed");
}

uint32_t audioOutputWriteBuffer(char* buffer, int count)
{
    if (!buffer || count <= 0)
    {
        free(buffer);
        return 0;
    }

    if (audioDisabledEnvEnabled() || g_audioOutputUnavailable || !g_audioDevice)
    {
        free(buffer);
        return 1;
    }

    lockAudio();
    if (!g_audioDevice || !g_gameAudioResourcesActive || outputMutedLocked())
    {
        unlockAudio();
        free(buffer);
        return 1;
    }

    std::vector<char> converted;
    bool convertedOk = convertAudioBufferLocked(buffer, count, &converted);
    free(buffer);
    if (!convertedOk)
    {
        unlockAudio();
        return 0;
    }
    if (converted.empty())
    {
        unlockAudio();
        return 1;
    }

    AudioQueueWaitResult waitResult = waitForAudioQueueSpaceLocked(maxQueuedAudioBytesLocked());
    if (waitResult != AUDIO_QUEUE_READY)
    {
        unlockAudio();
        return 1;
    }

    applyResampleLowPassInPlaceLocked(converted.data(), (int)converted.size());
    applyAudioEffectInPlaceLocked(converted.data(), (int)converted.size());
    applyVolumeInPlaceLocked(converted.data(), (int)converted.size());
    applyOutputConditionerS16Locked(converted.data(), (int)converted.size());
    uint32_t queuedBytes = SDL_GetQueuedAudioSize(g_audioDevice);
    applyQueueRecoveryFadeInLocked(converted.data(), (int)converted.size(),
        queuedBytes);
    audioValidationRecordAudio(converted.data(), (uint32_t)converted.size(),
        "queue", queuedBytes, g_pendingAudioBytes);
    int queued = SDL_QueueAudio(g_audioDevice, converted.data(), (Uint32)converted.size());
    if (queued != 0)
    {
        g_audioQueueExpectedEndTicks = 0;
        audioValidationRecordEvent("queue_error", (uint32_t)converted.size(),
            queuedBytes, g_pendingAudioBytes, 0);
    }
    unlockAudio();
    return queued == 0 ? 1 : 0;
}

uint32_t audioOutputTryWriteBuffer(char* buffer, int count)
{
    if (!buffer || count <= 0)
    {
        free(buffer);
        return 0;
    }

    if (audioDisabledEnvEnabled() || g_audioOutputUnavailable || !g_audioDevice)
    {
        free(buffer);
        return 1;
    }

    lockAudio();
    if (!g_audioDevice || !g_gameAudioResourcesActive || outputMutedLocked())
    {
        unlockAudio();
        free(buffer);
        return 1;
    }
    flushPendingAudioLocked();
    std::vector<char> converted;
    bool convertedOk = convertAudioBufferLocked(buffer, count, &converted);
    free(buffer);
    if (!convertedOk)
    {
        unlockAudio();
        return 0;
    }
    if (converted.empty())
    {
        unlockAudio();
        return 1;
    }
    if (SDL_GetQueuedAudioSize(g_audioDevice) >= maxQueuedAudioBytesLocked())
    {
        applyResampleLowPassInPlaceLocked(converted.data(), (int)converted.size());
        applyAudioEffectInPlaceLocked(converted.data(), (int)converted.size());
        applyVolumeInPlaceLocked(converted.data(), (int)converted.size());
        applyOutputConditionerS16Locked(converted.data(), (int)converted.size());
        audioValidationRecordAudio(converted.data(), (uint32_t)converted.size(),
            "pending", SDL_GetQueuedAudioSize(g_audioDevice),
            g_pendingAudioBytes);
        const uint32_t pendingLimit = maxPendingAudioBytesLocked();
        if (g_pendingAudioBytes <= pendingLimit &&
            converted.size() <= pendingLimit - g_pendingAudioBytes)
        {
            g_pendingAudio.push_back(std::move(converted));
            g_pendingAudioBytes += (uint32_t)g_pendingAudio.back().size();
            unlockAudio();
            return 1;
        }
        unlockAudio();
        return 0;
    }

    applyResampleLowPassInPlaceLocked(converted.data(), (int)converted.size());
    applyAudioEffectInPlaceLocked(converted.data(), (int)converted.size());
    applyVolumeInPlaceLocked(converted.data(), (int)converted.size());
    applyOutputConditionerS16Locked(converted.data(), (int)converted.size());
    uint32_t queuedBytes = SDL_GetQueuedAudioSize(g_audioDevice);
    applyQueueRecoveryFadeInLocked(converted.data(), (int)converted.size(),
        queuedBytes);
    audioValidationRecordAudio(converted.data(), (uint32_t)converted.size(),
        "queue", queuedBytes, g_pendingAudioBytes);
    int queued = SDL_QueueAudio(g_audioDevice, converted.data(), (Uint32)converted.size());
    if (queued != 0)
    {
        g_audioQueueExpectedEndTicks = 0;
        audioValidationRecordEvent("queue_error", (uint32_t)converted.size(),
            queuedBytes, g_pendingAudioBytes, 0);
    }
    unlockAudio();
    return queued == 0 ? 1 : 0;
}

uint32_t audioOutputIsPlaying()
{
    uint32_t canWrite = audioOutputCanWriteNonBlocking();
    if (!canWrite)
    {
        SDL_Delay(1);
    }
    return canWrite;
}

uint32_t audioOutputCanWriteNonBlocking()
{
    lockAudio();
    flushPendingAudioLocked();
    uint32_t canWrite = 1;
    if (audioDisabledEnvEnabled() || g_audioOutputUnavailable)
    {
        canWrite = 1;
    }
    else if (g_audioDevice && g_gameAudioResourcesActive && !outputMutedLocked())
    {
        canWrite = SDL_GetQueuedAudioSize(g_audioDevice) < maxQueuedAudioBytesLocked() &&
            g_pendingAudio.empty() ? 1 : 0;
    }
    unlockAudio();
    return canWrite;
}

bool audioOutputSkipsGuestOutput()
{
    if (audioDisabledEnvEnabled())
    {
        return true;
    }

    lockAudio();
    bool skipsAudioOutput = g_audioOutputUnavailable || outputMutedLocked() || !g_audioDevice;
    unlockAudio();
    return skipsAudioOutput;
}

void audioOutputSetGuestVolume(uint32_t vol)
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
            clearPendingAudioLocked();
            clearAudioStreamLocked();
        }
    }
    SDL_Log("Audio guest volume set to %u, master=%d%%, effective=%d%%%s",
        g_volume, g_masterVolumePercent, effectiveVolumePercentLocked(), outputMutedLocked() ? " muted" : "");
    unlockAudio();
}

void audioOutputSetMuted(bool muted)
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
            clearPendingAudioLocked();
            clearAudioStreamLocked();
        }
    }
    SDL_Log("Audio mute %s", g_guestMuteRequested ? "on" : "off");
    unlockAudio();
}

void audioOutputSetFrontendPaused(bool paused)
{
    lockAudio();
    g_frontendPauseRequested = paused;
    if (g_audioDevice)
    {
        bool outputMuted = outputMutedWithoutFrontendPauseLocked();
        SDL_PauseAudioDevice(g_audioDevice, outputMuted ? 1 : 0);
        if (paused)
        {
            clearPendingAudioLocked();
        }
    }
    SDL_Log("Audio frontend pause %s", g_frontendPauseRequested ? "on" : "off");
    unlockAudio();
}

void audioOutputSetMasterVolumePercent(int percent)
{
    lockAudio();
    g_masterVolumePercent = clampIntLocal(percent, 0, 150);
    if (g_audioDevice)
    {
        SDL_PauseAudioDevice(g_audioDevice, outputMutedLocked() ? 1 : 0);
        SDL_ClearQueuedAudio(g_audioDevice);
        clearPendingAudioLocked();
        clearAudioStreamLocked();
    }
    SDL_Log("Audio master volume set to %d%%, guest=%u, effective=%d%%%s",
        g_masterVolumePercent, g_volume, effectiveVolumePercentLocked(), outputMutedLocked() ? " muted" : "");
    unlockAudio();
}

void audioOutputSetBufferSamples(int samples)
{
    lockAudio();
    g_bufferSamples = normalizeBufferSamples(samples);
    SDL_Log("Audio buffer samples set to %d", g_bufferSamples);
    unlockAudio();
}

void audioOutputSetBufferLatencyMode(AudioBufferLatencyMode mode)
{
    lockAudio();
    if (mode < AUDIO_BUFFER_LATENCY_AUTO || mode >= AUDIO_BUFFER_LATENCY_MODE_COUNT)
    {
        mode = AUDIO_BUFFER_LATENCY_AUTO;
    }
    g_audioBufferLatencyMode = mode;
    unlockAudio();
}

void audioOutputSetEffect(AudioEffectMode effect)
{
    lockAudio();
    effect = normalizeAudioEffect(effect);
    if (g_audioEffect != effect)
    {
        g_audioEffect = effect;
        resetAudioEffectStateLocked();
        if (g_audioDevice)
        {
            SDL_ClearQueuedAudio(g_audioDevice);
            clearPendingAudioLocked();
            clearAudioStreamLocked();
        }
    }
    SDL_Log("Audio effect set to %s", emulatorAudioEffectName(g_audioEffect));
    unlockAudio();
}

void audioOutputSetNoiseReduction(DigitalNoiseReductionLevel level)
{
    lockAudio();
    level = normalizeDigitalNoiseReduction(level);
    if (g_digitalNoiseReduction != level)
    {
        g_digitalNoiseReduction = level;
        resetOutputConditionerLocked();
        configureResampleLowPassLocked();
        if (g_audioDevice)
        {
            SDL_ClearQueuedAudio(g_audioDevice);
            clearPendingAudioLocked();
            if (g_audioStream)
            {
                SDL_AudioStreamClear(g_audioStream);
            }
        }
    }
    SDL_Log("Digital noise reduction set to %s",
        emulatorDigitalNoiseReductionName(g_digitalNoiseReduction));
    unlockAudio();
}

void audioOutputRecordInput(uint32_t controlMask)
{
    lockAudio();
    audioValidationRecordEvent("input", controlMask,
        g_audioDevice ? SDL_GetQueuedAudioSize(g_audioDevice) : 0,
        g_pendingAudioBytes, 0);
    unlockAudio();
}

void audioOutputSetValidationCaptureEnabled(bool enabled)
{
    lockAudio();
    audioValidationSetEnabled(enabled);
    SDL_Log("Audio validation capture %s", enabled ? "enabled" : "disabled");
    unlockAudio();
}
