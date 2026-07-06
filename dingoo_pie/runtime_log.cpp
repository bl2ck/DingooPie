#include "runtime_log.h"

#include <atomic>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static const uint64_t kDefaultProfileIntervalMs = 5000;
static const uint64_t kMinProfileIntervalMs = 1000;
static const uint64_t kMaxProfileIntervalMs = 60000;

static std::atomic<bool> s_profileEnabled(false);
static std::atomic<bool> s_externalProfileEnabled(false);
static std::atomic<uint64_t> s_profileIntervalMs(kDefaultProfileIntervalMs);

bool runtimeLogEnvEnabled(const char* name)
{
    const char* value = getenv(name);
    return value && value[0] && strcmp(value, "0") != 0;
}

static uint64_t runtimeLogReadProfileIntervalMs(void)
{
    const char* value = getenv("DINGOO_PIE_PROFILE_INTERVAL_MS");
    if (!value || !value[0])
    {
        return kDefaultProfileIntervalMs;
    }

    errno = 0;
    char* end = NULL;
    unsigned long parsed = strtoul(value, &end, 10);
    if (errno || end == value || (end && *end) || parsed == 0)
    {
        return kDefaultProfileIntervalMs;
    }
    if (parsed < kMinProfileIntervalMs)
    {
        return kMinProfileIntervalMs;
    }
    if (parsed > kMaxProfileIntervalMs)
    {
        return kMaxProfileIntervalMs;
    }
    return (uint64_t)parsed;
}

static void runtimeLogRefreshProfileInterval(void)
{
    s_profileIntervalMs.store(runtimeLogReadProfileIntervalMs());
}

void runtimeLogInitialize(bool profileEnabled, bool externalProfileEnabled)
{
    runtimeLogRefreshProfileInterval();
    s_externalProfileEnabled.store(externalProfileEnabled);
    s_profileEnabled.store(profileEnabled || externalProfileEnabled);
}

void runtimeLogSetProfileEnabled(bool enabled)
{
    runtimeLogRefreshProfileInterval();
    s_profileEnabled.store(enabled || s_externalProfileEnabled.load());
}

void runtimeLogSetExternalProfileEnabled(bool enabled)
{
    s_externalProfileEnabled.store(enabled);
}

bool runtimeLogProfileEnabled(void)
{
    return s_profileEnabled.load();
}

bool runtimeLogExternalProfileEnabled(void)
{
    return s_externalProfileEnabled.load();
}

uint64_t runtimeLogProfileIntervalMs(void)
{
    return s_profileIntervalMs.load();
}

uint64_t runtimeLogProfileIntervalUs(void)
{
    return runtimeLogProfileIntervalMs() * 1000ull;
}

uint64_t runtimeLogRatePerSecond(uint64_t count, uint64_t elapsedMs)
{
    if (!elapsedMs)
    {
        return 0;
    }
    return (uint64_t)(((long double)count * 1000.0L) / (long double)elapsedMs);
}

uint64_t runtimeLogRatePerSecondUs(uint64_t count, uint64_t elapsedUs)
{
    if (!elapsedUs)
    {
        return 0;
    }
    return (uint64_t)(((long double)count * 1000000.0L) / (long double)elapsedUs);
}

bool runtimeLogShouldPrintEmptyProfile(void)
{
    return runtimeLogEnvEnabled("DINGOO_PIE_PROFILE_EMPTY");
}
