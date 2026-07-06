#ifndef DINGOO_PIE_INPUT_CONTROLS_H
#define DINGOO_PIE_INPUT_CONTROLS_H

#include "input_state.h"

#include <SDL2/SDL.h>
#include <string>

#define CONTROL_POWER         7 /*!< Dingoo A320 power slider; HOLD is not tracked separately. */

#define CONTROL_BUTTON_A      31 /*!< Dingoo A320 A / Gemei X760+ circle. */
#define CONTROL_BUTTON_B      21 /*!< Dingoo A320 B / Gemei X760+ square. */
#define CONTROL_BUTTON_X      16 /*!< Dingoo A320 X / Gemei X760+ triangle. */
#define CONTROL_BUTTON_Y      6  /*!< Dingoo A320 Y / Gemei X760+ X. */
#define CONTROL_BUTTON_START  11 /*!< Dingoo A320 START; synthetic only for Gemei X760+ mappings. */
#define CONTROL_BUTTON_SELECT 10 /*!< Dingoo A320 SELECT; synthetic only for Gemei X760+ mappings. */

#define CONTROL_TRIGGER_LEFT  8  /*!< Dingoo A320 left shoulder; synthetic only for Gemei X760+ mappings. */
#define CONTROL_TRIGGER_RIGHT 29 /*!< Dingoo A320 right shoulder; synthetic only for Gemei X760+ mappings. */

#define CONTROL_DPAD_UP       20 /*!< Directional pad up. */
#define CONTROL_DPAD_DOWN     27 /*!< Directional pad down. */
#define CONTROL_DPAD_LEFT     28 /*!< Directional pad left. */
#define CONTROL_DPAD_RIGHT    18 /*!< Directional pad right. */

void inputClearControls(void);
void inputClearSyntheticControls(void);
void inputResetTransientControls(void);
void inputSetSyntheticControl(uint32_t controlBit, bool pressed);
void inputHandleHostScancode(SDL_Scancode scancode, bool pressed);
void inputHandleHostVirtualKey(int virtualKey, bool pressed);
void inputPollKeyboardState(void);
void inputApplyKeyboardMapping(const std::string& mapping);
std::string inputCurrentKeyboardMapping(void);
std::string inputKeyboardSourceForControl(uint32_t controlBit);
bool inputSetKeyboardMappingForControl(uint32_t controlBit, SDL_Scancode scancode);
void inputResetKeyboardMapping(void);

#endif
