#ifndef DINGOO_PIE_FRONTEND_INPUT_KEYBOARD_MAPPING_H
#define DINGOO_PIE_FRONTEND_INPUT_KEYBOARD_MAPPING_H

#include "frontend/input/input_controls.h"

#include <stddef.h>
#include <string>

struct KeyboardBinding
{
    uint32_t controlBit;
    SDL_Scancode scancode;
    int virtualKey;
    const char* label;
};

bool keyboardMappingIsCurrent(const std::string& mapping);
bool keyboardMappingInitialized(void);
void keyboardMappingApply(const std::string& mapping);
std::string keyboardMappingCurrentSpec(void);
std::string keyboardMappingSourceForControl(uint32_t controlBit);
bool keyboardMappingSetForControl(uint32_t controlBit, SDL_Scancode scancode);
void keyboardMappingReset(void);
const KeyboardBinding* keyboardMappingBindings(size_t* outCount);

#endif
