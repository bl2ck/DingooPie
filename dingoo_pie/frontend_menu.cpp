#include "frontend_menu.h"

#include "app_paths.h"
#include "cheat_manager_ui.h"
#include "cheat_runtime.h"
#include "debug_console.h"
#include "emulator_config.h"
#include "emulator_core.h"
#include "input_controls.h"
#include "runtime_log.h"
#include "save_state.h"
#include "save_state_manager_ui.h"
#include "sdl_frontend.h"
#include "platform_win32.h"
#include "ui_strings.h"

#include <stdio.h>
#include <stdlib.h>
#include <chrono>
#include <sstream>
#include <string>
#include <thread>
#include <time.h>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shellapi.h>
#endif

enum FrontendMenuCommand
{
    // Keep command IDs grouped in the same order as the Win32 menus below.
    MENU_FILE_OPEN = 1001,
    MENU_FILE_RECENT_CLEAR,
    MENU_FILE_RESTART,
    MENU_FILE_PAUSE_RESUME,
    MENU_FILE_SAVE_SCREENSHOT,
    MENU_FILE_SAVE_SLOT_1,
    MENU_FILE_SAVE_SLOT_2,
    MENU_FILE_SAVE_SLOT_3,
    MENU_FILE_SAVE_SLOT_4,
    MENU_FILE_SAVE_SLOT_5,
    MENU_FILE_SAVE_SLOT_6,
    MENU_FILE_SAVE_SLOT_7,
    MENU_FILE_SAVE_SLOT_8,
    MENU_FILE_SAVE_SLOT_9,
    MENU_FILE_SAVE_SLOT_10,
    MENU_FILE_SAVE_SLOT_11,
    MENU_FILE_SAVE_SLOT_12,
    MENU_FILE_SAVE_SLOT_13,
    MENU_FILE_SAVE_SLOT_14,
    MENU_FILE_SAVE_SLOT_15,
    MENU_FILE_LOAD_SLOT_1,
    MENU_FILE_LOAD_SLOT_2,
    MENU_FILE_LOAD_SLOT_3,
    MENU_FILE_LOAD_SLOT_4,
    MENU_FILE_LOAD_SLOT_5,
    MENU_FILE_LOAD_SLOT_6,
    MENU_FILE_LOAD_SLOT_7,
    MENU_FILE_LOAD_SLOT_8,
    MENU_FILE_LOAD_SLOT_9,
    MENU_FILE_LOAD_SLOT_10,
    MENU_FILE_LOAD_SLOT_11,
    MENU_FILE_LOAD_SLOT_12,
    MENU_FILE_LOAD_SLOT_13,
    MENU_FILE_LOAD_SLOT_14,
    MENU_FILE_LOAD_SLOT_15,
    MENU_FILE_SAVE_STATE_MANAGER,
    MENU_FILE_EXIT,
    MENU_VIDEO_SCALE_1X,
    MENU_VIDEO_SCALE_2X,
    MENU_VIDEO_SCALE_3X,
    MENU_VIDEO_FULLSCREEN,
    MENU_VIDEO_AA_OFF,
    MENU_VIDEO_AA_LOW,
    MENU_VIDEO_AA_CLEAR,
    MENU_VIDEO_EFFECT_NORMAL,
    MENU_VIDEO_EFFECT_GRAYSCALE,
    MENU_VIDEO_EFFECT_INVERT,
    MENU_VIDEO_EFFECT_SOFT_BLUR,
    MENU_VIDEO_EFFECT_SHARPEN,
    MENU_VIDEO_EFFECT_VIVID,
    MENU_VIDEO_EFFECT_SEPIA,
    MENU_VIDEO_EFFECT_PIXEL_GRID,
    MENU_VIDEO_EFFECT_LCD_SCANLINE,
    MENU_VIDEO_EFFECT_LIGHT_CRT,
    MENU_VIDEO_BRIGHTNESS_50,
    MENU_VIDEO_BRIGHTNESS_75,
    MENU_VIDEO_BRIGHTNESS_90,
    MENU_VIDEO_BRIGHTNESS_100,
    MENU_VIDEO_BRIGHTNESS_110,
    MENU_VIDEO_BRIGHTNESS_125,
    MENU_VIDEO_BRIGHTNESS_150,
    MENU_VIDEO_CONTRAST_50,
    MENU_VIDEO_CONTRAST_75,
    MENU_VIDEO_CONTRAST_90,
    MENU_VIDEO_CONTRAST_100,
    MENU_VIDEO_CONTRAST_110,
    MENU_VIDEO_CONTRAST_125,
    MENU_VIDEO_CONTRAST_150,
    MENU_VIDEO_GAMMA_50,
    MENU_VIDEO_GAMMA_75,
    MENU_VIDEO_GAMMA_90,
    MENU_VIDEO_GAMMA_100,
    MENU_VIDEO_GAMMA_110,
    MENU_VIDEO_GAMMA_125,
    MENU_VIDEO_GAMMA_150,
    MENU_VIDEO_SATURATION_50,
    MENU_VIDEO_SATURATION_75,
    MENU_VIDEO_SATURATION_90,
    MENU_VIDEO_SATURATION_100,
    MENU_VIDEO_SATURATION_110,
    MENU_VIDEO_SATURATION_125,
    MENU_VIDEO_SATURATION_150,
    MENU_VIDEO_MINIMIZED_NORMAL,
    MENU_VIDEO_MINIMIZED_PAUSE,
    MENU_VIDEO_MINIMIZED_THROTTLE,
    MENU_VIDEO_PORTRAIT,
    MENU_VIDEO_SHOW_FPS,
    MENU_AUDIO_VOLUME_0,
    MENU_AUDIO_VOLUME_25,
    MENU_AUDIO_VOLUME_50,
    MENU_AUDIO_VOLUME_75,
    MENU_AUDIO_VOLUME_100,
    MENU_AUDIO_VOLUME_125,
    MENU_AUDIO_VOLUME_150,
    MENU_AUDIO_BUFFER_512,
    MENU_AUDIO_BUFFER_1024,
    MENU_AUDIO_BUFFER_2048,
    MENU_AUDIO_BUFFER_4096,
    MENU_AUDIO_BUFFER_8192,
    MENU_AUDIO_EFFECT_OFF,
    MENU_AUDIO_EFFECT_SOFT,
    MENU_AUDIO_EFFECT_CLEAR,
    MENU_AUDIO_EFFECT_BASS_BOOST,
    MENU_AUDIO_EFFECT_MONO,
    MENU_AUDIO_DISABLE,
    MENU_INPUT_DISABLE_IME,
    MENU_INPUT_SHOW_VIRTUAL_CONTROLS,
    MENU_INPUT_MAPPING_WINDOW,
    MENU_SETTINGS_BACKEND_AUTO,
    MENU_SETTINGS_BACKEND_IRJIT,
    MENU_SETTINGS_BACKEND_INTERPRETER,
    MENU_SETTINGS_CPU_CLOCK_AUTO,
    MENU_SETTINGS_CPU_CLOCK_200,
    MENU_SETTINGS_CPU_CLOCK_336,
    MENU_SETTINGS_CPU_CLOCK_370,
    MENU_SETTINGS_CPU_CLOCK_400,
    MENU_SETTINGS_CPU_CLOCK_430,
    MENU_SETTINGS_RUNTIME_SPEED_AUTO,
    MENU_SETTINGS_RUNTIME_SPEED_100,
    MENU_SETTINGS_RUNTIME_SPEED_095,
    MENU_SETTINGS_RUNTIME_SPEED_090,
    MENU_SETTINGS_RUNTIME_SPEED_085,
    MENU_SETTINGS_RUNTIME_SPEED_080,
    MENU_SETTINGS_RUNTIME_SPEED_075,
    MENU_SETTINGS_RUNTIME_SPEED_070,
    MENU_SETTINGS_RUNTIME_SPEED_065,
    MENU_SETTINGS_RUNTIME_SPEED_060,
    MENU_SETTINGS_RUNTIME_SPEED_055,
    MENU_SETTINGS_RUNTIME_SPEED_050,
    MENU_SETTINGS_RUNTIME_SPEED_045,
    MENU_SETTINGS_RUNTIME_SPEED_040,
    MENU_SETTINGS_RUNTIME_SPEED_035,
    MENU_SETTINGS_RUNTIME_SPEED_030,
    MENU_SETTINGS_RUNTIME_SPEED_025,
    MENU_SETTINGS_RUNTIME_SPEED_020,
    MENU_SETTINGS_DELAY_SCALE_AUTO,
    MENU_SETTINGS_DELAY_SCALE_100,
    MENU_SETTINGS_DELAY_SCALE_095,
    MENU_SETTINGS_DELAY_SCALE_090,
    MENU_SETTINGS_DELAY_SCALE_085,
    MENU_SETTINGS_DELAY_SCALE_080,
    MENU_SETTINGS_DELAY_SCALE_075,
    MENU_SETTINGS_DELAY_SCALE_070,
    MENU_SETTINGS_DELAY_SCALE_065,
    MENU_SETTINGS_DELAY_SCALE_060,
    MENU_SETTINGS_DELAY_SCALE_055,
    MENU_SETTINGS_DELAY_SCALE_050,
    MENU_SETTINGS_DELAY_SCALE_045,
    MENU_SETTINGS_DELAY_SCALE_040,
    MENU_SETTINGS_DELAY_SCALE_035,
    MENU_SETTINGS_DELAY_SCALE_030,
    MENU_SETTINGS_DELAY_SCALE_025,
    MENU_SETTINGS_DELAY_SCALE_020,
    MENU_SETTINGS_ENABLE_CHEATS,
    MENU_SETTINGS_CHEAT_MANAGER,
    MENU_SETTINGS_LANGUAGE_CHINESE,
    MENU_SETTINGS_LANGUAGE_ENGLISH,
    MENU_SETTINGS_RESET,
    MENU_DEBUG_SHOW_CONSOLE,
    MENU_DEBUG_PROFILE,
    MENU_DEBUG_OPEN_LOG,
    MENU_DEBUG_RESOURCE_MONITOR,
    MENU_DEBUG_MEMORY_SEARCHER,
    MENU_DEBUG_DEBUGGER,
    MENU_HELP_ABOUT
};

static const unsigned int MENU_FILE_RECENT_BASE = 3000;
static const unsigned int MENU_FILE_RECENT_MAX =
    MENU_FILE_RECENT_BASE + EMULATOR_RECENT_APP_LIMIT - 1;
static const unsigned int MENU_CHEAT_ENTRY_BASE = 4000;
static const unsigned int MENU_CHEAT_ENTRY_LIMIT = 128;
static const unsigned int MENU_CHEAT_ENTRY_MAX =
    MENU_CHEAT_ENTRY_BASE + MENU_CHEAT_ENTRY_LIMIT - 1;
static const unsigned int MENU_SAVE_SLOT_BASE = MENU_FILE_SAVE_SLOT_1;
static const unsigned int MENU_LOAD_SLOT_BASE = MENU_FILE_LOAD_SLOT_1;

#ifdef _WIN32
static HWND g_menuWindow = NULL;
static HMENU g_cheatMenu = NULL;
static HMENU g_saveSlotMenu = NULL;
static HMENU g_loadSlotMenu = NULL;
#endif
static EmulatorSettings* g_menuSettings = NULL;
static std::string g_currentAppPath;
static bool g_gameRunning = false;
static bool g_pendingRelaunch = false;
static std::string g_pendingRelaunchPath;
static uint32_t g_menuCheatRevision = 0xffffffffu;
static std::string g_cheatMismatchPromptSource;
static bool g_resourceMonitorAutoOpenedForRun = false;
static bool g_resourceMonitorAutoOpenPending = false;

static void rebuildMenu(void);

static void openResourceMonitorForCurrentRun(void)
{
    if (!g_menuSettings ||
        !g_menuSettings->resourceMonitorAutoOpen ||
        !g_gameRunning ||
        g_resourceMonitorAutoOpenedForRun)
    {
        return;
    }

    g_resourceMonitorAutoOpenedForRun = true;
    printf("frontend: resource monitor auto-open requested\n");
    frontendOpenResourceMonitorWindow();
}

class ScopedFrontendModalPause
{
public:
    explicit ScopedFrontendModalPause(bool active)
        : active_(active)
    {
        if (active_)
        {
            frontendBeginModalPause();
        }
    }

    ~ScopedFrontendModalPause()
    {
        if (active_)
        {
            frontendEndModalPause();
        }
    }

    void dismiss(void)
    {
        if (!active_)
        {
            return;
        }
        frontendEndModalPause();
        active_ = false;
    }

private:
    bool active_;
};

static int showModalMessageBox(const wchar_t* text, const wchar_t* caption, unsigned int type)
{
#ifdef _WIN32
    ScopedFrontendModalPause pauseWhileOpen(true);
    return MessageBoxW(g_menuWindow, text, caption, type);
#else
    (void)text;
    (void)caption;
    (void)type;
    return 0;
#endif
}

static const wchar_t* uiText(UiTextId id)
{
    return uiText(g_menuSettings ? g_menuSettings->uiLanguage : UI_LANGUAGE_ENGLISH, id);
}

static std::wstring audioBufferLabel(int samples)
{
    wchar_t text[32] = {};
    if (g_menuSettings && g_menuSettings->uiLanguage == UI_LANGUAGE_CHINESE)
    {
        swprintf(text, sizeof(text) / sizeof(text[0]), L"%d \u91c7\u6837", samples);
    }
    else
    {
        swprintf(text, sizeof(text) / sizeof(text[0]), L"%d samples", samples);
    }
    return std::wstring(text);
}

static int percentForVideoCommand(unsigned int commandId, unsigned int firstCommand)
{
    static const int kValues[] = { 50, 75, 90, 100, 110, 125, 150 };
    unsigned int index = commandId - firstCommand;
    if (index >= sizeof(kValues) / sizeof(kValues[0]))
    {
        return 100;
    }
    return kValues[index];
}

static int percentForAudioVolumeCommand(unsigned int commandId)
{
    static const int kValues[] = { 0, 25, 50, 75, 100, 125, 150 };
    unsigned int index = commandId - MENU_AUDIO_VOLUME_0;
    if (index >= sizeof(kValues) / sizeof(kValues[0]))
    {
        return 100;
    }
    return kValues[index];
}

struct ScalePreset
{
    unsigned int commandId;
    int percent;
    const char* iniValue;
};

struct MinimizedBehaviorPreset
{
    unsigned int commandId;
    MinimizedBehavior behavior;
    UiTextId labelId;
};

struct AudioEffectPreset
{
    unsigned int commandId;
    AudioEffectMode effect;
    UiTextId labelId;
};

static const MinimizedBehaviorPreset kMinimizedBehaviorPresets[] =
{
    { MENU_VIDEO_MINIMIZED_NORMAL, MINIMIZED_BEHAVIOR_NORMAL, TXT_VIDEO_MINIMIZED_NORMAL },
    { MENU_VIDEO_MINIMIZED_PAUSE, MINIMIZED_BEHAVIOR_PAUSE, TXT_VIDEO_MINIMIZED_PAUSE },
    { MENU_VIDEO_MINIMIZED_THROTTLE, MINIMIZED_BEHAVIOR_THROTTLE, TXT_VIDEO_MINIMIZED_THROTTLE },
};

static const AudioEffectPreset kAudioEffectPresets[] =
{
    { MENU_AUDIO_EFFECT_OFF, AUDIO_EFFECT_OFF, TXT_AUDIO_EFFECT_OFF },
    { MENU_AUDIO_EFFECT_SOFT, AUDIO_EFFECT_SOFT, TXT_AUDIO_EFFECT_SOFT },
    { MENU_AUDIO_EFFECT_CLEAR, AUDIO_EFFECT_CLEAR, TXT_AUDIO_EFFECT_CLEAR },
    { MENU_AUDIO_EFFECT_BASS_BOOST, AUDIO_EFFECT_BASS_BOOST, TXT_AUDIO_EFFECT_BASS_BOOST },
    { MENU_AUDIO_EFFECT_MONO, AUDIO_EFFECT_MONO, TXT_AUDIO_EFFECT_MONO },
};

static const ScalePreset kRuntimeSpeedPresets[] =
{
    { MENU_SETTINGS_RUNTIME_SPEED_100, 100, "1.0" },
    { MENU_SETTINGS_RUNTIME_SPEED_095, 95, "0.95" },
    { MENU_SETTINGS_RUNTIME_SPEED_090, 90, "0.90" },
    { MENU_SETTINGS_RUNTIME_SPEED_085, 85, "0.85" },
    { MENU_SETTINGS_RUNTIME_SPEED_080, 80, "0.80" },
    { MENU_SETTINGS_RUNTIME_SPEED_075, 75, "0.75" },
    { MENU_SETTINGS_RUNTIME_SPEED_070, 70, "0.70" },
    { MENU_SETTINGS_RUNTIME_SPEED_065, 65, "0.65" },
    { MENU_SETTINGS_RUNTIME_SPEED_060, 60, "0.60" },
    { MENU_SETTINGS_RUNTIME_SPEED_055, 55, "0.55" },
    { MENU_SETTINGS_RUNTIME_SPEED_050, 50, "0.50" },
    { MENU_SETTINGS_RUNTIME_SPEED_045, 45, "0.45" },
    { MENU_SETTINGS_RUNTIME_SPEED_040, 40, "0.40" },
    { MENU_SETTINGS_RUNTIME_SPEED_035, 35, "0.35" },
    { MENU_SETTINGS_RUNTIME_SPEED_030, 30, "0.30" },
    { MENU_SETTINGS_RUNTIME_SPEED_025, 25, "0.25" },
    { MENU_SETTINGS_RUNTIME_SPEED_020, 20, "0.20" },
};

static const ScalePreset kDelayScalePresets[] =
{
    { MENU_SETTINGS_DELAY_SCALE_100, 100, "1.0" },
    { MENU_SETTINGS_DELAY_SCALE_095, 95, "0.95" },
    { MENU_SETTINGS_DELAY_SCALE_090, 90, "0.90" },
    { MENU_SETTINGS_DELAY_SCALE_085, 85, "0.85" },
    { MENU_SETTINGS_DELAY_SCALE_080, 80, "0.80" },
    { MENU_SETTINGS_DELAY_SCALE_075, 75, "0.75" },
    { MENU_SETTINGS_DELAY_SCALE_070, 70, "0.70" },
    { MENU_SETTINGS_DELAY_SCALE_065, 65, "0.65" },
    { MENU_SETTINGS_DELAY_SCALE_060, 60, "0.60" },
    { MENU_SETTINGS_DELAY_SCALE_055, 55, "0.55" },
    { MENU_SETTINGS_DELAY_SCALE_050, 50, "0.50" },
    { MENU_SETTINGS_DELAY_SCALE_045, 45, "0.45" },
    { MENU_SETTINGS_DELAY_SCALE_040, 40, "0.40" },
    { MENU_SETTINGS_DELAY_SCALE_035, 35, "0.35" },
    { MENU_SETTINGS_DELAY_SCALE_030, 30, "0.30" },
    { MENU_SETTINGS_DELAY_SCALE_025, 25, "0.25" },
    { MENU_SETTINGS_DELAY_SCALE_020, 20, "0.20" },
};

static const ScalePreset* scalePresetForCommand(
    unsigned int commandId,
    const ScalePreset* presets,
    size_t presetCount)
{
    for (size_t i = 0; i < presetCount; ++i)
    {
        if (presets[i].commandId == commandId)
        {
            return &presets[i];
        }
    }
    return NULL;
}

static const ScalePreset* scalePresetForValue(
    const std::string& value,
    const ScalePreset* presets,
    size_t presetCount)
{
    if (value.empty())
    {
        return NULL;
    }

    char* end = NULL;
    double parsed = strtod(value.c_str(), &end);
    if (end == value.c_str())
    {
        return NULL;
    }

    for (size_t i = 0; i < presetCount; ++i)
    {
        double target = (double)presets[i].percent / 100.0;
        double diff = parsed - target;
        if (diff < 0.0)
        {
            diff = -diff;
        }
        if (diff < 0.0005)
        {
            return &presets[i];
        }
    }
    return NULL;
}

static const ScalePreset* runtimeSpeedPresetForCommand(unsigned int commandId)
{
    return scalePresetForCommand(commandId, kRuntimeSpeedPresets,
        sizeof(kRuntimeSpeedPresets) / sizeof(kRuntimeSpeedPresets[0]));
}

static const ScalePreset* runtimeSpeedPresetForValue(const std::string& value)
{
    return scalePresetForValue(value, kRuntimeSpeedPresets,
        sizeof(kRuntimeSpeedPresets) / sizeof(kRuntimeSpeedPresets[0]));
}

static const ScalePreset* delayScalePresetForCommand(unsigned int commandId)
{
    return scalePresetForCommand(commandId, kDelayScalePresets,
        sizeof(kDelayScalePresets) / sizeof(kDelayScalePresets[0]));
}

static const ScalePreset* delayScalePresetForValue(const std::string& value)
{
    return scalePresetForValue(value, kDelayScalePresets,
        sizeof(kDelayScalePresets) / sizeof(kDelayScalePresets[0]));
}

static const MinimizedBehaviorPreset* minimizedBehaviorPresetForCommand(unsigned int commandId)
{
    for (size_t i = 0; i < sizeof(kMinimizedBehaviorPresets) / sizeof(kMinimizedBehaviorPresets[0]); ++i)
    {
        if (kMinimizedBehaviorPresets[i].commandId == commandId)
        {
            return &kMinimizedBehaviorPresets[i];
        }
    }
    return NULL;
}

static const AudioEffectPreset* audioEffectPresetForCommand(unsigned int commandId)
{
    for (size_t i = 0; i < sizeof(kAudioEffectPresets) / sizeof(kAudioEffectPresets[0]); ++i)
    {
        if (kAudioEffectPresets[i].commandId == commandId)
        {
            return &kAudioEffectPresets[i];
        }
    }
    return NULL;
}

static const AudioEffectPreset* audioEffectPresetForEffect(AudioEffectMode effect)
{
    for (size_t i = 0; i < sizeof(kAudioEffectPresets) / sizeof(kAudioEffectPresets[0]); ++i)
    {
        if (kAudioEffectPresets[i].effect == effect)
        {
            return &kAudioEffectPresets[i];
        }
    }
    return &kAudioEffectPresets[0];
}

static void applyAndSaveRuntimeSettings(void)
{
    emulatorApplyRuntimeSettings(*g_menuSettings);
    emulatorSaveSettings(*g_menuSettings);
}

static void applyAndSaveVideoSettings(void)
{
    frontendApplyVideoSettings(*g_menuSettings);
    emulatorSaveSettings(*g_menuSettings);
}

#ifdef _WIN32
static void relaunchEmulatorAfterExit(void);
#endif

static bool applyBackendSetting(const char* backendName, const char* label)
{
    std::string nextBackend = backendName ? backendName : "";
    if (g_menuSettings->backendName == nextBackend)
    {
        printf("frontend: backend setting unchanged as %s\n", label ? label : "auto");
        frontendMenuRefresh();
        return true;
    }

    g_menuSettings->backendName = nextBackend;
    emulatorApplySettingsToEnvironment(*g_menuSettings);
    emulatorSaveSettings(*g_menuSettings);
    printf("frontend: backend setting saved as %s, relaunching emulator\n",
        label ? label : "auto");
    frontendMenuRefresh();
#ifdef _WIN32
    relaunchEmulatorAfterExit();
#endif
    return true;
}

#ifdef _WIN32
static void appendMenuItem(HMENU menu, UINT id, const wchar_t* text)
{
    AppendMenuW(menu, MF_STRING, id, text);
}

static void appendCheckedMenuItem(HMENU menu, UINT id, const wchar_t* text, bool checked)
{
    AppendMenuW(menu, MF_STRING | (checked ? MF_CHECKED : MF_UNCHECKED), id, text);
}

static void appendDisabledMenuItem(HMENU menu, const wchar_t* text)
{
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, text);
}

static void appendScalePreset(HMENU menu, const ScalePreset& preset)
{
    wchar_t label[32];
    swprintf(label, sizeof(label) / sizeof(label[0]), L"%d%%", preset.percent);
    appendMenuItem(menu, preset.commandId, label);
}

static void setMenuEnabled(UINT id, bool enabled);
static void setMenuText(UINT id, const wchar_t* text);

static std::wstring saveStateTimeLabel(uint64_t timestamp)
{
    if (!timestamp)
    {
        return L"";
    }

    time_t value = (time_t)timestamp;
    struct tm localTime;
#ifdef _WIN32
    if (localtime_s(&localTime, &value) != 0)
    {
        return L"";
    }
#else
    if (!localtime_r(&value, &localTime))
    {
        return L"";
    }
#endif

    wchar_t text[32] = {};
    swprintf(text, sizeof(text) / sizeof(text[0]), L"%04d-%02d-%02d %02d:%02d:%02d",
        localTime.tm_year + 1900,
        localTime.tm_mon + 1,
        localTime.tm_mday,
        localTime.tm_hour,
        localTime.tm_min,
        localTime.tm_sec);
    return std::wstring(text);
}

static std::wstring saveStateSlotLabel(int slot, bool exists, uint64_t modifiedTime)
{
    wchar_t label[96] = {};
    bool zh = g_menuSettings && g_menuSettings->uiLanguage == UI_LANGUAGE_CHINESE;
    std::wstring timeText = saveStateTimeLabel(modifiedTime);
    if (zh)
    {
        if (exists && !timeText.empty())
        {
            swprintf(label, sizeof(label) / sizeof(label[0]), L"\u6863\u4f4d %d  %ls",
                slot, timeText.c_str());
        }
        else
        {
            swprintf(label, sizeof(label) / sizeof(label[0]), L"\u6863\u4f4d %d%s",
                slot, exists ? L"" : L" \u7a7a");
        }
    }
    else
    {
        if (exists && !timeText.empty())
        {
            swprintf(label, sizeof(label) / sizeof(label[0]), L"Slot %d  %ls",
                slot, timeText.c_str());
        }
        else
        {
            swprintf(label, sizeof(label) / sizeof(label[0]), L"Slot %d%s",
                slot, exists ? L"" : L" (empty)");
        }
    }
    return std::wstring(label);
}

static void refreshSaveLoadSlotMenus(void)
{
    if (!g_menuWindow)
    {
        return;
    }

    bool canSave = !g_currentAppPath.empty() && g_gameRunning;
    for (int slot = 1; slot <= kSaveStateSlotCount; ++slot)
    {
        SaveStateSlotInfo info = {};
        if (canSave)
        {
            info = saveStateSlotInfo(g_currentAppPath, slot);
        }
        UINT saveId = MENU_SAVE_SLOT_BASE + (UINT)(slot - 1);
        UINT loadId = MENU_LOAD_SLOT_BASE + (UINT)(slot - 1);
        std::wstring label = saveStateSlotLabel(slot, canSave && info.exists, info.modifiedTime);
        setMenuText(saveId, label.c_str());
        setMenuText(loadId, label.c_str());
        setMenuEnabled(saveId, canSave);
        setMenuEnabled(loadId, canSave && info.exists);
    }
}

static std::string cheatLogName(const CheatRuntimeEntryView& entry)
{
    if (g_menuSettings && g_menuSettings->uiLanguage == UI_LANGUAGE_CHINESE)
    {
        return entry.nameChinese.empty() ? entry.name : entry.nameChinese;
    }
    return entry.nameEnglish.empty() ? entry.name : entry.nameEnglish;
}

static std::wstring cheatMenuLabel(const CheatRuntimeEntryView& entry)
{
    std::wstring label = platformUtf8ToWide(cheatLogName(entry));
    if (label.empty())
    {
        label = L"(unnamed)";
    }
    return label;
}

static std::vector<std::string> enabledCheatFeatureKeys(const CheatRuntimeStatus& status)
{
    std::vector<std::string> keys;
    for (size_t i = 0; i < status.entries.size(); ++i)
    {
        if (status.entries[i].enabled && !status.entries[i].name.empty())
        {
            keys.push_back(status.entries[i].name);
        }
    }
    return keys;
}

static void refreshCheatMenu(const CheatRuntimeStatus& status)
{
    if (!g_cheatMenu)
    {
        return;
    }

    while (GetMenuItemCount(g_cheatMenu) > 0)
    {
        DeleteMenu(g_cheatMenu, 0, MF_BYPOSITION);
    }

    appendCheckedMenuItem(g_cheatMenu, MENU_SETTINGS_ENABLE_CHEATS,
        uiText(TXT_SETTINGS_ENABLE_CHEATS), g_menuSettings && g_menuSettings->cheatsEnabled);
    appendMenuItem(g_cheatMenu, MENU_SETTINGS_CHEAT_MANAGER, uiText(TXT_SETTINGS_CHEAT_MANAGER));
    AppendMenuW(g_cheatMenu, MF_SEPARATOR, 0, NULL);

    if (!status.loaded)
    {
        appendDisabledMenuItem(g_cheatMenu, uiText(TXT_CHEATS_NO_FILE));
    }
    else if (status.shaMismatch)
    {
        appendDisabledMenuItem(g_cheatMenu, uiText(TXT_CHEATS_SHA_MISMATCH));
    }
    else if (status.entries.empty())
    {
        appendDisabledMenuItem(g_cheatMenu, uiText(TXT_CHEATS_NO_FILE));
    }
    else
    {
        size_t count = status.entries.size();
        if (count > MENU_CHEAT_ENTRY_LIMIT)
        {
            count = MENU_CHEAT_ENTRY_LIMIT;
        }
        for (size_t i = 0; i < count; ++i)
        {
            std::wstring label = cheatMenuLabel(status.entries[i]);
            UINT flags = MF_STRING | (status.entries[i].enabled ? MF_CHECKED : MF_UNCHECKED);
            AppendMenuW(g_cheatMenu, flags, MENU_CHEAT_ENTRY_BASE + (UINT)i, label.c_str());
        }
    }

    g_menuCheatRevision = status.revision;
    DrawMenuBar(g_menuWindow);
}

static void showCheatShaMismatchPrompt(const CheatRuntimeStatus& status)
{
    if (!status.loaded || !status.shaMismatch || status.sourcePath.empty())
    {
        return;
    }
    std::string promptKey = status.sourcePath + "|" + status.appSha256 + "|" + status.currentAppSha256;
    if (g_cheatMismatchPromptSource == promptKey)
    {
        return;
    }
    g_cheatMismatchPromptSource = promptKey;

    std::wstring body = uiText(TXT_CHEATS_SHA_MISMATCH);
    body += L"\n\nFile: ";
    body += platformUtf8ToWide(status.sourcePath);
    body += L"\nCheat SHA256: ";
    body += platformUtf8ToWide(status.appSha256.empty() ? "(none)" : status.appSha256);
    body += L"\nCurrent SHA256: ";
    body += platformUtf8ToWide(status.currentAppSha256.empty() ? "(none)" : status.currentAppSha256);
    showModalMessageBox(body.c_str(), uiText(TXT_DIALOG_CHEATS_TITLE),
        MB_OK | MB_ICONWARNING);
}

static void setMenuCheck(UINT id, bool checked)
{
    if (!g_menuWindow)
    {
        return;
    }
    HMENU menu = GetMenu(g_menuWindow);
    CheckMenuItem(menu, id, MF_BYCOMMAND | (checked ? MF_CHECKED : MF_UNCHECKED));
}

static void setMenuEnabled(UINT id, bool enabled)
{
    if (!g_menuWindow)
    {
        return;
    }
    HMENU menu = GetMenu(g_menuWindow);
    EnableMenuItem(menu, id, MF_BYCOMMAND | (enabled ? MF_ENABLED : MF_GRAYED));
}

static void setMenuText(UINT id, const wchar_t* text)
{
    if (!g_menuWindow)
    {
        return;
    }
    HMENU menu = GetMenu(g_menuWindow);
    MENUITEMINFOW info;
    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    info.fMask = MIIM_STRING;
    info.dwTypeData = (LPWSTR)text;
    SetMenuItemInfoW(menu, id, FALSE, &info);
}

static void requestRelaunchAfterExit(const std::string& appPath)
{
    g_pendingRelaunch = true;
    g_pendingRelaunchPath = appPath;
    printf("frontend: queued emulator relaunch app=%s\n",
        appPath.empty() ? "(none)" : appPath.c_str());
    frontendRequestQuit();
}

static bool validateAppPathForOpen(const std::string& appPath, const char* source)
{
    if (appPath.empty())
    {
        return false;
    }
    if (!appPathHasAppExtension(appPath))
    {
        printf("frontend: rejected %s app without .app extension: %s\n",
            source ? source : "selected", appPath.c_str());
        return false;
    }
    if (!platformFileExists(appPath))
    {
        printf("frontend: rejected missing %s app: %s\n",
            source ? source : "selected", appPath.c_str());
        return false;
    }
    if (!platformProbeAppHeader(appPath))
    {
        printf("frontend: rejected invalid %s app header: %s\n",
            source ? source : "selected", appPath.c_str());
        return false;
    }
    return true;
}

bool frontendMenuRequestOpenApp(const std::string& appPath)
{
    if (!validateAppPathForOpen(appPath, "open-request"))
    {
        return false;
    }

    EmulatorSettings settings = g_menuSettings ? *g_menuSettings : emulatorLoadSettings();
    bool changed = emulatorRememberRecentApp(&settings, appPath);
    if (emulatorSaveSettings(settings))
    {
        if (g_menuSettings)
        {
            *g_menuSettings = settings;
            if (changed)
            {
                rebuildMenu();
            }
        }
    }
    else
    {
        printf("frontend: failed to save recent app before relaunch: %s\n", appPath.c_str());
    }

    requestRelaunchAfterExit(appPath);
    return true;
}

bool frontendMenuConsumeRelaunchPath(std::string* outPath)
{
    bool pending = g_pendingRelaunch;
    if (outPath)
    {
        *outPath = g_pendingRelaunchPath;
    }
    g_pendingRelaunch = false;
    g_pendingRelaunchPath.clear();
    return pending;
}

static void relaunchEmulatorAfterExit(void)
{
    requestRelaunchAfterExit(g_currentAppPath);
}

static void openTextFileNearExe(const wchar_t* fileName)
{
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring path(exePath);
    size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
    {
        path.resize(slash + 1);
    }
    path += fileName;

    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY))
    {
        showModalMessageBox(
            uiText(TXT_DEBUG_LOG_MISSING_BODY),
            uiText(TXT_DEBUG_LOG_MISSING_TITLE),
            MB_OK | MB_ICONINFORMATION);
        return;
    }

    ShellExecuteW(NULL, L"open", path.c_str(), NULL, NULL, SW_SHOWNORMAL);
}

static bool hasExtensionIgnoreCase(const std::wstring& path, const wchar_t* extension)
{
    size_t extLen = wcslen(extension);
    if (path.size() < extLen)
    {
        return false;
    }
    return _wcsicmp(path.c_str() + path.size() - extLen, extension) == 0;
}

static std::wstring screenshotExtensionForFilter(DWORD filterIndex)
{
    if (filterIndex == 2)
    {
        return L".jpg";
    }
    if (filterIndex == 3)
    {
        return L".bmp";
    }
    return L".png";
}

static void ensureScreenshotExtension(std::wstring* path, DWORD filterIndex)
{
    if (!path)
    {
        return;
    }
    if (hasExtensionIgnoreCase(*path, L".png") ||
        hasExtensionIgnoreCase(*path, L".jpg") ||
        hasExtensionIgnoreCase(*path, L".jpeg") ||
        hasExtensionIgnoreCase(*path, L".bmp"))
    {
        return;
    }
    *path += screenshotExtensionForFilter(filterIndex);
}

static void buildTimestampedScreenshotName(wchar_t* buffer, size_t bufferCount)
{
    if (!buffer || bufferCount == 0)
    {
        return;
    }

    SYSTEMTIME now;
    GetLocalTime(&now);
    swprintf(buffer, bufferCount, L"dingoo-screenshot-%04u%02u%02u-%02u%02u%02u",
        (unsigned int)now.wYear,
        (unsigned int)now.wMonth,
        (unsigned int)now.wDay,
        (unsigned int)now.wHour,
        (unsigned int)now.wMinute,
        (unsigned int)now.wSecond);
}

static void saveScreenshotWithDialog(void)
{
    wchar_t fileName[MAX_PATH] = {};
    buildTimestampedScreenshotName(fileName, MAX_PATH);
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_menuWindow;
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = uiText(TXT_DIALOG_SAVE_FILTER);
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = uiText(TXT_DIALOG_SAVE_TITLE);
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

    if (!GetSaveFileNameW(&ofn))
    {
        return;
    }

    std::wstring path(fileName);
    ensureScreenshotExtension(&path, ofn.nFilterIndex);
    bool ok = frontendSaveScreenshot(platformWideToUtf8(path).c_str());
    showModalMessageBox(
        ok ? uiText(TXT_DIALOG_SCREENSHOT_SAVED) : uiText(TXT_DIALOG_SCREENSHOT_FAILED),
        L"DingooPie",
        ok ? MB_OK | MB_ICONINFORMATION : MB_OK | MB_ICONERROR);
}

static std::wstring recentAppMenuLabel(size_t index, const std::string& appPath)
{
    std::string fileName = appFileNameFromPath(appPath);
    std::wstring label = std::to_wstring((unsigned int)(index + 1));
    label += L". ";
    label += platformUtf8ToWide(fileName.empty() ? appPath : fileName);
    return label;
}

static void populateRecentMenu(HMENU recentMenu)
{
    if (g_menuSettings && !g_menuSettings->recentAppPaths.empty())
    {
        size_t recentCount = g_menuSettings->recentAppPaths.size();
        if (recentCount > EMULATOR_RECENT_APP_LIMIT)
        {
            recentCount = EMULATOR_RECENT_APP_LIMIT;
        }
        for (size_t i = 0; i < recentCount; ++i)
        {
            std::wstring label = recentAppMenuLabel(i, g_menuSettings->recentAppPaths[i]);
            appendMenuItem(recentMenu, MENU_FILE_RECENT_BASE + (UINT)i, label.c_str());
        }
        AppendMenuW(recentMenu, MF_SEPARATOR, 0, NULL);
        appendMenuItem(recentMenu, MENU_FILE_RECENT_CLEAR, uiText(TXT_FILE_RECENT_CLEAR));
        return;
    }

    appendDisabledMenuItem(recentMenu, uiText(TXT_FILE_RECENT_EMPTY));
}
#endif

void frontendMenuAttach(void* nativeWindow, EmulatorSettings* settings, const std::string& currentAppPath)
{
    g_menuSettings = settings;
    g_currentAppPath = currentAppPath;
#ifdef _WIN32
    g_menuWindow = (HWND)nativeWindow;
    if (!g_menuWindow)
    {
        return;
    }

    HMENU menuBar = CreateMenu();
    HMENU fileMenu = CreatePopupMenu();
    HMENU recentMenu = CreatePopupMenu();
    HMENU saveSlotMenu = CreatePopupMenu();
    HMENU loadSlotMenu = CreatePopupMenu();
    HMENU optionsMenu = CreatePopupMenu();
    HMENU videoMenu = CreatePopupMenu();
    HMENU scaleMenu = CreatePopupMenu();
    HMENU antiAliasingMenu = CreatePopupMenu();
    HMENU effectMenu = CreatePopupMenu();
    HMENU brightnessMenu = CreatePopupMenu();
    HMENU contrastMenu = CreatePopupMenu();
    HMENU gammaMenu = CreatePopupMenu();
    HMENU saturationMenu = CreatePopupMenu();
    HMENU minimizedMenu = CreatePopupMenu();
    HMENU audioMenu = CreatePopupMenu();
    HMENU audioVolumeMenu = CreatePopupMenu();
    HMENU audioBufferMenu = CreatePopupMenu();
    HMENU audioEffectMenu = CreatePopupMenu();
    HMENU inputMenu = CreatePopupMenu();
    HMENU settingsMenu = CreatePopupMenu();
    HMENU backendMenu = CreatePopupMenu();
    HMENU cpuClockMenu = CreatePopupMenu();
    HMENU speedMenu = CreatePopupMenu();
    HMENU delayMenu = CreatePopupMenu();
    HMENU cheatMenu = CreatePopupMenu();
    HMENU languageMenu = CreatePopupMenu();
    HMENU debugMenu = CreatePopupMenu();
    HMENU helpMenu = CreatePopupMenu();

    appendMenuItem(fileMenu, MENU_FILE_OPEN, uiText(TXT_FILE_OPEN));
    populateRecentMenu(recentMenu);
    AppendMenuW(fileMenu, MF_POPUP, (UINT_PTR)recentMenu, uiText(TXT_FILE_RECENT));
    appendMenuItem(fileMenu, MENU_FILE_RESTART, uiText(TXT_FILE_RESTART));
    appendMenuItem(fileMenu, MENU_FILE_PAUSE_RESUME, uiText(TXT_FILE_PAUSE));
    appendMenuItem(fileMenu, MENU_FILE_SAVE_SCREENSHOT, uiText(TXT_FILE_SAVE_SCREENSHOT));
    for (int slot = 1; slot <= kSaveStateSlotCount; ++slot)
    {
        std::wstring label = saveStateSlotLabel(slot, false, 0);
        appendMenuItem(saveSlotMenu, MENU_SAVE_SLOT_BASE + (UINT)(slot - 1), label.c_str());
        appendMenuItem(loadSlotMenu, MENU_LOAD_SLOT_BASE + (UINT)(slot - 1), label.c_str());
    }
    g_saveSlotMenu = saveSlotMenu;
    g_loadSlotMenu = loadSlotMenu;
    AppendMenuW(fileMenu, MF_POPUP, (UINT_PTR)saveSlotMenu, uiText(TXT_FILE_SAVE_SLOT));
    AppendMenuW(fileMenu, MF_POPUP, (UINT_PTR)loadSlotMenu, uiText(TXT_FILE_LOAD_SLOT));
    appendMenuItem(fileMenu, MENU_FILE_SAVE_STATE_MANAGER, uiText(TXT_FILE_SAVE_STATE_MANAGER));
    AppendMenuW(fileMenu, MF_SEPARATOR, 0, NULL);
    appendMenuItem(fileMenu, MENU_FILE_EXIT, uiText(TXT_FILE_EXIT));

    appendMenuItem(scaleMenu, MENU_VIDEO_SCALE_1X, L"1x");
    appendMenuItem(scaleMenu, MENU_VIDEO_SCALE_2X, L"2x");
    appendMenuItem(scaleMenu, MENU_VIDEO_SCALE_3X, L"3x");
    AppendMenuW(scaleMenu, MF_SEPARATOR, 0, NULL);
    appendMenuItem(scaleMenu, MENU_VIDEO_FULLSCREEN, uiText(TXT_VIDEO_FULLSCREEN));
    AppendMenuW(videoMenu, MF_POPUP, (UINT_PTR)scaleMenu, uiText(TXT_VIDEO_SCALE));

    appendMenuItem(antiAliasingMenu, MENU_VIDEO_AA_OFF, uiText(TXT_VIDEO_AA_OFF));
    appendMenuItem(antiAliasingMenu, MENU_VIDEO_AA_LOW, uiText(TXT_VIDEO_AA_LOW));
    appendMenuItem(antiAliasingMenu, MENU_VIDEO_AA_CLEAR, uiText(TXT_VIDEO_AA_CLEAR));
    AppendMenuW(videoMenu, MF_POPUP, (UINT_PTR)antiAliasingMenu, uiText(TXT_VIDEO_ANTI_ALIASING));
    appendMenuItem(effectMenu, MENU_VIDEO_EFFECT_NORMAL, uiText(TXT_VIDEO_EFFECT_NORMAL));
    appendMenuItem(effectMenu, MENU_VIDEO_EFFECT_GRAYSCALE, uiText(TXT_VIDEO_EFFECT_GRAYSCALE));
    appendMenuItem(effectMenu, MENU_VIDEO_EFFECT_INVERT, uiText(TXT_VIDEO_EFFECT_INVERT));
    appendMenuItem(effectMenu, MENU_VIDEO_EFFECT_SOFT_BLUR, uiText(TXT_VIDEO_EFFECT_SOFT_BLUR));
    appendMenuItem(effectMenu, MENU_VIDEO_EFFECT_SHARPEN, uiText(TXT_VIDEO_EFFECT_SHARPEN));
    appendMenuItem(effectMenu, MENU_VIDEO_EFFECT_VIVID, uiText(TXT_VIDEO_EFFECT_VIVID));
    appendMenuItem(effectMenu, MENU_VIDEO_EFFECT_SEPIA, uiText(TXT_VIDEO_EFFECT_SEPIA));
    appendMenuItem(effectMenu, MENU_VIDEO_EFFECT_PIXEL_GRID, uiText(TXT_VIDEO_EFFECT_PIXEL_GRID));
    appendMenuItem(effectMenu, MENU_VIDEO_EFFECT_LCD_SCANLINE, uiText(TXT_VIDEO_EFFECT_LCD_SCANLINE));
    appendMenuItem(effectMenu, MENU_VIDEO_EFFECT_LIGHT_CRT, uiText(TXT_VIDEO_EFFECT_LIGHT_CRT));
    AppendMenuW(videoMenu, MF_POPUP, (UINT_PTR)effectMenu, uiText(TXT_VIDEO_EFFECT));
    appendMenuItem(brightnessMenu, MENU_VIDEO_BRIGHTNESS_50, L"50%");
    appendMenuItem(brightnessMenu, MENU_VIDEO_BRIGHTNESS_75, L"75%");
    appendMenuItem(brightnessMenu, MENU_VIDEO_BRIGHTNESS_90, L"90%");
    appendMenuItem(brightnessMenu, MENU_VIDEO_BRIGHTNESS_100, L"100%");
    appendMenuItem(brightnessMenu, MENU_VIDEO_BRIGHTNESS_110, L"110%");
    appendMenuItem(brightnessMenu, MENU_VIDEO_BRIGHTNESS_125, L"125%");
    appendMenuItem(brightnessMenu, MENU_VIDEO_BRIGHTNESS_150, L"150%");
    AppendMenuW(videoMenu, MF_POPUP, (UINT_PTR)brightnessMenu, uiText(TXT_VIDEO_BRIGHTNESS));
    appendMenuItem(contrastMenu, MENU_VIDEO_CONTRAST_50, L"50%");
    appendMenuItem(contrastMenu, MENU_VIDEO_CONTRAST_75, L"75%");
    appendMenuItem(contrastMenu, MENU_VIDEO_CONTRAST_90, L"90%");
    appendMenuItem(contrastMenu, MENU_VIDEO_CONTRAST_100, L"100%");
    appendMenuItem(contrastMenu, MENU_VIDEO_CONTRAST_110, L"110%");
    appendMenuItem(contrastMenu, MENU_VIDEO_CONTRAST_125, L"125%");
    appendMenuItem(contrastMenu, MENU_VIDEO_CONTRAST_150, L"150%");
    AppendMenuW(videoMenu, MF_POPUP, (UINT_PTR)contrastMenu, uiText(TXT_VIDEO_CONTRAST));
    appendMenuItem(gammaMenu, MENU_VIDEO_GAMMA_50, L"50%");
    appendMenuItem(gammaMenu, MENU_VIDEO_GAMMA_75, L"75%");
    appendMenuItem(gammaMenu, MENU_VIDEO_GAMMA_90, L"90%");
    appendMenuItem(gammaMenu, MENU_VIDEO_GAMMA_100, L"100%");
    appendMenuItem(gammaMenu, MENU_VIDEO_GAMMA_110, L"110%");
    appendMenuItem(gammaMenu, MENU_VIDEO_GAMMA_125, L"125%");
    appendMenuItem(gammaMenu, MENU_VIDEO_GAMMA_150, L"150%");
    AppendMenuW(videoMenu, MF_POPUP, (UINT_PTR)gammaMenu, uiText(TXT_VIDEO_GAMMA));
    appendMenuItem(saturationMenu, MENU_VIDEO_SATURATION_50, L"50%");
    appendMenuItem(saturationMenu, MENU_VIDEO_SATURATION_75, L"75%");
    appendMenuItem(saturationMenu, MENU_VIDEO_SATURATION_90, L"90%");
    appendMenuItem(saturationMenu, MENU_VIDEO_SATURATION_100, L"100%");
    appendMenuItem(saturationMenu, MENU_VIDEO_SATURATION_110, L"110%");
    appendMenuItem(saturationMenu, MENU_VIDEO_SATURATION_125, L"125%");
    appendMenuItem(saturationMenu, MENU_VIDEO_SATURATION_150, L"150%");
    AppendMenuW(videoMenu, MF_POPUP, (UINT_PTR)saturationMenu, uiText(TXT_VIDEO_SATURATION));
    for (size_t i = 0; i < sizeof(kMinimizedBehaviorPresets) / sizeof(kMinimizedBehaviorPresets[0]); ++i)
    {
        appendMenuItem(minimizedMenu, kMinimizedBehaviorPresets[i].commandId,
            uiText(kMinimizedBehaviorPresets[i].labelId));
    }
    AppendMenuW(videoMenu, MF_POPUP, (UINT_PTR)minimizedMenu, uiText(TXT_VIDEO_MINIMIZED_BEHAVIOR));
    AppendMenuW(videoMenu, MF_SEPARATOR, 0, NULL);
    appendMenuItem(videoMenu, MENU_VIDEO_PORTRAIT, uiText(TXT_VIDEO_PORTRAIT));
    appendMenuItem(videoMenu, MENU_VIDEO_SHOW_FPS, uiText(TXT_VIDEO_SHOW_FPS));

    appendMenuItem(audioVolumeMenu, MENU_AUDIO_VOLUME_0, L"0%");
    appendMenuItem(audioVolumeMenu, MENU_AUDIO_VOLUME_25, L"25%");
    appendMenuItem(audioVolumeMenu, MENU_AUDIO_VOLUME_50, L"50%");
    appendMenuItem(audioVolumeMenu, MENU_AUDIO_VOLUME_75, L"75%");
    appendMenuItem(audioVolumeMenu, MENU_AUDIO_VOLUME_100, L"100%");
    appendMenuItem(audioVolumeMenu, MENU_AUDIO_VOLUME_125, L"125%");
    appendMenuItem(audioVolumeMenu, MENU_AUDIO_VOLUME_150, L"150%");
    appendMenuItem(audioBufferMenu, MENU_AUDIO_BUFFER_512, audioBufferLabel(512).c_str());
    appendMenuItem(audioBufferMenu, MENU_AUDIO_BUFFER_1024, audioBufferLabel(1024).c_str());
    appendMenuItem(audioBufferMenu, MENU_AUDIO_BUFFER_2048, audioBufferLabel(2048).c_str());
    appendMenuItem(audioBufferMenu, MENU_AUDIO_BUFFER_4096, audioBufferLabel(4096).c_str());
    appendMenuItem(audioBufferMenu, MENU_AUDIO_BUFFER_8192, audioBufferLabel(8192).c_str());
    for (size_t i = 0; i < sizeof(kAudioEffectPresets) / sizeof(kAudioEffectPresets[0]); ++i)
    {
        appendMenuItem(audioEffectMenu, kAudioEffectPresets[i].commandId,
            uiText(kAudioEffectPresets[i].labelId));
    }
    AppendMenuW(audioMenu, MF_POPUP, (UINT_PTR)audioVolumeMenu, uiText(TXT_AUDIO_VOLUME));
    AppendMenuW(audioMenu, MF_POPUP, (UINT_PTR)audioBufferMenu, uiText(TXT_AUDIO_BUFFER));
    AppendMenuW(audioMenu, MF_POPUP, (UINT_PTR)audioEffectMenu, uiText(TXT_AUDIO_EFFECT));
    appendMenuItem(audioMenu, MENU_AUDIO_DISABLE, uiText(TXT_AUDIO_DISABLE));

    appendMenuItem(inputMenu, MENU_INPUT_DISABLE_IME, uiText(TXT_INPUT_DISABLE_IME));
    appendMenuItem(inputMenu, MENU_INPUT_SHOW_VIRTUAL_CONTROLS, uiText(TXT_INPUT_VIRTUAL_CONTROLS));
    appendMenuItem(inputMenu, MENU_INPUT_MAPPING_WINDOW, uiText(TXT_INPUT_MAPPING_WINDOW));

    AppendMenuW(optionsMenu, MF_POPUP, (UINT_PTR)videoMenu, uiText(TXT_ROOT_VIDEO));
    AppendMenuW(optionsMenu, MF_POPUP, (UINT_PTR)audioMenu, uiText(TXT_ROOT_AUDIO));
    AppendMenuW(optionsMenu, MF_POPUP, (UINT_PTR)inputMenu, uiText(TXT_ROOT_INPUT));

    // Keep this persistent settings order aligned with emulatorSaveSettings().
    // Menu selections save immediately; there is intentionally no manual Save item.
    appendMenuItem(backendMenu, MENU_SETTINGS_BACKEND_AUTO, uiText(TXT_SETTINGS_BACKEND_AUTO));
    appendMenuItem(backendMenu, MENU_SETTINGS_BACKEND_IRJIT, uiText(TXT_SETTINGS_BACKEND_IRJIT));
    appendMenuItem(backendMenu, MENU_SETTINGS_BACKEND_INTERPRETER, uiText(TXT_SETTINGS_BACKEND_INTERPRETER));
    AppendMenuW(settingsMenu, MF_POPUP, (UINT_PTR)backendMenu, uiText(TXT_SETTINGS_CPU_BACKEND));
    appendMenuItem(cpuClockMenu, MENU_SETTINGS_CPU_CLOCK_AUTO, uiText(TXT_SETTINGS_AUTO));
    appendMenuItem(cpuClockMenu, MENU_SETTINGS_CPU_CLOCK_200, L"200 MHz");
    appendMenuItem(cpuClockMenu, MENU_SETTINGS_CPU_CLOCK_336, L"336 MHz");
    appendMenuItem(cpuClockMenu, MENU_SETTINGS_CPU_CLOCK_370, L"370 MHz");
    appendMenuItem(cpuClockMenu, MENU_SETTINGS_CPU_CLOCK_400, L"400 MHz");
    appendMenuItem(cpuClockMenu, MENU_SETTINGS_CPU_CLOCK_430, L"430 MHz");
    AppendMenuW(settingsMenu, MF_POPUP, (UINT_PTR)cpuClockMenu, uiText(TXT_SETTINGS_CPU_CLOCK));
    appendMenuItem(speedMenu, MENU_SETTINGS_RUNTIME_SPEED_AUTO, uiText(TXT_SETTINGS_AUTO));
    for (size_t i = 0; i < sizeof(kRuntimeSpeedPresets) / sizeof(kRuntimeSpeedPresets[0]); ++i)
    {
        appendScalePreset(speedMenu, kRuntimeSpeedPresets[i]);
    }
    AppendMenuW(settingsMenu, MF_POPUP, (UINT_PTR)speedMenu, uiText(TXT_SETTINGS_RUNTIME_SPEED));

    appendMenuItem(delayMenu, MENU_SETTINGS_DELAY_SCALE_AUTO, uiText(TXT_SETTINGS_AUTO));
    for (size_t i = 0; i < sizeof(kDelayScalePresets) / sizeof(kDelayScalePresets[0]); ++i)
    {
        appendScalePreset(delayMenu, kDelayScalePresets[i]);
    }
    AppendMenuW(settingsMenu, MF_POPUP, (UINT_PTR)delayMenu, uiText(TXT_SETTINGS_DELAY_SCALE));
    g_cheatMenu = cheatMenu;
    refreshCheatMenu(cheatRuntimeGetStatus());
    AppendMenuW(settingsMenu, MF_POPUP, (UINT_PTR)cheatMenu, uiText(TXT_SETTINGS_CHEATS));
    AppendMenuW(settingsMenu, MF_SEPARATOR, 0, NULL);

    appendMenuItem(languageMenu, MENU_SETTINGS_LANGUAGE_CHINESE, L"\u4e2d\u6587");
    appendMenuItem(languageMenu, MENU_SETTINGS_LANGUAGE_ENGLISH, L"English");
    AppendMenuW(settingsMenu, MF_POPUP, (UINT_PTR)languageMenu, uiText(TXT_SETTINGS_LANGUAGE));
    AppendMenuW(settingsMenu, MF_SEPARATOR, 0, NULL);

    appendMenuItem(settingsMenu, MENU_SETTINGS_RESET, uiText(TXT_SETTINGS_RESET));

    appendMenuItem(debugMenu, MENU_DEBUG_SHOW_CONSOLE, uiText(TXT_DEBUG_CONSOLE));
    appendMenuItem(debugMenu, MENU_DEBUG_PROFILE, uiText(TXT_DEBUG_PROFILE));
    appendMenuItem(debugMenu, MENU_DEBUG_OPEN_LOG, uiText(TXT_DEBUG_OPEN_LOG));
    appendMenuItem(debugMenu, MENU_DEBUG_RESOURCE_MONITOR, uiText(TXT_DEBUG_RESOURCE_MONITOR));
    appendMenuItem(debugMenu, MENU_DEBUG_MEMORY_SEARCHER, uiText(TXT_DEBUG_MEMORY_SEARCHER));
    appendMenuItem(debugMenu, MENU_DEBUG_DEBUGGER, uiText(TXT_DEBUG_DEBUGGER));

    appendMenuItem(helpMenu, MENU_HELP_ABOUT, uiText(TXT_HELP_ABOUT));

    AppendMenuW(menuBar, MF_POPUP, (UINT_PTR)fileMenu, uiText(TXT_ROOT_FILE));
    AppendMenuW(menuBar, MF_POPUP, (UINT_PTR)optionsMenu, uiText(TXT_ROOT_OPTIONS));
    AppendMenuW(menuBar, MF_POPUP, (UINT_PTR)settingsMenu, uiText(TXT_ROOT_SETTINGS));
    AppendMenuW(menuBar, MF_POPUP, (UINT_PTR)debugMenu, uiText(TXT_ROOT_DEBUG));
    AppendMenuW(menuBar, MF_POPUP, (UINT_PTR)helpMenu, uiText(TXT_ROOT_HELP));

    SetMenu(g_menuWindow, menuBar);
    DrawMenuBar(g_menuWindow);
    g_menuCheatRevision = cheatRuntimeGetStatus().revision;
#endif
    frontendMenuRefresh();
}

static void rebuildMenu(void)
{
#ifdef _WIN32
    if (!g_menuWindow)
    {
        return;
    }
    HMENU oldMenu = GetMenu(g_menuWindow);
    SetMenu(g_menuWindow, NULL);
    if (oldMenu)
    {
        g_cheatMenu = NULL;
        g_saveSlotMenu = NULL;
        g_loadSlotMenu = NULL;
        DestroyMenu(oldMenu);
    }
    frontendMenuAttach(g_menuWindow, g_menuSettings, g_currentAppPath);
#endif
}

void frontendMenuRefresh(void)
{
    if (!g_menuSettings)
    {
        return;
    }
#ifdef _WIN32
    const bool hasCurrentApp = !g_currentAppPath.empty();
    const bool gamePaused = frontendUserGamePaused();
    CheatRuntimeStatus cheatStatus = cheatRuntimeGetStatus();
    if (cheatStatus.revision != g_menuCheatRevision)
    {
        refreshCheatMenu(cheatStatus);
    }
    showCheatShaMismatchPrompt(cheatStatus);
    setMenuEnabled(MENU_FILE_RESTART, hasCurrentApp);
    setMenuEnabled(MENU_FILE_PAUSE_RESUME, hasCurrentApp && g_gameRunning);
    setMenuText(MENU_FILE_PAUSE_RESUME, uiText(gamePaused ? TXT_FILE_RESUME : TXT_FILE_PAUSE));
    setMenuEnabled(MENU_FILE_SAVE_SCREENSHOT, hasCurrentApp && g_gameRunning);
    refreshSaveLoadSlotMenus();
    setMenuEnabled(MENU_FILE_SAVE_STATE_MANAGER, hasCurrentApp);
    setMenuCheck(MENU_VIDEO_SCALE_1X, g_menuSettings->windowScale == 1);
    setMenuCheck(MENU_VIDEO_SCALE_2X, g_menuSettings->windowScale == 2);
    setMenuCheck(MENU_VIDEO_SCALE_3X, g_menuSettings->windowScale == 3);
    setMenuCheck(MENU_VIDEO_FULLSCREEN, g_menuSettings->fullscreen);
    setMenuCheck(MENU_VIDEO_AA_OFF, g_menuSettings->antiAliasing == ANTI_ALIASING_OFF);
    setMenuCheck(MENU_VIDEO_AA_LOW, g_menuSettings->antiAliasing == ANTI_ALIASING_LOW);
    setMenuCheck(MENU_VIDEO_AA_CLEAR, g_menuSettings->antiAliasing == ANTI_ALIASING_CLEAR);
    setMenuCheck(MENU_VIDEO_EFFECT_NORMAL, g_menuSettings->colorEffect == COLOR_EFFECT_NORMAL);
    setMenuCheck(MENU_VIDEO_EFFECT_GRAYSCALE, g_menuSettings->colorEffect == COLOR_EFFECT_GRAYSCALE);
    setMenuCheck(MENU_VIDEO_EFFECT_INVERT, g_menuSettings->colorEffect == COLOR_EFFECT_INVERT);
    setMenuCheck(MENU_VIDEO_EFFECT_SOFT_BLUR, g_menuSettings->colorEffect == COLOR_EFFECT_SOFT_BLUR);
    setMenuCheck(MENU_VIDEO_EFFECT_SHARPEN, g_menuSettings->colorEffect == COLOR_EFFECT_SHARPEN);
    setMenuCheck(MENU_VIDEO_EFFECT_VIVID, g_menuSettings->colorEffect == COLOR_EFFECT_VIVID);
    setMenuCheck(MENU_VIDEO_EFFECT_SEPIA, g_menuSettings->colorEffect == COLOR_EFFECT_SEPIA);
    setMenuCheck(MENU_VIDEO_EFFECT_PIXEL_GRID, g_menuSettings->colorEffect == COLOR_EFFECT_PIXEL_GRID);
    setMenuCheck(MENU_VIDEO_EFFECT_LCD_SCANLINE, g_menuSettings->colorEffect == COLOR_EFFECT_LCD_SCANLINE);
    setMenuCheck(MENU_VIDEO_EFFECT_LIGHT_CRT, g_menuSettings->colorEffect == COLOR_EFFECT_LIGHT_CRT);
    setMenuCheck(MENU_VIDEO_BRIGHTNESS_50, g_menuSettings->brightnessPercent == 50);
    setMenuCheck(MENU_VIDEO_BRIGHTNESS_75, g_menuSettings->brightnessPercent == 75);
    setMenuCheck(MENU_VIDEO_BRIGHTNESS_90, g_menuSettings->brightnessPercent == 90);
    setMenuCheck(MENU_VIDEO_BRIGHTNESS_100, g_menuSettings->brightnessPercent == 100);
    setMenuCheck(MENU_VIDEO_BRIGHTNESS_110, g_menuSettings->brightnessPercent == 110);
    setMenuCheck(MENU_VIDEO_BRIGHTNESS_125, g_menuSettings->brightnessPercent == 125);
    setMenuCheck(MENU_VIDEO_BRIGHTNESS_150, g_menuSettings->brightnessPercent == 150);
    setMenuCheck(MENU_VIDEO_CONTRAST_50, g_menuSettings->contrastPercent == 50);
    setMenuCheck(MENU_VIDEO_CONTRAST_75, g_menuSettings->contrastPercent == 75);
    setMenuCheck(MENU_VIDEO_CONTRAST_90, g_menuSettings->contrastPercent == 90);
    setMenuCheck(MENU_VIDEO_CONTRAST_100, g_menuSettings->contrastPercent == 100);
    setMenuCheck(MENU_VIDEO_CONTRAST_110, g_menuSettings->contrastPercent == 110);
    setMenuCheck(MENU_VIDEO_CONTRAST_125, g_menuSettings->contrastPercent == 125);
    setMenuCheck(MENU_VIDEO_CONTRAST_150, g_menuSettings->contrastPercent == 150);
    setMenuCheck(MENU_VIDEO_GAMMA_50, g_menuSettings->gammaPercent == 50);
    setMenuCheck(MENU_VIDEO_GAMMA_75, g_menuSettings->gammaPercent == 75);
    setMenuCheck(MENU_VIDEO_GAMMA_90, g_menuSettings->gammaPercent == 90);
    setMenuCheck(MENU_VIDEO_GAMMA_100, g_menuSettings->gammaPercent == 100);
    setMenuCheck(MENU_VIDEO_GAMMA_110, g_menuSettings->gammaPercent == 110);
    setMenuCheck(MENU_VIDEO_GAMMA_125, g_menuSettings->gammaPercent == 125);
    setMenuCheck(MENU_VIDEO_GAMMA_150, g_menuSettings->gammaPercent == 150);
    setMenuCheck(MENU_VIDEO_SATURATION_50, g_menuSettings->saturationPercent == 50);
    setMenuCheck(MENU_VIDEO_SATURATION_75, g_menuSettings->saturationPercent == 75);
    setMenuCheck(MENU_VIDEO_SATURATION_90, g_menuSettings->saturationPercent == 90);
    setMenuCheck(MENU_VIDEO_SATURATION_100, g_menuSettings->saturationPercent == 100);
    setMenuCheck(MENU_VIDEO_SATURATION_110, g_menuSettings->saturationPercent == 110);
    setMenuCheck(MENU_VIDEO_SATURATION_125, g_menuSettings->saturationPercent == 125);
    setMenuCheck(MENU_VIDEO_SATURATION_150, g_menuSettings->saturationPercent == 150);
    for (size_t i = 0; i < sizeof(kMinimizedBehaviorPresets) / sizeof(kMinimizedBehaviorPresets[0]); ++i)
    {
        setMenuCheck(kMinimizedBehaviorPresets[i].commandId,
            g_menuSettings->minimizedBehavior == kMinimizedBehaviorPresets[i].behavior);
    }
    setMenuCheck(MENU_VIDEO_PORTRAIT, g_menuSettings->portraitMode);
    setMenuCheck(MENU_VIDEO_SHOW_FPS, g_menuSettings->showFps);
    setMenuCheck(MENU_AUDIO_VOLUME_0, g_menuSettings->audioVolumePercent == 0);
    setMenuCheck(MENU_AUDIO_VOLUME_25, g_menuSettings->audioVolumePercent == 25);
    setMenuCheck(MENU_AUDIO_VOLUME_50, g_menuSettings->audioVolumePercent == 50);
    setMenuCheck(MENU_AUDIO_VOLUME_75, g_menuSettings->audioVolumePercent == 75);
    setMenuCheck(MENU_AUDIO_VOLUME_100, g_menuSettings->audioVolumePercent == 100);
    setMenuCheck(MENU_AUDIO_VOLUME_125, g_menuSettings->audioVolumePercent == 125);
    setMenuCheck(MENU_AUDIO_VOLUME_150, g_menuSettings->audioVolumePercent == 150);
    setMenuCheck(MENU_AUDIO_BUFFER_512, g_menuSettings->audioBufferSamples == 512);
    setMenuCheck(MENU_AUDIO_BUFFER_1024, g_menuSettings->audioBufferSamples == 1024);
    setMenuCheck(MENU_AUDIO_BUFFER_2048, g_menuSettings->audioBufferSamples == 2048);
    setMenuCheck(MENU_AUDIO_BUFFER_4096, g_menuSettings->audioBufferSamples == 4096);
    setMenuCheck(MENU_AUDIO_BUFFER_8192, g_menuSettings->audioBufferSamples == 8192);
    const AudioEffectPreset* checkedAudioEffect = audioEffectPresetForEffect(g_menuSettings->audioEffect);
    for (size_t i = 0; i < sizeof(kAudioEffectPresets) / sizeof(kAudioEffectPresets[0]); ++i)
    {
        setMenuCheck(kAudioEffectPresets[i].commandId,
            checkedAudioEffect && checkedAudioEffect->commandId == kAudioEffectPresets[i].commandId);
    }
    setMenuCheck(MENU_AUDIO_DISABLE, g_menuSettings->audioDisabled);
    setMenuCheck(MENU_INPUT_DISABLE_IME, g_menuSettings->disableIme);
    setMenuCheck(MENU_INPUT_SHOW_VIRTUAL_CONTROLS, g_menuSettings->showVirtualControls);
    setMenuCheck(MENU_SETTINGS_BACKEND_AUTO, g_menuSettings->backendName.empty());
    setMenuCheck(MENU_SETTINGS_BACKEND_IRJIT, g_menuSettings->backendName == "ppsspp_irjit");
    setMenuCheck(MENU_SETTINGS_BACKEND_INTERPRETER, g_menuSettings->backendName == "interpreter");
    setMenuCheck(MENU_SETTINGS_CPU_CLOCK_AUTO, g_menuSettings->cpuClockHz.empty());
    setMenuCheck(MENU_SETTINGS_CPU_CLOCK_200, g_menuSettings->cpuClockHz == "200000000");
    setMenuCheck(MENU_SETTINGS_CPU_CLOCK_336, g_menuSettings->cpuClockHz == "336000000");
    setMenuCheck(MENU_SETTINGS_CPU_CLOCK_370, g_menuSettings->cpuClockHz == "370000000");
    setMenuCheck(MENU_SETTINGS_CPU_CLOCK_400, g_menuSettings->cpuClockHz == "400000000");
    setMenuCheck(MENU_SETTINGS_CPU_CLOCK_430, g_menuSettings->cpuClockHz == "430000000");
    setMenuCheck(MENU_SETTINGS_RUNTIME_SPEED_AUTO, g_menuSettings->runtimeSpeedScale.empty());
    const ScalePreset* checkedSpeed = runtimeSpeedPresetForValue(g_menuSettings->runtimeSpeedScale);
    for (size_t i = 0; i < sizeof(kRuntimeSpeedPresets) / sizeof(kRuntimeSpeedPresets[0]); ++i)
    {
        setMenuCheck(kRuntimeSpeedPresets[i].commandId,
            checkedSpeed && checkedSpeed->commandId == kRuntimeSpeedPresets[i].commandId);
    }
    setMenuCheck(MENU_SETTINGS_DELAY_SCALE_AUTO, g_menuSettings->ostimeDlyScale.empty());
    const ScalePreset* checkedDelay = delayScalePresetForValue(g_menuSettings->ostimeDlyScale);
    for (size_t i = 0; i < sizeof(kDelayScalePresets) / sizeof(kDelayScalePresets[0]); ++i)
    {
        setMenuCheck(kDelayScalePresets[i].commandId,
            checkedDelay && checkedDelay->commandId == kDelayScalePresets[i].commandId);
    }
    setMenuCheck(MENU_SETTINGS_ENABLE_CHEATS, g_menuSettings->cheatsEnabled);
    setMenuEnabled(MENU_SETTINGS_CHEAT_MANAGER, g_gameRunning);
    size_t cheatCount = cheatStatus.entries.size();
    if (cheatCount > MENU_CHEAT_ENTRY_LIMIT)
    {
        cheatCount = MENU_CHEAT_ENTRY_LIMIT;
    }
    for (size_t i = 0; i < cheatCount; ++i)
    {
        setMenuCheck(MENU_CHEAT_ENTRY_BASE + (UINT)i, cheatStatus.entries[i].enabled);
    }
    setMenuCheck(MENU_SETTINGS_LANGUAGE_CHINESE, g_menuSettings->uiLanguage == UI_LANGUAGE_CHINESE);
    setMenuCheck(MENU_SETTINGS_LANGUAGE_ENGLISH, g_menuSettings->uiLanguage == UI_LANGUAGE_ENGLISH);
    setMenuCheck(MENU_DEBUG_SHOW_CONSOLE, g_menuSettings->showDebugConsole);
    setMenuCheck(MENU_DEBUG_PROFILE,
        g_menuSettings->debugProfile || runtimeLogProfileEnabled());
    setMenuEnabled(MENU_DEBUG_RESOURCE_MONITOR, true);
    setMenuCheck(MENU_DEBUG_RESOURCE_MONITOR, g_menuSettings->resourceMonitorAutoOpen);
    setMenuEnabled(MENU_DEBUG_MEMORY_SEARCHER, g_gameRunning);
    setMenuEnabled(MENU_DEBUG_DEBUGGER, g_gameRunning);
#endif
}

void frontendMenuRefreshCheats(void)
{
    if (!g_menuSettings)
    {
        return;
    }
#ifdef _WIN32
    if (!g_cheatMenu || cheatRuntimeRevision() == g_menuCheatRevision)
    {
        return;
    }

    CheatRuntimeStatus cheatStatus = cheatRuntimeGetStatus();
    if (cheatStatus.revision != g_menuCheatRevision)
    {
        refreshCheatMenu(cheatStatus);
    }
    showCheatShaMismatchPrompt(cheatStatus);
#endif
}

bool frontendMenuGameRunning(void)
{
    return g_gameRunning;
}

void frontendMenuSetGameRunning(bool running)
{
    bool changed = g_gameRunning != running;
    g_gameRunning = running;
    if (changed && running)
    {
        frontendReleaseIdleResources();
        g_resourceMonitorAutoOpenedForRun = false;
        g_resourceMonitorAutoOpenPending = true;
    }
    else if (changed && !running)
    {
        frontendClearPauseRequests();
        g_resourceMonitorAutoOpenedForRun = false;
        g_resourceMonitorAutoOpenPending = false;
    }
    frontendMenuRefresh();
}

void frontendMenuProcessDeferredResourceMonitorOpen(void)
{
    if (!g_resourceMonitorAutoOpenPending)
    {
        return;
    }

    g_resourceMonitorAutoOpenPending = false;
    openResourceMonitorForCurrentRun();
}

static bool handleRecentAppCommand(unsigned int commandId)
{
    if (commandId < MENU_FILE_RECENT_BASE || commandId > MENU_FILE_RECENT_MAX)
    {
        return false;
    }

    size_t index = commandId - MENU_FILE_RECENT_BASE;
    if (index >= g_menuSettings->recentAppPaths.size())
    {
        return true;
    }

    std::string appPath = g_menuSettings->recentAppPaths[index];
    if (!validateAppPathForOpen(appPath, "recent"))
    {
        if (emulatorRemoveRecentApp(g_menuSettings, appPath))
        {
            emulatorSaveSettings(*g_menuSettings);
            rebuildMenu();
        }
        return true;
    }

    frontendMenuRequestOpenApp(appPath);
    return true;
}

static bool handleCheatEntryCommand(unsigned int commandId)
{
    if (commandId < MENU_CHEAT_ENTRY_BASE || commandId > MENU_CHEAT_ENTRY_MAX)
    {
        return false;
    }

    size_t index = commandId - MENU_CHEAT_ENTRY_BASE;
    CheatRuntimeStatus status = cheatRuntimeGetStatus();
    if (!status.available || index >= status.entries.size())
    {
        return true;
    }

    bool enableEntry = !status.entries[index].enabled;
    if (cheatRuntimeSetEntryEnabled(index, enableEntry))
    {
        bool settingsChanged = false;
        if (enableEntry && !status.enabled)
        {
            g_menuSettings->cheatsEnabled = true;
            cheatRuntimeSetEnabled(true);
            emulatorApplySettingsToEnvironment(*g_menuSettings);
            settingsChanged = true;
        }
        if (enableEntry)
        {
            cheatRuntimeApplyNow();
        }
        CheatRuntimeStatus updatedStatus = cheatRuntimeGetStatus();
        // The global cheat switch and per-game feature list are saved
        // separately so feature defaults remain unchecked until selected.
        settingsChanged = emulatorSetCheatFeatureKeysForApp(g_menuSettings,
            g_currentAppPath, enabledCheatFeatureKeys(updatedStatus)) || settingsChanged;
        if (settingsChanged)
        {
            emulatorSaveSettings(*g_menuSettings);
        }
        std::string logName = cheatLogName(status.entries[index]);
        printf("frontend: cheat '%s' %s\n",
            logName.c_str(), enableEntry ? "enabled" : "disabled");
        frontendMenuRefresh();
    }
    return true;
}

struct SaveStateDialogSlot
{
    int slot;
    bool exists;
    uint64_t modifiedTime;
};

#ifdef _WIN32
struct SaveStateProgressDialog
{
    HWND window;
    HWND label;
    HWND progress;
    HFONT font;
    bool ownFont;
    bool visible;
};

static void destroySaveStateProgressDialog(SaveStateProgressDialog* dialog)
{
    if (!dialog)
    {
        return;
    }
    if (dialog->window)
    {
        DestroyWindow(dialog->window);
        dialog->window = NULL;
        dialog->label = NULL;
        dialog->progress = NULL;
    }
    if (dialog->font && dialog->ownFont)
    {
        DeleteObject(dialog->font);
    }
    dialog->font = NULL;
    dialog->ownFont = false;
    dialog->visible = false;
}

static HFONT saveStateProgressDialogFont(SaveStateProgressDialog* dialog)
{
    if (!dialog)
    {
        return (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    }
    if (dialog->font)
    {
        return dialog->font;
    }

    NONCLIENTMETRICSW metrics;
    memset(&metrics, 0, sizeof(metrics));
    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, metrics.cbSize, &metrics, 0))
    {
        dialog->font = CreateFontIndirectW(&metrics.lfMessageFont);
        dialog->ownFont = dialog->font != NULL;
    }
    if (!dialog->font)
    {
        dialog->font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        dialog->ownFont = false;
    }
    return dialog->font;
}

static void centerSaveStateProgressDialog(HWND dialogWindow)
{
    if (!dialogWindow)
    {
        return;
    }

    RECT dialogRect = {};
    RECT parentRect = {};
    if (!GetWindowRect(dialogWindow, &dialogRect))
    {
        return;
    }
    if (!g_menuWindow || !GetWindowRect(g_menuWindow, &parentRect))
    {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &parentRect, 0);
    }

    int dialogWidth = dialogRect.right - dialogRect.left;
    int dialogHeight = dialogRect.bottom - dialogRect.top;
    int x = parentRect.left + ((parentRect.right - parentRect.left) - dialogWidth) / 2;
    int y = parentRect.top + ((parentRect.bottom - parentRect.top) - dialogHeight) / 2;
    SetWindowPos(dialogWindow, NULL, x, y, 0, 0,
        SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSIZE | SWP_NOZORDER);
}

static void ensureSaveStateProgressDialog(SaveStateProgressDialog* dialog)
{
    if (!dialog || dialog->window)
    {
        return;
    }

    INITCOMMONCONTROLSEX controls;
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&controls);

    dialog->window = CreateWindowExW(WS_EX_TOOLWINDOW,
        L"#32770", uiText(TXT_DIALOG_STATE_SAVE_TITLE),
        WS_CAPTION,
        CW_USEDEFAULT, CW_USEDEFAULT, 360, 96,
        g_menuWindow, NULL, GetModuleHandleW(NULL), NULL);
    if (!dialog->window)
    {
        return;
    }
    DeleteMenu(GetSystemMenu(dialog->window, FALSE), SC_CLOSE, MF_BYCOMMAND);
    centerSaveStateProgressDialog(dialog->window);

    dialog->label = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | platformWin32NormalizeChildStyle(L"STATIC", SS_LEFT),
        18, 12, 310, 20,
        dialog->window, NULL, GetModuleHandleW(NULL), NULL);
    dialog->progress = CreateWindowExW(0, PROGRESS_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
        18, 38, 310, 18,
        dialog->window, NULL, GetModuleHandleW(NULL), NULL);
    HFONT font = saveStateProgressDialogFont(dialog);
    SendMessageW(dialog->window, WM_SETFONT, (WPARAM)font, TRUE);
    SendMessageW(dialog->label, WM_SETFONT, (WPARAM)font, TRUE);
    SendMessageW(dialog->progress, WM_SETFONT, (WPARAM)font, TRUE);
    SendMessageW(dialog->progress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
}

static void updateWindowIfValid(HWND window)
{
    if (window)
    {
        UpdateWindow(window);
    }
}

static void pumpWindowMessages(HWND window)
{
    MSG msg;
    while (window && PeekMessageW(&msg, window, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

static void updateSaveStateProgressDialog(SaveStateProgressDialog* dialog,
    const SaveStateProgress& progress)
{
    if (!dialog)
    {
        return;
    }

    ensureSaveStateProgressDialog(dialog);
    if (!dialog->window || !IsWindow(dialog->window))
    {
        dialog->window = NULL;
        dialog->label = NULL;
        dialog->progress = NULL;
        dialog->visible = false;
        return;
    }

    const wchar_t* text = progress.phase == SAVE_STATE_PROGRESS_DECOMPRESS ?
        uiText(TXT_DIALOG_STATE_DECOMPRESSING) :
        uiText(TXT_DIALOG_STATE_COMPRESSING);
    SetWindowTextW(dialog->label, text);
    SendMessageW(dialog->progress, PBM_SETPOS, progress.percent, 0);
    if (!dialog->visible)
    {
        ShowWindow(dialog->window, SW_SHOWNORMAL);
        dialog->visible = true;
    }
    updateWindowIfValid(dialog->window);
    updateWindowIfValid(dialog->label);
    updateWindowIfValid(dialog->progress);

    pumpWindowMessages(dialog->window);
    pumpWindowMessages(dialog->label);
    pumpWindowMessages(dialog->progress);
}

static void saveStateProgressCallback(const SaveStateProgress& progress, void* userData)
{
    updateSaveStateProgressDialog((SaveStateProgressDialog*)userData, progress);
}
#endif

static SaveStateDialogSlot makeSaveStateDialogSlot(int slot, bool exists, uint64_t modifiedTime)
{
    SaveStateDialogSlot dialogSlot = {};
    dialogSlot.slot = slot;
    dialogSlot.exists = exists;
    dialogSlot.modifiedTime = modifiedTime;
    return dialogSlot;
}

static SaveStateDialogSlot makeSaveStateDialogSlot(int slot, const SaveStateSlotInfo& info)
{
    return makeSaveStateDialogSlot(slot, info.exists, info.modifiedTime);
}

static std::wstring saveStateMessage(UiTextId id, const SaveStateDialogSlot& slotInfo,
    const std::string& detail)
{
    std::wstring message = uiText(id);
    message += L"\n";
    message += saveStateSlotLabel(slotInfo.slot, slotInfo.exists, slotInfo.modifiedTime);
    if (!detail.empty())
    {
        message += L"\n";
        message += platformUtf8ToWide(detail);
    }
    return message;
}

static bool confirmSaveStateAction(UiTextId id, const SaveStateDialogSlot& slotInfo,
    const wchar_t* title, bool warning)
{
#ifdef _WIN32
    std::wstring message = saveStateMessage(id, slotInfo, "");
    UINT icon = warning ? MB_ICONWARNING : MB_ICONQUESTION;
    return showModalMessageBox(message.c_str(), title,
        MB_YESNO | icon | MB_DEFBUTTON2) == IDYES;
#else
    (void)id;
    (void)slotInfo;
    (void)title;
    (void)warning;
    return true;
#endif
}

static bool waitForSaveStatePause(const char* action, uint32_t* expectedOut, uint32_t* waitersOut)
{
    static const uint32_t kPauseTimeoutMs = 5000;
    static const uint32_t kPausePollMs = 25;
    uint32_t expected = emulatorRuntimeActiveThreadCount();
    if (expected == 0)
    {
        expected = 1;
    }

    // Save/load must observe all guest runtimes at a pause point before copying memory and registers.
    bool paused = false;
    std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(kPauseTimeoutMs);
    while (!paused)
    {
        emulatorRuntimeNotifyPauseRequested();
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        if (now >= deadline)
        {
            break;
        }
        uint32_t waitMs = kPausePollMs;
        uint32_t remainingMs = (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - now).count();
        if (remainingMs < waitMs)
        {
            waitMs = remainingMs;
        }
        paused = frontendWaitForRuntimePausedWaiters(waitMs, expected);
    }
    uint32_t waiters = frontendRuntimePausedWaiterCount();
    printf("frontend: %s state pause waiters=%u expected=%u %s\n",
        action ? action : "save",
        waiters,
        expected,
        paused ? "ready" : "timeout");
    if (expectedOut)
    {
        *expectedOut = expected;
    }
    if (waitersOut)
    {
        *waitersOut = waiters;
    }
    return paused;
}

static bool validateSaveStateRuntimeCount(const SaveStateSlotInfo& info,
    uint32_t expected, std::string* errorOut)
{
    if (!info.exists)
    {
        return true;
    }
    if (!info.runtimeCountValid)
    {
        if (errorOut) *errorOut = "unsupported save-state file";
        return false;
    }
    if (info.runtimeCount != expected)
    {
        if (errorOut)
        {
            *errorOut = platformWideToUtf8(uiText(TXT_DIALOG_STATE_STAGE_MISMATCH));
        }
        // A mismatch usually means the user is at a title/menu stage while the
        // save was captured after the game created additional task runtimes.
        printf("frontend: save-state runtime count mismatch current=%u saved=%u\n",
            expected, info.runtimeCount);
        return false;
    }
    return true;
}

static bool saveStateSlotNow(int slot, std::string* errorOut, bool showProgress)
{
    ScopedFrontendModalPause pauseWhileSaving(true);
    EmulatorRuntimeState state;
    std::string error;
#ifdef _WIN32
    SaveStateProgressDialog progressDialog = {};
    SaveStateProgressCallback progressCallback = showProgress ? saveStateProgressCallback : NULL;
    void* progressUserData = showProgress ? &progressDialog : NULL;
#else
    (void)showProgress;
    SaveStateProgressCallback progressCallback = NULL;
    void* progressUserData = NULL;
#endif
    bool paused = waitForSaveStatePause("save", NULL, NULL);
    bool ok = paused && emulatorRuntimeCaptureState(&state) &&
        saveStateWriteSlot(g_currentAppPath, slot, state, &error,
            progressCallback, progressUserData);
#ifdef _WIN32
    destroySaveStateProgressDialog(&progressDialog);
#endif
    if (!ok && error.empty())
    {
        error = paused ? "runtime state is not available" : "runtime did not pause in time";
    }

    printf("frontend: save state slot %d %s%s%s\n", slot, ok ? "saved" : "failed",
        error.empty() ? "" : ": ", error.c_str());
    if (ok)
    {
        std::string thumbnailPath = saveStateThumbnailPathForSlot(g_currentAppPath, slot);
        if (!thumbnailPath.empty() &&
            !frontendSaveScreenshotThumbnail(thumbnailPath.c_str(), 160, 120))
        {
            printf("frontend: save state slot %d thumbnail failed: %s\n",
                slot, thumbnailPath.c_str());
        }
    }
    if (errorOut)
    {
        *errorOut = error;
    }
    frontendMenuRefresh();
    return ok;
}

static bool loadStateSlotNow(int slot, std::string* errorOut, bool showProgress)
{
    ScopedFrontendModalPause pauseWhileLoading(true);
    EmulatorRuntimeState state;
    std::string error;
#ifdef _WIN32
    SaveStateProgressDialog progressDialog = {};
    SaveStateProgressCallback progressCallback = showProgress ? saveStateProgressCallback : NULL;
    void* progressUserData = showProgress ? &progressDialog : NULL;
#else
    (void)showProgress;
    SaveStateProgressCallback progressCallback = NULL;
    void* progressUserData = NULL;
#endif
    uint32_t expected = 0;
    bool paused = waitForSaveStatePause("load", &expected, NULL);
    SaveStateSlotInfo info = saveStateSlotInfo(g_currentAppPath, slot);
    bool runtimeCountMatches = paused && validateSaveStateRuntimeCount(info, expected, &error);
    std::chrono::steady_clock::time_point readBegin = std::chrono::steady_clock::now();
    bool readOk = runtimeCountMatches &&
        saveStateReadSlot(g_currentAppPath, slot, &state, &error,
            progressCallback, progressUserData);
    uint32_t readMs = (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - readBegin).count();
#ifdef _WIN32
    destroySaveStateProgressDialog(&progressDialog);
#endif
    printf("frontend: load state slot %d decompress %s in %u ms\n",
        slot, readOk ? "done" : "failed", readMs);
    std::chrono::steady_clock::time_point restoreBegin = std::chrono::steady_clock::now();
    bool restoreOk = readOk && emulatorRuntimeRestoreState(state);
    uint32_t restoreMs = (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - restoreBegin).count();
    if (readOk)
    {
        printf("frontend: load state slot %d restore %s in %u ms\n",
            slot, restoreOk ? "done" : "failed", restoreMs);
    }
    bool ok = readOk && restoreOk;
    if (!ok && error.empty())
    {
        error = paused ? "runtime state is not available" : "runtime did not pause in time";
    }
    if (ok)
    {
        frontendResetInputAfterStateRestore();
        pauseWhileLoading.dismiss();
        frontendResetInputAfterStateRestore();
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }

    printf("frontend: load state slot %d %s%s%s\n", slot, ok ? "loaded" : "failed",
        error.empty() ? "" : ": ", error.c_str());
    if (errorOut)
    {
        *errorOut = error;
    }
    frontendMenuRefresh();
    return ok;
}

static bool validateSaveStateSlotContext(int slot, std::string* errorOut)
{
    if (slot < 1 || slot > kSaveStateSlotCount || g_currentAppPath.empty() || !g_gameRunning)
    {
        if (errorOut)
        {
            *errorOut = "no running game";
        }
        return false;
    }
    return true;
}

static bool automationSaveStateProgressEnabled(void)
{
    const char* value = getenv("DINGOO_PIE_AUTOTEST_STATE_PROGRESS");
    return value && value[0] && strcmp(value, "0") != 0;
}

static void showSaveStateFailureMessage(UiTextId messageId, UiTextId titleId,
    int slot, const std::string& error)
{
#ifdef _WIN32
    std::wstring message = saveStateMessage(messageId,
        makeSaveStateDialogSlot(slot, true, 0), error);
    showModalMessageBox(message.c_str(), uiText(titleId), MB_OK | MB_ICONERROR);
#else
    (void)messageId;
    (void)titleId;
    (void)slot;
    (void)error;
#endif
}

static bool confirmAndSaveStateSlot(int slot, std::string* errorOut)
{
    if (!validateSaveStateSlotContext(slot, errorOut))
    {
        return false;
    }

    SaveStateSlotInfo info = saveStateSlotInfo(g_currentAppPath, slot);
    SaveStateDialogSlot dialogSlot = makeSaveStateDialogSlot(slot, info);
    UiTextId confirmText = info.exists ?
        TXT_DIALOG_STATE_CONFIRM_OVERWRITE :
        TXT_DIALOG_STATE_CONFIRM_SAVE;
    if (!confirmSaveStateAction(confirmText, dialogSlot,
        uiText(TXT_DIALOG_STATE_SAVE_TITLE), info.exists))
    {
        if (errorOut)
        {
            errorOut->clear();
        }
        return false;
    }

    bool ok = saveStateSlotNow(slot, errorOut, true);

    if (!ok)
    {
        std::string error = errorOut ? *errorOut : "";
        showSaveStateFailureMessage(TXT_DIALOG_STATE_SAVE_FAILED,
            TXT_DIALOG_STATE_SAVE_TITLE, slot, error);
    }
    return ok;
}

static bool handleSaveSlotCommand(unsigned int commandId)
{
    if (commandId < MENU_SAVE_SLOT_BASE ||
        commandId >= MENU_SAVE_SLOT_BASE + kSaveStateSlotCount)
    {
        return false;
    }

    int slot = (int)(commandId - MENU_SAVE_SLOT_BASE) + 1;
    std::string error;
    confirmAndSaveStateSlot(slot, &error);
    return true;
}

bool frontendMenuSaveStateSlotForAutomation(int slot)
{
    if (!validateSaveStateSlotContext(slot, NULL))
    {
        return false;
    }

    return saveStateSlotNow(slot, NULL, automationSaveStateProgressEnabled());
}

static bool confirmAndLoadStateSlot(int slot, std::string* errorOut)
{
    if (!validateSaveStateSlotContext(slot, errorOut))
    {
        return false;
    }

    SaveStateSlotInfo info = saveStateSlotInfo(g_currentAppPath, slot);
    if (!info.exists)
    {
#ifdef _WIN32
        std::wstring message = saveStateMessage(TXT_DIALOG_STATE_EMPTY,
            makeSaveStateDialogSlot(slot, false, 0), "");
        showModalMessageBox(message.c_str(), uiText(TXT_DIALOG_STATE_LOAD_TITLE),
            MB_OK | MB_ICONINFORMATION);
#endif
        if (errorOut)
        {
            *errorOut = "state slot is empty";
        }
        return false;
    }

    SaveStateDialogSlot dialogSlot = makeSaveStateDialogSlot(slot, info);
    if (!confirmSaveStateAction(TXT_DIALOG_STATE_CONFIRM_LOAD, dialogSlot,
        uiText(TXT_DIALOG_STATE_LOAD_TITLE), false))
    {
        if (errorOut)
        {
            errorOut->clear();
        }
        return false;
    }

    bool ok = loadStateSlotNow(slot, errorOut, true);

    if (!ok)
    {
        std::string error = errorOut ? *errorOut : "";
        showSaveStateFailureMessage(TXT_DIALOG_STATE_LOAD_FAILED,
            TXT_DIALOG_STATE_LOAD_TITLE, slot, error);
    }
    return ok;
}

static bool handleLoadSlotCommand(unsigned int commandId)
{
    if (commandId < MENU_LOAD_SLOT_BASE ||
        commandId >= MENU_LOAD_SLOT_BASE + kSaveStateSlotCount)
    {
        return false;
    }

    int slot = (int)(commandId - MENU_LOAD_SLOT_BASE) + 1;
    std::string error;
    confirmAndLoadStateSlot(slot, &error);
    return true;
}

bool frontendMenuLoadStateSlotForAutomation(int slot)
{
    if (!validateSaveStateSlotContext(slot, NULL))
    {
        return false;
    }

    SaveStateSlotInfo info = saveStateSlotInfo(g_currentAppPath, slot);
    if (!info.exists)
    {
        printf("frontend: load state slot %d failed: state slot is empty\n", slot);
        return false;
    }

    return loadStateSlotNow(slot, NULL, automationSaveStateProgressEnabled());
}

#ifdef _WIN32
static bool saveStateManagerSaveSlot(int slot, std::string* errorOut, void* userData)
{
    (void)userData;
    return confirmAndSaveStateSlot(slot, errorOut);
}

static bool saveStateManagerLoadSlot(int slot, std::string* errorOut, void* userData)
{
    (void)userData;
    return confirmAndLoadStateSlot(slot, errorOut);
}

static void saveStateManagerChanged(void* userData)
{
    (void)userData;
    frontendMenuRefresh();
}

static void openSaveStateManagerWindow(void)
{
    if (g_currentAppPath.empty())
    {
        return;
    }

    SaveStateManagerCallbacks callbacks = {};
    callbacks.saveSlot = saveStateManagerSaveSlot;
    callbacks.loadSlot = saveStateManagerLoadSlot;
    callbacks.changed = saveStateManagerChanged;
    callbacks.userData = NULL;
    saveStateManagerOpenWindow(g_menuWindow, g_menuSettings->uiLanguage,
        g_currentAppPath, g_gameRunning, callbacks);
}
#endif

static void clearRecentAppMenu(void)
{
    if (emulatorClearRecentApps(g_menuSettings))
    {
        if (emulatorSaveSettings(*g_menuSettings))
        {
            suppressCurrentRunRecentAppSave();
            rebuildMenu();
        }
        else
        {
            printf("frontend: failed to save cleared recent app list\n");
        }
    }
}

bool frontendMenuHandleCommand(unsigned int commandId)
{
    if (!g_menuSettings)
    {
        return false;
    }

    if (handleRecentAppCommand(commandId))
    {
        return true;
    }
    if (handleCheatEntryCommand(commandId))
    {
        return true;
    }
    if (handleSaveSlotCommand(commandId))
    {
        return true;
    }
    if (handleLoadSlotCommand(commandId))
    {
        return true;
    }

    switch (commandId)
    {
    case MENU_FILE_OPEN:
    {
        ScopedFrontendModalPause pauseWhileChoosing(true);
        std::string appPath = platformSelectAppPathLocalized(uiText(TXT_DIALOG_APP_TITLE), uiText(TXT_DIALOG_APP_FILTER));
        if (!appPath.empty())
        {
            printf("frontend: selected app queued as recent app after normal exit: %s\n", appPath.c_str());
            if (frontendMenuRequestOpenApp(appPath))
            {
                pauseWhileChoosing.dismiss();
            }
        }
        return true;
    }
    case MENU_FILE_RECENT_CLEAR:
        clearRecentAppMenu();
        return true;
    case MENU_FILE_RESTART:
#ifdef _WIN32
        if (!g_currentAppPath.empty())
        {
            printf("frontend: restarting current app: %s\n", g_currentAppPath.c_str());
            requestRelaunchAfterExit(g_currentAppPath);
        }
#endif
        return true;
    case MENU_FILE_PAUSE_RESUME:
        if (!g_currentAppPath.empty() && g_gameRunning)
        {
            frontendToggleGamePaused();
        }
        return true;
    case MENU_FILE_SAVE_SCREENSHOT:
#ifdef _WIN32
        if (!g_currentAppPath.empty() && g_gameRunning)
        {
            saveScreenshotWithDialog();
        }
#endif
        return true;
    case MENU_FILE_SAVE_STATE_MANAGER:
#ifdef _WIN32
        openSaveStateManagerWindow();
#endif
        return true;
    case MENU_FILE_EXIT:
        frontendRequestQuit();
        return true;
    case MENU_VIDEO_SCALE_1X:
    case MENU_VIDEO_SCALE_2X:
    case MENU_VIDEO_SCALE_3X:
        g_menuSettings->windowScale = (int)(commandId - MENU_VIDEO_SCALE_1X + 1);
        g_menuSettings->fullscreen = false;
        applyAndSaveVideoSettings();
        printf("frontend: window scale setting applied as %dx\n",
            g_menuSettings->windowScale);
        frontendMenuRefresh();
        return true;
    case MENU_VIDEO_FULLSCREEN:
        g_menuSettings->fullscreen = !g_menuSettings->fullscreen;
        applyAndSaveVideoSettings();
        printf("frontend: fullscreen setting applied as %u\n",
            g_menuSettings->fullscreen ? 1u : 0u);
        frontendMenuRefresh();
        return true;
    case MENU_VIDEO_AA_OFF:
        g_menuSettings->antiAliasing = ANTI_ALIASING_OFF;
        frontendApplyVideoSettings(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        frontendMenuRefresh();
        return true;
    case MENU_VIDEO_AA_LOW:
        g_menuSettings->antiAliasing = ANTI_ALIASING_LOW;
        frontendApplyVideoSettings(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        frontendMenuRefresh();
        return true;
    case MENU_VIDEO_AA_CLEAR:
        g_menuSettings->antiAliasing = ANTI_ALIASING_CLEAR;
        frontendApplyVideoSettings(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        frontendMenuRefresh();
        return true;
    case MENU_VIDEO_EFFECT_NORMAL:
        g_menuSettings->colorEffect = COLOR_EFFECT_NORMAL;
        frontendApplyVideoSettings(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        frontendMenuRefresh();
        return true;
    case MENU_VIDEO_EFFECT_GRAYSCALE:
        g_menuSettings->colorEffect = COLOR_EFFECT_GRAYSCALE;
        frontendApplyVideoSettings(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        frontendMenuRefresh();
        return true;
    case MENU_VIDEO_EFFECT_INVERT:
        g_menuSettings->colorEffect = COLOR_EFFECT_INVERT;
        frontendApplyVideoSettings(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        frontendMenuRefresh();
        return true;
    case MENU_VIDEO_EFFECT_SOFT_BLUR:
        g_menuSettings->colorEffect = COLOR_EFFECT_SOFT_BLUR;
        frontendApplyVideoSettings(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        frontendMenuRefresh();
        return true;
    case MENU_VIDEO_EFFECT_SHARPEN:
        g_menuSettings->colorEffect = COLOR_EFFECT_SHARPEN;
        frontendApplyVideoSettings(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        frontendMenuRefresh();
        return true;
    case MENU_VIDEO_EFFECT_VIVID:
        g_menuSettings->colorEffect = COLOR_EFFECT_VIVID;
        frontendApplyVideoSettings(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        frontendMenuRefresh();
        return true;
    case MENU_VIDEO_EFFECT_SEPIA:
        g_menuSettings->colorEffect = COLOR_EFFECT_SEPIA;
        frontendApplyVideoSettings(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        frontendMenuRefresh();
        return true;
    case MENU_VIDEO_EFFECT_PIXEL_GRID:
        g_menuSettings->colorEffect = COLOR_EFFECT_PIXEL_GRID;
        frontendApplyVideoSettings(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        frontendMenuRefresh();
        return true;
    case MENU_VIDEO_EFFECT_LCD_SCANLINE:
        g_menuSettings->colorEffect = COLOR_EFFECT_LCD_SCANLINE;
        frontendApplyVideoSettings(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        frontendMenuRefresh();
        return true;
    case MENU_VIDEO_EFFECT_LIGHT_CRT:
        g_menuSettings->colorEffect = COLOR_EFFECT_LIGHT_CRT;
        frontendApplyVideoSettings(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        frontendMenuRefresh();
        return true;
    case MENU_VIDEO_BRIGHTNESS_50:
    case MENU_VIDEO_BRIGHTNESS_75:
    case MENU_VIDEO_BRIGHTNESS_90:
    case MENU_VIDEO_BRIGHTNESS_100:
    case MENU_VIDEO_BRIGHTNESS_110:
    case MENU_VIDEO_BRIGHTNESS_125:
    case MENU_VIDEO_BRIGHTNESS_150:
        g_menuSettings->brightnessPercent = percentForVideoCommand(commandId, MENU_VIDEO_BRIGHTNESS_50);
        frontendApplyVideoSettings(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        frontendMenuRefresh();
        return true;
    case MENU_VIDEO_CONTRAST_50:
    case MENU_VIDEO_CONTRAST_75:
    case MENU_VIDEO_CONTRAST_90:
    case MENU_VIDEO_CONTRAST_100:
    case MENU_VIDEO_CONTRAST_110:
    case MENU_VIDEO_CONTRAST_125:
    case MENU_VIDEO_CONTRAST_150:
        g_menuSettings->contrastPercent = percentForVideoCommand(commandId, MENU_VIDEO_CONTRAST_50);
        frontendApplyVideoSettings(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        frontendMenuRefresh();
        return true;
    case MENU_VIDEO_GAMMA_50:
    case MENU_VIDEO_GAMMA_75:
    case MENU_VIDEO_GAMMA_90:
    case MENU_VIDEO_GAMMA_100:
    case MENU_VIDEO_GAMMA_110:
    case MENU_VIDEO_GAMMA_125:
    case MENU_VIDEO_GAMMA_150:
        g_menuSettings->gammaPercent = percentForVideoCommand(commandId, MENU_VIDEO_GAMMA_50);
        frontendApplyVideoSettings(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        frontendMenuRefresh();
        return true;
    case MENU_VIDEO_SATURATION_50:
    case MENU_VIDEO_SATURATION_75:
    case MENU_VIDEO_SATURATION_90:
    case MENU_VIDEO_SATURATION_100:
    case MENU_VIDEO_SATURATION_110:
    case MENU_VIDEO_SATURATION_125:
    case MENU_VIDEO_SATURATION_150:
        g_menuSettings->saturationPercent = percentForVideoCommand(commandId, MENU_VIDEO_SATURATION_50);
        frontendApplyVideoSettings(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        frontendMenuRefresh();
        return true;
    case MENU_VIDEO_MINIMIZED_NORMAL:
    case MENU_VIDEO_MINIMIZED_PAUSE:
    case MENU_VIDEO_MINIMIZED_THROTTLE:
    {
        const MinimizedBehaviorPreset* preset = minimizedBehaviorPresetForCommand(commandId);
        if (preset)
        {
            g_menuSettings->minimizedBehavior = preset->behavior;
            emulatorSaveSettings(*g_menuSettings);
            frontendMenuRefresh();
        }
        return true;
    }
    case MENU_VIDEO_PORTRAIT:
        g_menuSettings->portraitMode = !g_menuSettings->portraitMode;
        applyAndSaveVideoSettings();
        printf("frontend: portrait mode setting applied as %u\n",
            g_menuSettings->portraitMode ? 1u : 0u);
        frontendMenuRefresh();
        return true;
    case MENU_VIDEO_SHOW_FPS:
        g_menuSettings->showFps = !g_menuSettings->showFps;
        applyAndSaveVideoSettings();
        printf("frontend: FPS overlay setting applied as %u\n",
            g_menuSettings->showFps ? 1u : 0u);
        frontendMenuRefresh();
        return true;
    case MENU_AUDIO_VOLUME_0:
    case MENU_AUDIO_VOLUME_25:
    case MENU_AUDIO_VOLUME_50:
    case MENU_AUDIO_VOLUME_75:
    case MENU_AUDIO_VOLUME_100:
    case MENU_AUDIO_VOLUME_125:
    case MENU_AUDIO_VOLUME_150:
        g_menuSettings->audioVolumePercent = percentForAudioVolumeCommand(commandId);
        frontendApplyAudioSettings(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        printf("frontend: master volume setting saved as %d%%\n", g_menuSettings->audioVolumePercent);
        frontendMenuRefresh();
        return true;
    case MENU_AUDIO_BUFFER_512:
    case MENU_AUDIO_BUFFER_1024:
    case MENU_AUDIO_BUFFER_2048:
    case MENU_AUDIO_BUFFER_4096:
    case MENU_AUDIO_BUFFER_8192:
        if (commandId == MENU_AUDIO_BUFFER_512)
        {
            g_menuSettings->audioBufferSamples = 512;
        }
        else if (commandId == MENU_AUDIO_BUFFER_1024)
        {
            g_menuSettings->audioBufferSamples = 1024;
        }
        else if (commandId == MENU_AUDIO_BUFFER_2048)
        {
            g_menuSettings->audioBufferSamples = 2048;
        }
        else if (commandId == MENU_AUDIO_BUFFER_4096)
        {
            g_menuSettings->audioBufferSamples = 4096;
        }
        else
        {
            g_menuSettings->audioBufferSamples = 8192;
        }
        frontendApplyAudioSettings(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        printf("frontend: audio buffer setting saved as %d samples\n", g_menuSettings->audioBufferSamples);
        frontendMenuRefresh();
        return true;
    case MENU_AUDIO_EFFECT_OFF:
    case MENU_AUDIO_EFFECT_SOFT:
    case MENU_AUDIO_EFFECT_CLEAR:
    case MENU_AUDIO_EFFECT_BASS_BOOST:
    case MENU_AUDIO_EFFECT_MONO:
    {
        const AudioEffectPreset* preset = audioEffectPresetForCommand(commandId);
        if (preset)
        {
            g_menuSettings->audioEffect = preset->effect;
            frontendApplyAudioSettings(*g_menuSettings);
            emulatorSaveSettings(*g_menuSettings);
            printf("frontend: audio effect setting saved as %s\n",
                emulatorAudioEffectName(g_menuSettings->audioEffect));
            frontendMenuRefresh();
        }
        return true;
    }
    case MENU_AUDIO_DISABLE:
        g_menuSettings->audioDisabled = !g_menuSettings->audioDisabled;
        emulatorApplyRuntimeSettings(*g_menuSettings);
        frontendApplyAudioSettings(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        printf("frontend: disable-audio setting applied as %u\n",
            g_menuSettings->audioDisabled ? 1u : 0u);
        frontendMenuRefresh();
        return true;
    case MENU_INPUT_DISABLE_IME:
        g_menuSettings->disableIme = !g_menuSettings->disableIme;
        frontendApplyInputSettings(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        printf("frontend: disable IME setting saved as %u\n",
            g_menuSettings->disableIme ? 1u : 0u);
        frontendMenuRefresh();
        return true;
    case MENU_INPUT_SHOW_VIRTUAL_CONTROLS:
        g_menuSettings->showVirtualControls = !g_menuSettings->showVirtualControls;
        emulatorSaveSettings(*g_menuSettings);
        printf("frontend: virtual controls overlay %s\n",
            g_menuSettings->showVirtualControls ? "enabled" : "disabled");
        frontendMenuRefresh();
        return true;
    case MENU_INPUT_MAPPING_WINDOW:
        frontendOpenInputMappingWindow();
        return true;
    case MENU_SETTINGS_BACKEND_AUTO:
        return applyBackendSetting("", "auto");
    case MENU_SETTINGS_BACKEND_IRJIT:
        return applyBackendSetting("ppsspp_irjit", "ppsspp_irjit");
    case MENU_SETTINGS_BACKEND_INTERPRETER:
        return applyBackendSetting("interpreter", "interpreter");
    case MENU_SETTINGS_CPU_CLOCK_AUTO:
    case MENU_SETTINGS_CPU_CLOCK_200:
    case MENU_SETTINGS_CPU_CLOCK_336:
    case MENU_SETTINGS_CPU_CLOCK_370:
    case MENU_SETTINGS_CPU_CLOCK_400:
    case MENU_SETTINGS_CPU_CLOCK_430:
        if (commandId == MENU_SETTINGS_CPU_CLOCK_AUTO)
        {
            g_menuSettings->cpuClockHz = "";
        }
        else if (commandId == MENU_SETTINGS_CPU_CLOCK_200)
        {
            g_menuSettings->cpuClockHz = "200000000";
        }
        else if (commandId == MENU_SETTINGS_CPU_CLOCK_336)
        {
            g_menuSettings->cpuClockHz = "336000000";
        }
        else if (commandId == MENU_SETTINGS_CPU_CLOCK_370)
        {
            g_menuSettings->cpuClockHz = "370000000";
        }
        else if (commandId == MENU_SETTINGS_CPU_CLOCK_400)
        {
            g_menuSettings->cpuClockHz = "400000000";
        }
        else
        {
            g_menuSettings->cpuClockHz = "430000000";
        }
        applyAndSaveRuntimeSettings();
        printf("frontend: CPU clock setting applied as '%s'\n",
            g_menuSettings->cpuClockHz.empty() ? "auto" : g_menuSettings->cpuClockHz.c_str());
        frontendMenuRefresh();
        return true;
    case MENU_SETTINGS_RUNTIME_SPEED_AUTO:
    case MENU_SETTINGS_RUNTIME_SPEED_100:
    case MENU_SETTINGS_RUNTIME_SPEED_095:
    case MENU_SETTINGS_RUNTIME_SPEED_090:
    case MENU_SETTINGS_RUNTIME_SPEED_085:
    case MENU_SETTINGS_RUNTIME_SPEED_080:
    case MENU_SETTINGS_RUNTIME_SPEED_075:
    case MENU_SETTINGS_RUNTIME_SPEED_070:
    case MENU_SETTINGS_RUNTIME_SPEED_065:
    case MENU_SETTINGS_RUNTIME_SPEED_060:
    case MENU_SETTINGS_RUNTIME_SPEED_055:
    case MENU_SETTINGS_RUNTIME_SPEED_050:
    case MENU_SETTINGS_RUNTIME_SPEED_045:
    case MENU_SETTINGS_RUNTIME_SPEED_040:
    case MENU_SETTINGS_RUNTIME_SPEED_035:
    case MENU_SETTINGS_RUNTIME_SPEED_030:
    case MENU_SETTINGS_RUNTIME_SPEED_025:
    case MENU_SETTINGS_RUNTIME_SPEED_020:
    {
        const ScalePreset* speedPreset = runtimeSpeedPresetForCommand(commandId);
        g_menuSettings->runtimeSpeedScale = speedPreset ? speedPreset->iniValue : "";
        applyAndSaveRuntimeSettings();
        printf("frontend: runtime speed scale setting applied as '%s'\n",
            g_menuSettings->runtimeSpeedScale.empty() ? "auto" : g_menuSettings->runtimeSpeedScale.c_str());
        frontendMenuRefresh();
        return true;
    }
    case MENU_SETTINGS_DELAY_SCALE_AUTO:
    case MENU_SETTINGS_DELAY_SCALE_100:
    case MENU_SETTINGS_DELAY_SCALE_095:
    case MENU_SETTINGS_DELAY_SCALE_090:
    case MENU_SETTINGS_DELAY_SCALE_085:
    case MENU_SETTINGS_DELAY_SCALE_080:
    case MENU_SETTINGS_DELAY_SCALE_075:
    case MENU_SETTINGS_DELAY_SCALE_070:
    case MENU_SETTINGS_DELAY_SCALE_065:
    case MENU_SETTINGS_DELAY_SCALE_060:
    case MENU_SETTINGS_DELAY_SCALE_055:
    case MENU_SETTINGS_DELAY_SCALE_050:
    case MENU_SETTINGS_DELAY_SCALE_045:
    case MENU_SETTINGS_DELAY_SCALE_040:
    case MENU_SETTINGS_DELAY_SCALE_035:
    case MENU_SETTINGS_DELAY_SCALE_030:
    case MENU_SETTINGS_DELAY_SCALE_025:
    case MENU_SETTINGS_DELAY_SCALE_020:
    {
        const ScalePreset* delayPreset = delayScalePresetForCommand(commandId);
        g_menuSettings->ostimeDlyScale = delayPreset ? delayPreset->iniValue : "";
        applyAndSaveRuntimeSettings();
        printf("frontend: OSTimeDly scale setting applied as '%s'\n",
            g_menuSettings->ostimeDlyScale.empty() ? "auto" : g_menuSettings->ostimeDlyScale.c_str());
        frontendMenuRefresh();
        return true;
    }
    case MENU_SETTINGS_ENABLE_CHEATS:
    {
        CheatRuntimeStatus cheatStatus = cheatRuntimeGetStatus();
        bool enableCheats = !g_menuSettings->cheatsEnabled;
        g_menuSettings->cheatsEnabled = enableCheats;
        cheatRuntimeSetEnabled(enableCheats);
        emulatorApplySettingsToEnvironment(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        if (enableCheats)
        {
            cheatRuntimeApplyNow();
        }
        printf("frontend: cheats setting saved as %u active=%u available=%u\n",
            g_menuSettings->cheatsEnabled ? 1u : 0u,
            cheatRuntimeEnabled() ? 1u : 0u,
            cheatStatus.available ? 1u : 0u);
        frontendMenuRefresh();
        return true;
    }
    case MENU_SETTINGS_CHEAT_MANAGER:
#ifdef _WIN32
        if (g_gameRunning)
        {
            cheatManagerOpenWindow(g_menuWindow, g_menuSettings->uiLanguage,
                g_menuSettings, g_currentAppPath);
        }
#endif
        return true;
    case MENU_SETTINGS_LANGUAGE_CHINESE:
    case MENU_SETTINGS_LANGUAGE_ENGLISH:
        g_menuSettings->uiLanguage = commandId == MENU_SETTINGS_LANGUAGE_CHINESE ?
            UI_LANGUAGE_CHINESE : UI_LANGUAGE_ENGLISH;
        emulatorSaveSettings(*g_menuSettings);
        rebuildMenu();
        return true;
    case MENU_SETTINGS_RESET:
    {
        std::string oldBackendName = g_menuSettings->backendName;
        *g_menuSettings = emulatorDefaultSettings();
        emulatorSaveSettings(*g_menuSettings);
        emulatorApplyRuntimeSettings(*g_menuSettings);
        cheatRuntimeSetEnabled(g_menuSettings->cheatsEnabled);
        debugConsoleClose();
        frontendApplyVideoSettings(*g_menuSettings);
        frontendApplyAudioSettings(*g_menuSettings);
        frontendApplyInputSettings(*g_menuSettings);
        rebuildMenu();
#ifdef _WIN32
        if (oldBackendName != g_menuSettings->backendName)
        {
            printf("frontend: backend reset from %s to %s, relaunching emulator\n",
                oldBackendName.c_str(),
                g_menuSettings->backendName.c_str());
            relaunchEmulatorAfterExit();
        }
#endif
        return true;
    }
    case MENU_DEBUG_SHOW_CONSOLE:
        g_menuSettings->showDebugConsole = !g_menuSettings->showDebugConsole;
        if (g_menuSettings->showDebugConsole)
        {
            if (!debugConsoleOpen())
            {
                g_menuSettings->showDebugConsole = false;
            }
        }
        else
        {
            debugConsoleClose();
        }
        emulatorSaveSettings(*g_menuSettings);
        printf("frontend: debug console setting applied as %u\n",
            g_menuSettings->showDebugConsole ? 1u : 0u);
        frontendMenuRefresh();
        return true;
    case MENU_DEBUG_PROFILE:
        g_menuSettings->debugProfile =
            !(g_menuSettings->debugProfile || runtimeLogProfileEnabled());
        if (!g_menuSettings->debugProfile)
        {
            runtimeLogSetExternalProfileEnabled(false);
        }
        if (g_menuSettings->debugProfile)
        {
            if (!debugLogOpen())
            {
                g_menuSettings->debugProfile = false;
            }
        }
        emulatorApplyRuntimeSettings(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        printf("frontend: performance log setting applied as %u\n",
            g_menuSettings->debugProfile ? 1u : 0u);
        frontendMenuRefresh();
        return true;
    case MENU_DEBUG_OPEN_LOG:
#ifdef _WIN32
        openTextFileNearExe(debugLogFileNameWide());
#endif
        return true;
    case MENU_DEBUG_RESOURCE_MONITOR:
        g_menuSettings->resourceMonitorAutoOpen = !g_menuSettings->resourceMonitorAutoOpen;
        g_resourceMonitorAutoOpenedForRun = false;
        emulatorSaveSettings(*g_menuSettings);
        printf("frontend: resource monitor auto-open setting applied as %u\n",
            g_menuSettings->resourceMonitorAutoOpen ? 1u : 0u);
        frontendMenuRefresh();
        openResourceMonitorForCurrentRun();
        return true;
    case MENU_DEBUG_MEMORY_SEARCHER:
#ifdef _WIN32
        if (g_gameRunning)
        {
            frontendOpenMemorySearcherWindow();
        }
#endif
        return true;
    case MENU_DEBUG_DEBUGGER:
#ifdef _WIN32
        if (g_gameRunning)
        {
            frontendOpenDebuggerWindow();
        }
#endif
        return true;
    case MENU_HELP_ABOUT:
#ifdef _WIN32
        showModalMessageBox(
            uiText(TXT_ABOUT_BODY),
            uiText(TXT_ABOUT_TITLE),
            MB_OK | MB_ICONINFORMATION);
#endif
        return true;
    default:
        return false;
    }
}
