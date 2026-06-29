#ifndef DINGOO_PIE_PAUSE_GATE_H
#define DINGOO_PIE_PAUSE_GATE_H

#include <stdint.h>

// Runtime pause gate shared by guest execution paths. The frontend thread stays
// responsive while guest threads block here until the user resumes gameplay.
void pauseGateSetPaused(bool paused);
bool pauseGateWaitForPaused(uint32_t timeoutMs);
bool pauseGateWaitForPausedWaiters(uint32_t timeoutMs, uint32_t minimumWaiters);
bool pauseGateWaitForResume(void);
uint32_t pauseGateWaiterCount(void);
void pauseGateMarkRuntimeRestored(void);
uint32_t pauseGateRestoreGeneration(void);

#endif
