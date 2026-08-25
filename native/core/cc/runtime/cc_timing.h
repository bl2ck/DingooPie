#ifndef DINGOO_PIE_CC_RUNTIME_CC_TIMING_H
#define DINGOO_PIE_CC_RUNTIME_CC_TIMING_H

#include <stdint.h>

static inline uint64_t ccScaleElapsedMicros(uint64_t hostMicros,
    double runtimeSpeedScale)
{
    // Auto and slow presets expose guest time at the same rate as instruction
    // execution. Values at or above 1 keep the host clock unchanged.
    if (runtimeSpeedScale > 0.0 && runtimeSpeedScale < 1.0)
    {
        return (uint64_t)((double)hostMicros * runtimeSpeedScale);
    }
    return hostMicros;
}

static inline uint64_t ccTaskSchedulerElapsedMicros(uint64_t hostMicros,
    double runtimeSpeedScale, bool audioProducer)
{
    return audioProducer ? hostMicros :
        ccScaleElapsedMicros(hostMicros, runtimeSpeedScale);
}

static inline uint32_t ccMillisecondsToOsTicks(uint64_t milliseconds,
    uint32_t ticksPerSecond)
{
    if (!ticksPerSecond)
    {
        return 1u;
    }
    uint64_t ticks = (milliseconds * ticksPerSecond + 999u) / 1000u;
    if (!ticks)
    {
        return 1u;
    }
    return ticks > UINT32_MAX ? UINT32_MAX : (uint32_t)ticks;
}

static inline uint32_t ccHmsmToOsTicks(uint32_t hours, uint32_t minutes,
    uint32_t seconds, uint32_t milliseconds, uint32_t ticksPerSecond)
{
    uint64_t totalSeconds = (uint64_t)hours * 3600u +
        (uint64_t)minutes * 60u + seconds;
    uint64_t totalMilliseconds = totalSeconds * 1000u + milliseconds;
    return ccMillisecondsToOsTicks(totalMilliseconds, ticksPerSecond);
}

static inline uint32_t ccScaleDelayTicks(uint32_t ticks, double delayScale)
{
    if (!ticks)
    {
        ticks = 1u;
    }
    if (delayScale <= 0.0)
    {
        return 1u;
    }
    double scaled = (double)ticks * delayScale;
    if (scaled >= (double)UINT32_MAX)
    {
        return UINT32_MAX;
    }
    uint32_t result = (uint32_t)scaled;
    if ((double)result < scaled)
    {
        ++result;
    }
    return result ? result : 1u;
}

static inline uint64_t ccCpuClockToTargetIps(uint64_t cpuClockHz,
    uint64_t referenceClockHz, uint64_t referenceIps)
{
    if (!cpuClockHz || !referenceClockHz || !referenceIps)
    {
        return 0;
    }
    return referenceIps * cpuClockHz / referenceClockHz;
}

#endif
