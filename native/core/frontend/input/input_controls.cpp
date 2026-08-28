#include "frontend/input/input_controls.h"
#include "frontend/input/keyboard_mapping.h"

#include <memory.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>

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
static uint32_t g_deferredRelease = 0;
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
    static const bool enabled = []() {
        const char* value = getenv("DINGOO_PIE_INPUT_TRACE");
        return value && value[0] && value[0] != '0';
    }();
    return enabled;
}

void inputApplyKeyboardMapping(const std::string& mapping)
{
    keyboardMappingApply(mapping);
}

std::string inputCurrentKeyboardMapping(void)
{
    return keyboardMappingCurrentSpec();
}

std::string inputKeyboardSourceForControl(uint32_t controlBit)
{
    return keyboardMappingSourceForControl(controlBit);
}

bool inputSetKeyboardMappingForControl(uint32_t controlBit, SDL_Scancode scancode)
{
    return keyboardMappingSetForControl(controlBit, scancode);
}

void inputResetKeyboardMapping(void)
{
    keyboardMappingReset();
}

void _kbd_get_status(GuestKeyStatus* ks)
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
    if (g_deferredRelease)
    {
        g_inputState.released |= g_deferredRelease;
        g_inputState.status &= ~g_deferredRelease;
        g_inputState.systemEventPending = 1;
        g_deferredRelease = 0;
    }
    unlockInput();

    if (inputTraceEnabled() && (ks->pressed || ks->released || ks->status))
    {
        printf("input: _kbd_get_status pressed=0x%08lX released=0x%08lX status=0x%08lX\n",
            (unsigned long)ks->pressed, (unsigned long)ks->released,
            (unsigned long)ks->status);
    }
}

uint32_t _kbd_get_key(void)
{
    lockInput();
    uint32_t ret = g_inputState.status;
    if (g_deferredRelease)
    {
        g_inputState.released |= g_deferredRelease;
        g_inputState.status &= ~g_deferredRelease;
        g_inputState.systemEventPending = 1;
        g_deferredRelease = 0;
    }
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
        g_deferredRelease &= ~mask;
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
            if (g_inputState.pressed & mask)
            {
                g_deferredRelease |= mask;
            }
            else
            {
                g_inputState.released |= mask;
                g_inputState.status &= ~mask;
                g_inputState.systemEventPending = 1;
            }
        }
    }
}

static void ensureKeyboardMappingInitialized(void)
{
    if (!keyboardMappingInitialized())
    {
        keyboardMappingApply("");
    }
}

static bool bindingMatchesScancode(const KeyboardBinding& binding, SDL_Scancode scancode)
{
    return scancode != SDL_SCANCODE_UNKNOWN && binding.scancode == scancode;
}

static bool bindingMatchesVirtualKey(const KeyboardBinding& binding, int virtualKey)
{
    return virtualKey != 0 && binding.virtualKey == virtualKey;
}

static bool updateBindingFromScancode(bool pressed, SDL_Scancode scancode)
{
    ensureKeyboardMappingInitialized();
    bool handled = false;
    size_t bindingCount = 0;
    const KeyboardBinding* bindings = keyboardMappingBindings(&bindingCount);
    lockInput();
    InputState before = g_inputState;
    for (size_t i = 0; i < bindingCount; ++i)
    {
        if (bindingMatchesScancode(bindings[i], scancode))
        {
            updateKeyLocked(pressed ? 1 : 0, bindings[i].controlBit);
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

static bool updateBindingFromVirtualKey(bool pressed, int virtualKey)
{
    ensureKeyboardMappingInitialized();
    bool handled = false;
    size_t bindingCount = 0;
    const KeyboardBinding* bindings = keyboardMappingBindings(&bindingCount);
    lockInput();
    InputState before = g_inputState;
    for (size_t i = 0; i < bindingCount; ++i)
    {
        if (bindingMatchesVirtualKey(bindings[i], virtualKey))
        {
            updateKeyLocked(pressed ? 1 : 0, bindings[i].controlBit);
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
            virtualKey,
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
    g_deferredRelease = 0;
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

void inputClearSyntheticControls(void)
{
    lockInput();
    uint32_t released = g_syntheticStatus & g_inputState.status;
    g_syntheticStatus = 0;
    if (released)
    {
        g_inputState.released |= released;
        g_inputState.status &= ~released;
        g_inputState.systemEventPending = 1;
    }
    InputState snapshot = g_inputState;
    unlockInput();

    if (inputTraceEnabled() && released)
    {
        printf("input: clear synthetic status=0x%08X pressed=0x%08X released=0x%08X\n",
            snapshot.status, snapshot.pressed, snapshot.released);
    }
}

void inputResetTransientControls(void)
{
    lockInput();
    InputState before = g_inputState;
    uint32_t syntheticBefore = g_syntheticStatus;
    g_syntheticStatus = 0;
    g_deferredRelease = 0;
    memset(&g_inputState, 0, sizeof(g_inputState));
    unlockInput();

    if (inputTraceEnabled() &&
        (before.status || before.pressed || before.released || before.systemEventPending || syntheticBefore))
    {
        printf("input: reset transient controls status=0x%08lX pressed=0x%08lX released=0x%08lX synthetic=0x%08X\n",
            (unsigned long)before.status,
            (unsigned long)before.pressed,
            (unsigned long)before.released,
            (unsigned int)syntheticBefore);
    }
}

void inputHandleHostScancode(SDL_Scancode scancode, bool pressed)
{
    updateBindingFromScancode(pressed, scancode);
}

void inputHandleHostVirtualKey(int virtualKey, bool pressed)
{
    updateBindingFromVirtualKey(pressed, virtualKey);
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

static bool isBindingDown(const uint8_t* keys, const KeyboardBinding& binding)
{
#ifdef _WIN32
    (void)keys;
    return isVirtualKeyDown(binding.virtualKey);
#else
    return isControlDown(keys, binding.scancode);
#endif
}

void inputPollKeyboardState(void)
{
    ensureKeyboardMappingInitialized();
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

    size_t bindingCount = 0;
    const KeyboardBinding* bindings = keyboardMappingBindings(&bindingCount);
    lockInput();
    InputState before = g_inputState;
    uint32_t referencedControls = 0;
    uint32_t activeControls = 0;
    for (size_t i = 0; i < bindingCount; ++i)
    {
        uint32_t mask = (1u << bindings[i].controlBit);
        referencedControls |= mask;
        if (isBindingDown(keys, bindings[i]) || (g_syntheticStatus & mask) != 0)
        {
            activeControls |= mask;
        }
    }
    for (uint32_t controlBit = 0; controlBit < 32; ++controlBit)
    {
        uint32_t mask = 1u << controlBit;
        if (referencedControls & mask)
        {
            updateKeyLocked((activeControls & mask) != 0 ? 1 : 0, controlBit);
        }
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
