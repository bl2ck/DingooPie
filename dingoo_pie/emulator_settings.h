#ifndef DINGOO_PIE_EMULATOR_SETTINGS_H
#define DINGOO_PIE_EMULATOR_SETTINGS_H

#include <string>

enum VideoFilterMode
{
    VIDEO_FILTER_NEAREST = 0,
    VIDEO_FILTER_LINEAR = 1
};

enum ColorEffectMode
{
    COLOR_EFFECT_NORMAL = 0,
    COLOR_EFFECT_GRAYSCALE = 1,
    COLOR_EFFECT_INVERT = 2,
    COLOR_EFFECT_INVERT_GRAYSCALE = 3,
    COLOR_EFFECT_SEPIA = 4,
    COLOR_EFFECT_AMBER = 5,
    COLOR_EFFECT_SHARPEN = 6,
    COLOR_EFFECT_SOFT_BLUR = 7,
    COLOR_EFFECT_LCD_SCANLINE = 8
};

enum UiLanguage
{
    UI_LANGUAGE_ENGLISH = 0,
    UI_LANGUAGE_CHINESE = 1
};

struct EmulatorSettings
{
    std::string lastAppPath;

    int windowScale;
    bool fullscreen;
    VideoFilterMode videoFilter;
    ColorEffectMode colorEffect;
    int brightnessPercent;
    int contrastPercent;
    int saturationPercent;
    bool portraitMode;
    bool showFps;

    int audioVolumePercent;
    int audioBufferSamples;
    bool dropAudio;

    bool showVirtualControls;
    bool disableIme;

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
void emulatorTraceSettings(const char* reason, const EmulatorSettings& settings);
bool emulatorResetSettings(void);
void emulatorApplySettingsToEnvironment(const EmulatorSettings& settings);
void emulatorApplyRuntimeSettings(const EmulatorSettings& settings);
const char* emulatorVideoFilterName(VideoFilterMode mode);
const char* emulatorColorEffectName(ColorEffectMode mode);
const char* emulatorUiLanguageName(UiLanguage language);

#endif
