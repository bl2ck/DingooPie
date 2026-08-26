#ifndef DINGOO_PIE_FRONTEND_INPUT_INPUT_STATE_H
#define DINGOO_PIE_FRONTEND_INPUT_INPUT_STATE_H

#include <stdint.h>

struct GuestKeyStatus
{
    uint32_t pressed;
    uint32_t released;
    uint32_t status;
};

void _kbd_get_status(GuestKeyStatus* ks);
uint32_t _kbd_get_key(void);
uint32_t inputGetCurrentStatus(void);
uint32_t inputHasPendingEvent(void);

#endif
