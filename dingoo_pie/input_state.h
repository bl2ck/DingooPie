#ifndef DINGOO_PIE_INPUT_STATE_H
#define DINGOO_PIE_INPUT_STATE_H

#include <stdint.h>

typedef struct {
    unsigned long pressed;
    unsigned long released;
    unsigned long status;
} KEY_STATUS;

void _kbd_get_status(KEY_STATUS* ks);
uint32_t _kbd_get_key(void);
uint32_t inputGetCurrentStatus(void);
uint32_t inputHasPendingEvent(void);

#endif
