#ifndef DINGOO_PIE_EMULATOR_SETTINGS_H
#define DINGOO_PIE_EMULATOR_SETTINGS_H

#include <string>
#include <vector>

enum
{
    EMULATOR_RECENT_APP_LIMIT = 10
};

enum AntiAliasingMode
{
    ANTI_ALIASING_OFF = 0,
    ANTI_ALIASING_LOW = 1,
    ANTI_ALIASING_CLEAR = 2
};

enum ColorEffectMode
{
    COLOR_EFFECT_NORMAL = 0,
    COLOR_EFFECT_GRAYSCALE = 1,
    COLOR_EFFECT_INVERT = 2,
    COLOR_EFFECT_SOFT_BLUR = 3,
    COLOR_EFFECT_SHARPEN = 4,
    COLOR_EFFECT_VIVID = 5,
    COLOR_EFFECT_SEPIA = 6,
    COLOR_EFFECT_PIXEL_GRID = 7,
    COLOR_EFFECT_LCD_SCANLINE = 8,
    COLOR_EFFECT_LIGHT_CRT = 9
};

enum UiLanguage
{
    UI_LANGUAGE_ENGLISH = 0,
    UI_LANGUAGE_CHINESE = 1
};

enum MinimizedBehavior
{
    MINIMIZED_BEHAVIOR_NORMAL = 0,
    MINIMIZED_BEHAVIOR_THROTTLE = 1,
    MINIMIZED_BEHAVIOR_PAUSE = 2
};

struct EmulatorSettings
{
    // lastAppPath preserves the original single-entry startup behavior; the
    // vector backs the visible Recent Games menu in newest-first order.
    std::string lastAppPath;
    std::vector<std::string> recentAppPaths;

    int windowScale;
    bool fullscreen;
    AntiAliasingMode antiAliasing;
    ColorEffectMode colorEffect;
    int brightnessPercent;
    int contrastPercent;
    int saturationPercent;
    MinimizedBehavior minimizedBehavior;
    bool portraitMode;
    bool showFps;

    int audioVolumePercent;
    int audioBufferSamples;
    bool dropAudio;

    bool disableIme;
    bool showVirtualControls;
    std::string keyboardMapping;
    std::string controllerMapping;

    std::string backendName;
    std::string cpuClockHz;
    std::string runtimeSpeedScale;
    std::string ostimeDlyScale;

    UiLanguage uiLanguage;

    bool showDebugConsole;
    bool debugProfile;
};

EmulatorSettings emulatorDefaultSettings(void);
std::string emulatorSettingsPath(void);
EmulatorSettings emulatorLoadSettings(void);
bool emulatorSaveSettings(const EmulatorSettings& settings);
bool emulatorRememberRecentApp(EmulatorSettings* settings, const std::string& appPath);
bool emulatorRemoveRecentApp(EmulatorSettings* settings, const std::string& appPath);
bool emulatorClearRecentApps(EmulatorSettings* settings);
void emulatorTraceSettings(const char* reason, const EmulatorSettings& settings);
bool emulatorResetSettings(void);
void emulatorApplySettingsToEnvironment(const EmulatorSettings& settings);
void emulatorApplyRuntimeSettings(const EmulatorSettings& settings);
const char* emulatorAntiAliasingName(AntiAliasingMode mode);
const char* emulatorColorEffectName(ColorEffectMode mode);
const char* emulatorUiLanguageName(UiLanguage language);
const char* emulatorMinimizedBehaviorName(MinimizedBehavior behavior);

#endif
