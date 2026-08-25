#ifndef DINGOO_PIE_RUNTIME_LOG_H
#define DINGOO_PIE_RUNTIME_LOG_H

#include <stdint.h>

bool runtimeLogEnvEnabled(const char* name);
void runtimeLogInitialize(bool profileEnabled, bool externalProfileEnabled);
void runtimeLogSetProfileEnabled(bool enabled);
void runtimeLogSetExternalProfileEnabled(bool enabled);
bool runtimeLogProfileEnabled(void);
bool runtimeLogExternalProfileEnabled(void);
uint64_t runtimeLogProfileIntervalMs(void);
uint64_t runtimeLogProfileIntervalUs(void);
uint64_t runtimeLogRatePerSecond(uint64_t count, uint64_t elapsedMs);
uint64_t runtimeLogRatePerSecondUs(uint64_t count, uint64_t elapsedUs);
bool runtimeLogShouldPrintEmptyProfile(void);

#endif
