#include "frontend/audio/audio_validation_capture.h"
#include "shared/platform/storage_services.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <string>

struct AudioValidationCapture
{
    bool enabled;
    FILE* waveFile;
    FILE* eventFile;
    uint32_t waveBytes;
    uint64_t startTicks;
    uint64_t lastFlushTicks;
    uint64_t bufferCount;
    uint64_t queueWaitCount;
    uint64_t queueWaitTotalMs;
    uint64_t queueWaitMaxMs;
    uint64_t droppedBufferCount;
};

static AudioValidationCapture g_capture = {};
static void writeUint16(FILE* file, uint16_t value)
{
    uint8_t bytes[2] = { (uint8_t)value, (uint8_t)(value >> 8) };
    fwrite(bytes, 1, sizeof(bytes), file);
}

static void writeUint32(FILE* file, uint32_t value)
{
    uint8_t bytes[4] = {
        (uint8_t)value,
        (uint8_t)(value >> 8),
        (uint8_t)(value >> 16),
        (uint8_t)(value >> 24)
    };
    fwrite(bytes, 1, sizeof(bytes), file);
}

static void refreshWaveHeader(void)
{
    if (!g_capture.waveFile)
    {
        return;
    }

    long position = ftell(g_capture.waveFile);
    fseek(g_capture.waveFile, 4, SEEK_SET);
    writeUint32(g_capture.waveFile, 36u + g_capture.waveBytes);
    fseek(g_capture.waveFile, 40, SEEK_SET);
    writeUint32(g_capture.waveFile, g_capture.waveBytes);
    fseek(g_capture.waveFile, position, SEEK_SET);
    fflush(g_capture.waveFile);
    if (g_capture.eventFile)
    {
        fflush(g_capture.eventFile);
    }
}

static void flushCapture(void)
{
    if (g_capture.waveFile)
    {
        fflush(g_capture.waveFile);
    }
    if (g_capture.eventFile)
    {
        fflush(g_capture.eventFile);
    }
    g_capture.lastFlushTicks = SDL_GetTicks64();
}

void audioValidationClose(void)
{
    refreshWaveHeader();
    if (g_capture.waveFile)
    {
        fclose(g_capture.waveFile);
        g_capture.waveFile = NULL;
    }
    if (g_capture.eventFile)
    {
        fclose(g_capture.eventFile);
        g_capture.eventFile = NULL;
    }
    if (g_capture.bufferCount || g_capture.droppedBufferCount)
    {
        SDL_Log("Audio validation captured bytes=%u buffers=%llu waits=%llu "
            "wait_total_ms=%llu wait_max_ms=%llu drops=%llu",
            g_capture.waveBytes,
            (unsigned long long)g_capture.bufferCount,
            (unsigned long long)g_capture.queueWaitCount,
            (unsigned long long)g_capture.queueWaitTotalMs,
            (unsigned long long)g_capture.queueWaitMaxMs,
            (unsigned long long)g_capture.droppedBufferCount);
    }
}

void audioValidationSetEnabled(bool enabled)
{
    g_capture.enabled = enabled;
    if (!enabled)
    {
        audioValidationClose();
    }
}

void audioValidationBegin(const SDL_AudioSpec& audioSpec)
{
    audioValidationClose();
    g_capture.waveBytes = 0;
    g_capture.startTicks = SDL_GetTicks64();
    g_capture.lastFlushTicks = g_capture.startTicks;
    g_capture.bufferCount = 0;
    g_capture.queueWaitCount = 0;
    g_capture.queueWaitTotalMs = 0;
    g_capture.queueWaitMaxMs = 0;
    g_capture.droppedBufferCount = 0;

    if (!g_capture.enabled)
    {
        return;
    }

    const uint16_t bitsPerSample = (uint16_t)SDL_AUDIO_BITSIZE(audioSpec.format);
    if ((audioSpec.format != AUDIO_U8 && audioSpec.format != AUDIO_S16LSB) ||
        audioSpec.freq <= 0 || !audioSpec.channels)
    {
        SDL_Log("Audio validation unsupported output format=%x rate=%d channels=%u",
            audioSpec.format, audioSpec.freq, (unsigned int)audioSpec.channels);
        return;
    }

    std::string logDirectory = platformGetLogDirectory();
    std::string wavePath = logDirectory + "/dingoopie-audio-validation.wav";
    std::string eventPath = logDirectory + "/dingoopie-audio-validation.csv";
    g_capture.waveFile = logDirectory.empty() ? NULL : fopen(wavePath.c_str(), "wb+");
    g_capture.eventFile = logDirectory.empty() ? NULL : fopen(eventPath.c_str(), "w");
    if (!g_capture.waveFile || !g_capture.eventFile)
    {
        SDL_Log("Audio validation output open failed");
        audioValidationClose();
        return;
    }

    const uint16_t blockAlign = (uint16_t)(
        audioSpec.channels * (bitsPerSample / 8u));
    fwrite("RIFF", 1, 4, g_capture.waveFile);
    writeUint32(g_capture.waveFile, 36);
    fwrite("WAVEfmt ", 1, 8, g_capture.waveFile);
    writeUint32(g_capture.waveFile, 16);
    writeUint16(g_capture.waveFile, 1);
    writeUint16(g_capture.waveFile, audioSpec.channels);
    writeUint32(g_capture.waveFile, (uint32_t)audioSpec.freq);
    writeUint32(g_capture.waveFile, (uint32_t)audioSpec.freq * blockAlign);
    writeUint16(g_capture.waveFile, blockAlign);
    writeUint16(g_capture.waveFile, bitsPerSample);
    fwrite("data", 1, 4, g_capture.waveFile);
    writeUint32(g_capture.waveFile, 0);
    fprintf(g_capture.eventFile,
        "elapsed_ms,event,bytes,queued_bytes,pending_bytes,wait_ms\n");
    fprintf(g_capture.eventFile, "0,open,%u,0,0,0\n", audioSpec.size);
    SDL_Log("Audio validation capture started rate=%d format=0x%x channels=%u",
        audioSpec.freq, audioSpec.format, (unsigned int)audioSpec.channels);
}

void audioValidationRecordEvent(const char* eventName, uint32_t bytes,
    uint32_t queuedBytes, uint32_t pendingBytes, uint64_t waitMs)
{
    if (!g_capture.eventFile)
    {
        return;
    }
    fprintf(g_capture.eventFile, "%llu,%s,%u,%u,%u,%llu\n",
        (unsigned long long)(SDL_GetTicks64() - g_capture.startTicks),
        eventName, bytes, queuedBytes, pendingBytes,
        (unsigned long long)waitMs);
}

void audioValidationRecordAudio(const void* data, uint32_t bytes,
    const char* eventName, uint32_t queuedBytes, uint32_t pendingBytes)
{
    if (!g_capture.waveFile || !data || !bytes)
    {
        return;
    }
    fwrite(data, 1, bytes, g_capture.waveFile);
    g_capture.waveBytes += bytes;
    ++g_capture.bufferCount;
    audioValidationRecordEvent(eventName, bytes, queuedBytes, pendingBytes, 0);
    if (SDL_GetTicks64() - g_capture.lastFlushTicks >= 1000)
    {
        flushCapture();
    }
}

void audioValidationRecordWait(uint32_t queuedBytes, uint32_t pendingBytes,
    uint64_t waitMs)
{
    ++g_capture.queueWaitCount;
    g_capture.queueWaitTotalMs += waitMs;
    if (waitMs > g_capture.queueWaitMaxMs)
    {
        g_capture.queueWaitMaxMs = waitMs;
    }
    audioValidationRecordEvent("wait", 0, queuedBytes, pendingBytes, waitMs);
}

void audioValidationRecordDrop(uint32_t queuedBytes, uint32_t pendingBytes,
    uint64_t waitMs)
{
    ++g_capture.droppedBufferCount;
    audioValidationRecordEvent("drop", 0, queuedBytes, pendingBytes, waitMs);
}
