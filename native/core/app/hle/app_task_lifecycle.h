#ifndef DINGOO_PIE_APP_HLE_APP_TASK_LIFECYCLE_H
#define DINGOO_PIE_APP_HLE_APP_TASK_LIFECYCLE_H

#include <stddef.h>

void taskThreadLifecycleBegin(void);
void taskThreadLifecycleCancelBegin(void);
void taskThreadLifecycleDetachCurrent(void);
void taskThreadLifecycleFinish(void);
void taskThreadLifecycleWaitForAll(void);
size_t taskThreadLifecycleActiveCount(void);

#endif
