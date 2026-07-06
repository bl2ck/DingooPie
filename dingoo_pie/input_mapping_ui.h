#ifndef DINGOO_PIE_INPUT_MAPPING_UI_H
#define DINGOO_PIE_INPUT_MAPPING_UI_H

#include "emulator_settings.h"

#include <SDL2/SDL.h>
#include <stdint.h>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

typedef std::string (*InputMappingUiSourceForControl)(uint32_t controlBit);
typedef void (*InputMappingUiControlAction)(uint32_t controlBit);
typedef void (*InputMappingUiAction)(void);

struct InputMappingUiCallbacks
{
    InputMappingUiSourceForControl controllerSourceForControl;
    InputMappingUiControlAction beginControllerMapping;
    InputMappingUiAction resetControllerMapping;
    InputMappingUiAction settingsChanged;
};

void inputMappingUiOpenWindow(
    HWND owner,
    UiLanguage language,
    EmulatorSettings* settings,
    const InputMappingUiCallbacks& callbacks);
void inputMappingUiRefresh(void);
void inputMappingUiSetStatus(const wchar_t* text);
bool inputMappingUiKeyboardCapturePending(void);
void inputMappingUiHandleKeyboardScancode(SDL_Scancode scancode);
#endif

#endif
