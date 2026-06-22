#ifndef DINGOO_PIE_PAUSE_GATE_H
#define DINGOO_PIE_PAUSE_GATE_H

// Runtime pause gate shared by guest execution paths. The frontend thread stays
// responsive while guest threads block here until the user resumes gameplay.
void pauseGateSetPaused(bool paused);
bool pauseGateWaitForResume(void);

#endif
