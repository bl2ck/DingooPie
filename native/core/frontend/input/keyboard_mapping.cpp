#include "keyboard_mapping.h"

#include <ctype.h>
#include <stdio.h>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

// The frontend uses a polling-only whitelist so unmapped host keys can never
// modify the emulated Dingoo button state.
static const KeyboardBinding kDefaultKeyboardBindings[] =
{
#ifdef _WIN32
    { CONTROL_DPAD_UP, SDL_SCANCODE_W, 'W', "W" },
    { CONTROL_DPAD_UP, SDL_SCANCODE_UP, VK_UP, "Up" },
    { CONTROL_DPAD_DOWN, SDL_SCANCODE_S, 'S', "S" },
    { CONTROL_DPAD_DOWN, SDL_SCANCODE_DOWN, VK_DOWN, "Down" },
    { CONTROL_DPAD_LEFT, SDL_SCANCODE_A, 'A', "A" },
    { CONTROL_DPAD_LEFT, SDL_SCANCODE_LEFT, VK_LEFT, "Left" },
    { CONTROL_DPAD_RIGHT, SDL_SCANCODE_D, 'D', "D" },
    { CONTROL_DPAD_RIGHT, SDL_SCANCODE_RIGHT, VK_RIGHT, "Right" },
    { CONTROL_BUTTON_X, SDL_SCANCODE_I, 'I', "I" },
    { CONTROL_BUTTON_B, SDL_SCANCODE_K, 'K', "K" },
    { CONTROL_BUTTON_Y, SDL_SCANCODE_J, 'J', "J" },
    { CONTROL_BUTTON_A, SDL_SCANCODE_L, 'L', "L" },
    { CONTROL_BUTTON_START, SDL_SCANCODE_O, 'O', "O" },
    { CONTROL_BUTTON_START, SDL_SCANCODE_0, '0', "0" },
    { CONTROL_BUTTON_SELECT, SDL_SCANCODE_Q, 'Q', "Q" },
    { CONTROL_BUTTON_SELECT, SDL_SCANCODE_1, '1', "1" },
    { CONTROL_TRIGGER_LEFT, SDL_SCANCODE_LSHIFT, VK_LSHIFT, "Left Shift" },
    { CONTROL_TRIGGER_RIGHT, SDL_SCANCODE_RSHIFT, VK_RSHIFT, "Right Shift" },
    { CONTROL_POWER, SDL_SCANCODE_BACKSPACE, VK_BACK, "Backspace" },
    { CONTROL_POWER, SDL_SCANCODE_HOME, VK_HOME, "Home" },
#else
    { CONTROL_DPAD_UP, SDL_SCANCODE_W, 0, "W" },
    { CONTROL_DPAD_UP, SDL_SCANCODE_UP, 0, "Up" },
    { CONTROL_DPAD_DOWN, SDL_SCANCODE_S, 0, "S" },
    { CONTROL_DPAD_DOWN, SDL_SCANCODE_DOWN, 0, "Down" },
    { CONTROL_DPAD_LEFT, SDL_SCANCODE_A, 0, "A" },
    { CONTROL_DPAD_LEFT, SDL_SCANCODE_LEFT, 0, "Left" },
    { CONTROL_DPAD_RIGHT, SDL_SCANCODE_D, 0, "D" },
    { CONTROL_DPAD_RIGHT, SDL_SCANCODE_RIGHT, 0, "Right" },
    { CONTROL_BUTTON_X, SDL_SCANCODE_I, 0, "I" },
    { CONTROL_BUTTON_B, SDL_SCANCODE_K, 0, "K" },
    { CONTROL_BUTTON_Y, SDL_SCANCODE_J, 0, "J" },
    { CONTROL_BUTTON_A, SDL_SCANCODE_L, 0, "L" },
    { CONTROL_BUTTON_START, SDL_SCANCODE_O, 0, "O" },
    { CONTROL_BUTTON_START, SDL_SCANCODE_0, 0, "0" },
    { CONTROL_BUTTON_SELECT, SDL_SCANCODE_Q, 0, "Q" },
    { CONTROL_BUTTON_SELECT, SDL_SCANCODE_1, 0, "1" },
    { CONTROL_TRIGGER_LEFT, SDL_SCANCODE_LSHIFT, 0, "Left Shift" },
    { CONTROL_TRIGGER_RIGHT, SDL_SCANCODE_RSHIFT, 0, "Right Shift" },
    { CONTROL_POWER, SDL_SCANCODE_BACKSPACE, 0, "Backspace" },
    { CONTROL_POWER, SDL_SCANCODE_HOME, 0, "Home" },
#endif
};

static const size_t kMaxKeyboardBindings = 64;
static KeyboardBinding g_keyboardBindings[kMaxKeyboardBindings];
static size_t g_keyboardBindingCount = 0;
static bool g_keyboardMappingInitialized = false;
static std::string g_appliedKeyboardMapping;

static std::string trimString(const std::string& text)
{
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t' ||
        text[begin] == '\r' || text[begin] == '\n'))
    {
        begin++;
    }
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
        text[end - 1] == '\r' || text[end - 1] == '\n'))
    {
        end--;
    }
    return text.substr(begin, end - begin);
}

static std::string normalizeMappingName(const std::string& text)
{
    std::string out;
    std::string trimmed = trimString(text);
    for (size_t i = 0; i < trimmed.size(); ++i)
    {
        unsigned char ch = (unsigned char)trimmed[i];
        if (ch == ' ' || ch == '\t' || ch == '_' || ch == '-')
        {
            continue;
        }
        out.push_back((char)tolower(ch));
    }
    return out;
}

static const char* keyboardControlName(uint32_t controlBit)
{
    switch (controlBit)
    {
    case CONTROL_BUTTON_A: return "A";
    case CONTROL_BUTTON_B: return "B";
    case CONTROL_BUTTON_X: return "X";
    case CONTROL_BUTTON_Y: return "Y";
    case CONTROL_BUTTON_START: return "Start";
    case CONTROL_BUTTON_SELECT: return "Select";
    case CONTROL_TRIGGER_LEFT: return "L";
    case CONTROL_TRIGGER_RIGHT: return "R";
    case CONTROL_DPAD_UP: return "Up";
    case CONTROL_DPAD_DOWN: return "Down";
    case CONTROL_DPAD_LEFT: return "Left";
    case CONTROL_DPAD_RIGHT: return "Right";
    case CONTROL_POWER: return "Power";
    default: return "None";
    }
}

static bool parseKeyboardControlName(const std::string& name, uint32_t* outControlBit)
{
    if (!outControlBit)
    {
        return false;
    }
    std::string normalized = normalizeMappingName(name);
    if (normalized == "a" || normalized == "buttona") { *outControlBit = CONTROL_BUTTON_A; return true; }
    if (normalized == "b" || normalized == "buttonb") { *outControlBit = CONTROL_BUTTON_B; return true; }
    if (normalized == "x" || normalized == "buttonx") { *outControlBit = CONTROL_BUTTON_X; return true; }
    if (normalized == "y" || normalized == "buttony") { *outControlBit = CONTROL_BUTTON_Y; return true; }
    if (normalized == "start") { *outControlBit = CONTROL_BUTTON_START; return true; }
    if (normalized == "select") { *outControlBit = CONTROL_BUTTON_SELECT; return true; }
    if (normalized == "l" || normalized == "leftshoulder" || normalized == "triggerleft") { *outControlBit = CONTROL_TRIGGER_LEFT; return true; }
    if (normalized == "r" || normalized == "rightshoulder" || normalized == "triggerright") { *outControlBit = CONTROL_TRIGGER_RIGHT; return true; }
    if (normalized == "up" || normalized == "dpadup") { *outControlBit = CONTROL_DPAD_UP; return true; }
    if (normalized == "down" || normalized == "dpaddown") { *outControlBit = CONTROL_DPAD_DOWN; return true; }
    if (normalized == "left" || normalized == "dpadleft") { *outControlBit = CONTROL_DPAD_LEFT; return true; }
    if (normalized == "right" || normalized == "dpadright") { *outControlBit = CONTROL_DPAD_RIGHT; return true; }
    if (normalized == "power") { *outControlBit = CONTROL_POWER; return true; }
    return false;
}

static int virtualKeyForScancode(SDL_Scancode scancode)
{
#ifdef _WIN32
    switch (scancode)
    {
    case SDL_SCANCODE_UP: return VK_UP;
    case SDL_SCANCODE_DOWN: return VK_DOWN;
    case SDL_SCANCODE_LEFT: return VK_LEFT;
    case SDL_SCANCODE_RIGHT: return VK_RIGHT;
    case SDL_SCANCODE_LSHIFT: return VK_LSHIFT;
    case SDL_SCANCODE_RSHIFT: return VK_RSHIFT;
    case SDL_SCANCODE_BACKSPACE: return VK_BACK;
    case SDL_SCANCODE_HOME: return VK_HOME;
    case SDL_SCANCODE_RETURN: return VK_RETURN;
    case SDL_SCANCODE_SPACE: return VK_SPACE;
    case SDL_SCANCODE_ESCAPE: return VK_ESCAPE;
    default:
        break;
    }
    SDL_Keycode keycode = SDL_GetKeyFromScancode(scancode);
    if (keycode >= 'a' && keycode <= 'z')
    {
        return (int)(keycode - 'a' + 'A');
    }
    if (keycode >= 'A' && keycode <= 'Z')
    {
        return (int)keycode;
    }
    if (keycode >= '0' && keycode <= '9')
    {
        return (int)keycode;
    }
#else
    (void)scancode;
#endif
    return 0;
}

static std::string keyboardSourceName(SDL_Scancode scancode)
{
    if (scancode == SDL_SCANCODE_UNKNOWN)
    {
        return "None";
    }
    const char* name = SDL_GetScancodeName(scancode);
    if (name && name[0])
    {
        return name;
    }
    char fallback[32] = {};
    snprintf(fallback, sizeof(fallback), "Scancode%d", (int)scancode);
    return fallback;
}

static bool parseKeyboardSourceName(const std::string& name, SDL_Scancode* outScancode)
{
    if (!outScancode)
    {
        return false;
    }
    std::string normalized = normalizeMappingName(name);
    if (normalized.empty() || normalized == "none" || normalized == "off" ||
        normalized == "unmapped" || normalized == "disabled" || normalized == "0")
    {
        *outScancode = SDL_SCANCODE_UNKNOWN;
        return true;
    }
    if (normalized == "up" || normalized == "arrowup") { *outScancode = SDL_SCANCODE_UP; return true; }
    if (normalized == "down" || normalized == "arrowdown") { *outScancode = SDL_SCANCODE_DOWN; return true; }
    if (normalized == "left" || normalized == "arrowleft") { *outScancode = SDL_SCANCODE_LEFT; return true; }
    if (normalized == "right" || normalized == "arrowright") { *outScancode = SDL_SCANCODE_RIGHT; return true; }
    if (normalized == "lshift" || normalized == "leftshift") { *outScancode = SDL_SCANCODE_LSHIFT; return true; }
    if (normalized == "rshift" || normalized == "rightshift") { *outScancode = SDL_SCANCODE_RSHIFT; return true; }
    if (normalized == "space") { *outScancode = SDL_SCANCODE_SPACE; return true; }
    if (normalized == "enter" || normalized == "return") { *outScancode = SDL_SCANCODE_RETURN; return true; }
    if (normalized == "esc" || normalized == "escape") { *outScancode = SDL_SCANCODE_ESCAPE; return true; }
    if (normalized == "backspace") { *outScancode = SDL_SCANCODE_BACKSPACE; return true; }
    if (normalized == "home") { *outScancode = SDL_SCANCODE_HOME; return true; }

    SDL_Scancode scancode = SDL_GetScancodeFromName(name.c_str());
    if (scancode != SDL_SCANCODE_UNKNOWN)
    {
        *outScancode = scancode;
        return true;
    }
    if (normalized.size() == 1)
    {
        char ch = (char)normalized[0];
        if (ch >= 'a' && ch <= 'z')
        {
            *outScancode = (SDL_Scancode)(SDL_SCANCODE_A + (ch - 'a'));
            return true;
        }
        if (ch >= '0' && ch <= '9')
        {
            *outScancode = ch == '0' ? SDL_SCANCODE_0 : (SDL_Scancode)(SDL_SCANCODE_1 + (ch - '1'));
            return true;
        }
    }
    return false;
}

static bool addKeyboardBinding(uint32_t controlBit, SDL_Scancode scancode)
{
    if (scancode == SDL_SCANCODE_UNKNOWN || g_keyboardBindingCount >= kMaxKeyboardBindings)
    {
        return false;
    }
    g_keyboardBindings[g_keyboardBindingCount].controlBit = controlBit;
    g_keyboardBindings[g_keyboardBindingCount].scancode = scancode;
    g_keyboardBindings[g_keyboardBindingCount].virtualKey = virtualKeyForScancode(scancode);
    g_keyboardBindings[g_keyboardBindingCount].label = NULL;
    g_keyboardBindingCount++;
    return true;
}

static void setDefaultKeyboardMapping(void)
{
    g_keyboardBindingCount = 0;
    for (size_t i = 0; i < sizeof(kDefaultKeyboardBindings) / sizeof(kDefaultKeyboardBindings[0]); ++i)
    {
        if (g_keyboardBindingCount < kMaxKeyboardBindings)
        {
            g_keyboardBindings[g_keyboardBindingCount++] = kDefaultKeyboardBindings[i];
        }
    }
}

static void removeKeyboardSource(SDL_Scancode scancode)
{
    if (scancode == SDL_SCANCODE_UNKNOWN)
    {
        return;
    }
    size_t out = 0;
    for (size_t i = 0; i < g_keyboardBindingCount; ++i)
    {
        if (g_keyboardBindings[i].scancode == scancode)
        {
            continue;
        }
        g_keyboardBindings[out++] = g_keyboardBindings[i];
    }
    g_keyboardBindingCount = out;
}

static void removeKeyboardControl(uint32_t controlBit)
{
    size_t out = 0;
    for (size_t i = 0; i < g_keyboardBindingCount; ++i)
    {
        if (g_keyboardBindings[i].controlBit == controlBit)
        {
            continue;
        }
        g_keyboardBindings[out++] = g_keyboardBindings[i];
    }
    g_keyboardBindingCount = out;
}

static uint32_t defaultControlForKeyboardSource(SDL_Scancode scancode, bool* found)
{
    for (size_t i = 0; i < sizeof(kDefaultKeyboardBindings) / sizeof(kDefaultKeyboardBindings[0]); ++i)
    {
        if (kDefaultKeyboardBindings[i].scancode == scancode)
        {
            if (found)
            {
                *found = true;
            }
            return kDefaultKeyboardBindings[i].controlBit;
        }
    }
    if (found)
    {
        *found = false;
    }
    return 0;
}

static uint32_t currentControlForKeyboardSource(SDL_Scancode scancode, bool* found)
{
    for (size_t i = 0; i < g_keyboardBindingCount; ++i)
    {
        if (g_keyboardBindings[i].scancode == scancode)
        {
            if (found)
            {
                *found = true;
            }
            return g_keyboardBindings[i].controlBit;
        }
    }
    if (found)
    {
        *found = false;
    }
    return 0;
}

static void applyKeyboardMappingToken(const std::string& token)
{
    std::string trimmed = trimString(token);
    if (trimmed.empty())
    {
        return;
    }
    size_t separator = trimmed.find('=');
    if (separator == std::string::npos)
    {
        separator = trimmed.find(':');
    }
    if (separator == std::string::npos)
    {
        printf("input: invalid keyboard mapping token='%s'\n", trimmed.c_str());
        return;
    }
    std::string sourceName = trimString(trimmed.substr(0, separator));
    std::string targetName = trimString(trimmed.substr(separator + 1));
    SDL_Scancode scancode = SDL_SCANCODE_UNKNOWN;
    if (!parseKeyboardSourceName(sourceName, &scancode))
    {
        printf("input: unknown keyboard mapping source='%s'\n", sourceName.c_str());
        return;
    }
    removeKeyboardSource(scancode);

    uint32_t controlBit = 0;
    std::string normalizedTarget = normalizeMappingName(targetName);
    if (normalizedTarget.empty() || normalizedTarget == "none" || normalizedTarget == "off" ||
        normalizedTarget == "unmapped" || normalizedTarget == "disabled" || normalizedTarget == "0")
    {
        return;
    }
    if (!parseKeyboardControlName(targetName, &controlBit))
    {
        printf("input: unknown keyboard mapping target='%s'\n", targetName.c_str());
        return;
    }
    addKeyboardBinding(controlBit, scancode);
}

void keyboardMappingApply(const std::string& mapping)
{
    if (g_keyboardMappingInitialized && mapping == g_appliedKeyboardMapping)
    {
        return;
    }

    inputClearControls();

    setDefaultKeyboardMapping();
    size_t begin = 0;
    while (begin <= mapping.size())
    {
        size_t comma = mapping.find_first_of(",;\n", begin);
        std::string token = comma == std::string::npos ?
            mapping.substr(begin) : mapping.substr(begin, comma - begin);
        applyKeyboardMappingToken(token);
        if (comma == std::string::npos)
        {
            break;
        }
        begin = comma + 1;
    }

    g_appliedKeyboardMapping = mapping;
    g_keyboardMappingInitialized = true;
    printf("input: keyboard mapping applied spec='%s'\n",
        mapping.empty() ? "(default)" : mapping.c_str());
}

std::string keyboardMappingCurrentSpec(void)
{
    if (!g_keyboardMappingInitialized)
    {
        keyboardMappingApply("");
    }

    std::string spec;
    for (size_t i = 0; i < sizeof(kDefaultKeyboardBindings) / sizeof(kDefaultKeyboardBindings[0]); ++i)
    {
        SDL_Scancode scancode = kDefaultKeyboardBindings[i].scancode;
        bool defaultFound = false;
        uint32_t defaultControl = defaultControlForKeyboardSource(scancode, &defaultFound);
        bool currentFound = false;
        uint32_t currentControl = currentControlForKeyboardSource(scancode, &currentFound);
        if (defaultFound && currentFound && defaultControl == currentControl)
        {
            continue;
        }
        if (!spec.empty()) spec += ",";
        spec += keyboardSourceName(scancode);
        spec += "=";
        spec += currentFound ? keyboardControlName(currentControl) : "None";
    }

    for (size_t i = 0; i < g_keyboardBindingCount; ++i)
    {
        bool defaultFound = false;
        defaultControlForKeyboardSource(g_keyboardBindings[i].scancode, &defaultFound);
        if (defaultFound)
        {
            continue;
        }
        if (!spec.empty()) spec += ",";
        spec += keyboardSourceName(g_keyboardBindings[i].scancode);
        spec += "=";
        spec += keyboardControlName(g_keyboardBindings[i].controlBit);
    }
    return spec;
}

std::string keyboardMappingSourceForControl(uint32_t controlBit)
{
    if (!g_keyboardMappingInitialized)
    {
        keyboardMappingApply("");
    }

    std::string out;
    for (size_t i = 0; i < g_keyboardBindingCount; ++i)
    {
        if (g_keyboardBindings[i].controlBit != controlBit)
        {
            continue;
        }
        if (!out.empty())
        {
            out += " / ";
        }
        out += keyboardSourceName(g_keyboardBindings[i].scancode);
    }
    return out.empty() ? "None" : out;
}

bool keyboardMappingSetForControl(uint32_t controlBit, SDL_Scancode scancode)
{
    if (scancode == SDL_SCANCODE_UNKNOWN)
    {
        return false;
    }
    if (!g_keyboardMappingInitialized)
    {
        keyboardMappingApply("");
    }

    inputClearControls();
    removeKeyboardSource(scancode);
    removeKeyboardControl(controlBit);
    addKeyboardBinding(controlBit, scancode);
    g_appliedKeyboardMapping = keyboardMappingCurrentSpec();
    return true;
}

void keyboardMappingReset(void)
{
    inputClearControls();
    setDefaultKeyboardMapping();
    g_appliedKeyboardMapping.clear();
    g_keyboardMappingInitialized = true;
}

bool keyboardMappingIsCurrent(const std::string& mapping)
{
    return g_keyboardMappingInitialized && mapping == g_appliedKeyboardMapping;
}

bool keyboardMappingInitialized(void)
{
    return g_keyboardMappingInitialized;
}

const KeyboardBinding* keyboardMappingBindings(size_t* outCount)
{
    if (!g_keyboardMappingInitialized)
    {
        keyboardMappingApply("");
    }
    if (outCount)
    {
        *outCount = g_keyboardBindingCount;
    }
    return g_keyboardBindings;
}
