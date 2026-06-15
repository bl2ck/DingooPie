#include "input_controls.h"

#include <memory.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

struct InputState
{
    uint32_t status;
    uint32_t pressed;
    uint32_t released;
    uint32_t systemEventPending;
};

static InputState g_inputState = { 0 };
static uint32_t g_syntheticStatus = 0;
static SDL_mutex* g_inputMutex = NULL;

static SDL_mutex* inputMutex(void)
{
    if (!g_inputMutex)
    {
        g_inputMutex = SDL_CreateMutex();
        if (!g_inputMutex)
        {
            printf("input: SDL_CreateMutex failed: %s\n", SDL_GetError());
        }
    }
    return g_inputMutex;
}

static void lockInput(void)
{
    SDL_mutex* mutex = inputMutex();
    if (mutex)
    {
        SDL_LockMutex(mutex);
    }
}

static void unlockInput(void)
{
    SDL_mutex* mutex = inputMutex();
    if (mutex)
    {
        SDL_UnlockMutex(mutex);
    }
}

static bool inputTraceEnabled(void)
{
    static int enabled = -1;
    if (enabled < 0)
    {
        const char* value = getenv("DINGOO_PIE_INPUT_TRACE");
        enabled = (value && value[0] && value[0] != '0') ? 1 : 0;
    }
    return enabled != 0;
}

struct InputBinding
{
    uint32_t controlBit;
    SDL_Scancode primary;
    SDL_Scancode alternate;
#ifdef _WIN32
    int primaryVk;
    int alternateVk;
#endif
    const char* label;
};

// The frontend uses a polling-only whitelist so unmapped host keys can never
// modify the emulated Dingoo button state.
static const InputBinding kInputBindings[] =
{
#ifdef _WIN32
    { CONTROL_DPAD_UP, SDL_SCANCODE_W, SDL_SCANCODE_UP, 'W', VK_UP, "DPadUp" },
    { CONTROL_DPAD_DOWN, SDL_SCANCODE_S, SDL_SCANCODE_DOWN, 'S', VK_DOWN, "DPadDown" },
    { CONTROL_DPAD_LEFT, SDL_SCANCODE_A, SDL_SCANCODE_LEFT, 'A', VK_LEFT, "DPadLeft" },
    { CONTROL_DPAD_RIGHT, SDL_SCANCODE_D, SDL_SCANCODE_RIGHT, 'D', VK_RIGHT, "DPadRight" },
    { CONTROL_BUTTON_X, SDL_SCANCODE_I, SDL_SCANCODE_UNKNOWN, 'I', 0, "ButtonX" },
    { CONTROL_BUTTON_B, SDL_SCANCODE_K, SDL_SCANCODE_UNKNOWN, 'K', 0, "ButtonB" },
    { CONTROL_BUTTON_Y, SDL_SCANCODE_J, SDL_SCANCODE_UNKNOWN, 'J', 0, "ButtonY" },
    { CONTROL_BUTTON_A, SDL_SCANCODE_L, SDL_SCANCODE_UNKNOWN, 'L', 0, "ButtonA" },
    { CONTROL_BUTTON_START, SDL_SCANCODE_O, SDL_SCANCODE_0, 'O', '0', "Start" },
    { CONTROL_BUTTON_SELECT, SDL_SCANCODE_Q, SDL_SCANCODE_1, 'Q', '1', "Select" },
    { CONTROL_TRIGGER_LEFT, SDL_SCANCODE_LSHIFT, SDL_SCANCODE_UNKNOWN, VK_LSHIFT, 0, "TriggerLeft" },
    { CONTROL_TRIGGER_RIGHT, SDL_SCANCODE_RSHIFT, SDL_SCANCODE_UNKNOWN, VK_RSHIFT, 0, "TriggerRight" },
    { CONTROL_POWER, SDL_SCANCODE_BACKSPACE, SDL_SCANCODE_HOME, VK_BACK, VK_HOME, "Power" },
#else
    { CONTROL_DPAD_UP, SDL_SCANCODE_W, SDL_SCANCODE_UP, "DPadUp" },
    { CONTROL_DPAD_DOWN, SDL_SCANCODE_S, SDL_SCANCODE_DOWN, "DPadDown" },
    { CONTROL_DPAD_LEFT, SDL_SCANCODE_A, SDL_SCANCODE_LEFT, "DPadLeft" },
    { CONTROL_DPAD_RIGHT, SDL_SCANCODE_D, SDL_SCANCODE_RIGHT, "DPadRight" },
    { CONTROL_BUTTON_X, SDL_SCANCODE_I, SDL_SCANCODE_UNKNOWN, "ButtonX" },
    { CONTROL_BUTTON_B, SDL_SCANCODE_K, SDL_SCANCODE_UNKNOWN, "ButtonB" },
    { CONTROL_BUTTON_Y, SDL_SCANCODE_J, SDL_SCANCODE_UNKNOWN, "ButtonY" },
    { CONTROL_BUTTON_A, SDL_SCANCODE_L, SDL_SCANCODE_UNKNOWN, "ButtonA" },
    { CONTROL_BUTTON_START, SDL_SCANCODE_O, SDL_SCANCODE_0, "Start" },
    { CONTROL_BUTTON_SELECT, SDL_SCANCODE_Q, SDL_SCANCODE_1, "Select" },
    { CONTROL_TRIGGER_LEFT, SDL_SCANCODE_LSHIFT, SDL_SCANCODE_UNKNOWN, "TriggerLeft" },
    { CONTROL_TRIGGER_RIGHT, SDL_SCANCODE_RSHIFT, SDL_SCANCODE_UNKNOWN, "TriggerRight" },
    { CONTROL_POWER, SDL_SCANCODE_BACKSPACE, SDL_SCANCODE_HOME, "Power" },
#endif
};

void _kbd_get_status(KEY_STATUS* ks)
{
    if (!ks)
    {
        return;
    }

    lockInput();
    ks->pressed = g_inputState.pressed;
    ks->released = g_inputState.released;
    ks->status = g_inputState.status;
    g_inputState.pressed = 0;
    g_inputState.released = 0;
    unlockInput();

    if (inputTraceEnabled() && (ks->pressed || ks->released || ks->status))
    {
        printf("input: _kbd_get_status pressed=0x%08lX released=0x%08lX status=0x%08lX\n",
            ks->pressed, ks->released, ks->status);
    }
}

uint32_t _kbd_get_key(void)
{
    lockInput();
    uint32_t ret = g_inputState.status;
    unlockInput();

    if (inputTraceEnabled() && ret)
    {
        printf("input: _kbd_get_key ret=0x%08X\n", ret);
    }
    return ret;
}

uint32_t inputGetCurrentStatus(void)
{
    lockInput();
    uint32_t ret = g_inputState.status;
    unlockInput();
    return ret;
}

uint32_t inputHasPendingEvent(void)
{
    lockInput();
    uint32_t ret = g_inputState.systemEventPending ? 1u : 0u;
    g_inputState.systemEventPending = 0;
    unlockInput();
    return ret;
}

static void updateKeyLocked(int pressed, uint32_t key)
{
    uint32_t mask = (1u << key);
    if (pressed)
    {
        if ((g_inputState.status & mask) == 0)
        {
            g_inputState.pressed |= mask;
            g_inputState.systemEventPending = 1;
        }
        g_inputState.released &= ~mask;
        g_inputState.status |= mask;
    }
    else
    {
        if (g_inputState.status & mask)
        {
            g_inputState.released |= mask;
            g_inputState.systemEventPending = 1;
        }
        g_inputState.status &= ~mask;
    }
}

static bool bindingMatchesScancode(const InputBinding& binding, SDL_Scancode scancode)
{
    return scancode != SDL_SCANCODE_UNKNOWN &&
        (binding.primary == scancode || binding.alternate == scancode);
}

static bool bindingMatchesVirtualKey(const InputBinding& binding, int virtualKey)
{
#ifdef _WIN32
    return virtualKey != 0 &&
        (binding.primaryVk == virtualKey || binding.alternateVk == virtualKey);
#else
    (void)binding;
    (void)virtualKey;
    return false;
#endif
}

static bool updateBindingFromHostKey(bool pressed, bool (*matches)(const InputBinding&, int), int keyValue)
{
    bool handled = false;
    lockInput();
    InputState before = g_inputState;
    for (size_t i = 0; i < sizeof(kInputBindings) / sizeof(kInputBindings[0]); ++i)
    {
        if (matches(kInputBindings[i], keyValue))
        {
            updateKeyLocked(pressed ? 1 : 0, kInputBindings[i].controlBit);
            handled = true;
        }
    }
    InputState after = g_inputState;
    unlockInput();

    if (handled && inputTraceEnabled() &&
        (before.status != after.status ||
            before.pressed != after.pressed ||
            before.released != after.released))
    {
        printf("input: event key=%d pressed=%u status=0x%08lX edge_down=0x%08lX edge_up=0x%08lX\n",
            keyValue,
            pressed ? 1u : 0u,
            (unsigned long)after.status,
            (unsigned long)after.pressed,
            (unsigned long)after.released);
    }
    return handled;
}

static bool updateBindingFromScancode(bool pressed, SDL_Scancode scancode)
{
    bool handled = false;
    lockInput();
    InputState before = g_inputState;
    for (size_t i = 0; i < sizeof(kInputBindings) / sizeof(kInputBindings[0]); ++i)
    {
        if (bindingMatchesScancode(kInputBindings[i], scancode))
        {
            updateKeyLocked(pressed ? 1 : 0, kInputBindings[i].controlBit);
            handled = true;
        }
    }
    InputState after = g_inputState;
    unlockInput();

    if (handled && inputTraceEnabled() &&
        (before.status != after.status ||
            before.pressed != after.pressed ||
            before.released != after.released))
    {
        printf("input: scancode=%d pressed=%u status=0x%08lX edge_down=0x%08lX edge_up=0x%08lX\n",
            (int)scancode,
            pressed ? 1u : 0u,
            (unsigned long)after.status,
            (unsigned long)after.pressed,
            (unsigned long)after.released);
    }
    return handled;
}

void inputClearControls(void)
{
    lockInput();
    uint32_t released = g_inputState.status;
    if (released)
    {
        g_inputState.released |= released;
        g_inputState.status = 0;
        g_inputState.systemEventPending = 1;
    }
    InputState snapshot = g_inputState;
    unlockInput();

    if (inputTraceEnabled() && released)
    {
        printf("input: clear status=0x%08X pressed=0x%08X released=0x%08X\n",
            snapshot.status, snapshot.pressed, snapshot.released);
    }
}

void inputHandleHostScancode(SDL_Scancode scancode, bool pressed)
{
    updateBindingFromScancode(pressed, scancode);
}

void inputHandleHostVirtualKey(int virtualKey, bool pressed)
{
    updateBindingFromHostKey(pressed, bindingMatchesVirtualKey, virtualKey);
}

void inputSetSyntheticControl(uint32_t controlBit, bool pressed)
{
    lockInput();
    InputState before = g_inputState;
    uint32_t mask = (1u << controlBit);
    if (pressed)
    {
        g_syntheticStatus |= mask;
    }
    else
    {
        g_syntheticStatus &= ~mask;
    }
    updateKeyLocked(pressed ? 1 : 0, controlBit);
    InputState after = g_inputState;
    unlockInput();

    if (inputTraceEnabled() &&
        (before.status != after.status ||
            before.pressed != after.pressed ||
            before.released != after.released))
    {
        printf("input: synthetic control=%u pressed=%u status=0x%08lX edge_down=0x%08lX edge_up=0x%08lX\n",
            (unsigned int)controlBit,
            pressed ? 1u : 0u,
            (unsigned long)after.status,
            (unsigned long)after.pressed,
            (unsigned long)after.released);
    }
}

static bool isControlDown(const uint8_t* keys, SDL_Scancode scancode)
{
    return keys && scancode != SDL_SCANCODE_UNKNOWN && keys[scancode] != 0;
}

#ifdef _WIN32
static bool isVirtualKeyDown(int virtualKey)
{
    return virtualKey && (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
}
#endif

static bool isBindingDown(const uint8_t* keys, const InputBinding& binding)
{
#ifdef _WIN32
    (void)keys;
    return isVirtualKeyDown(binding.primaryVk) || isVirtualKeyDown(binding.alternateVk);
#else
    return isControlDown(keys, binding.primary) || isControlDown(keys, binding.alternate);
#endif
}

void inputPollKeyboardState(void)
{
#ifdef _WIN32
    const uint8_t* keys = NULL;
#else
    SDL_PumpEvents();
    const uint8_t* keys = SDL_GetKeyboardState(NULL);
    if (!keys)
    {
        return;
    }
#endif

    lockInput();
    InputState before = g_inputState;
    for (size_t i = 0; i < sizeof(kInputBindings) / sizeof(kInputBindings[0]); ++i)
    {
        uint32_t mask = (1u << kInputBindings[i].controlBit);
        bool down = isBindingDown(keys, kInputBindings[i]) || (g_syntheticStatus & mask) != 0;
        updateKeyLocked(down ? 1 : 0, kInputBindings[i].controlBit);
    }
    InputState after = g_inputState;
    unlockInput();

    if (inputTraceEnabled() &&
        (before.status != after.status ||
            before.pressed != after.pressed ||
            before.released != after.released))
    {
        printf("input: poll status=0x%08lX pressed=0x%08lX released=0x%08lX\n",
            (unsigned long)after.status, (unsigned long)after.pressed,
            (unsigned long)after.released);
    }
}

