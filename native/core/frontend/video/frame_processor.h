#ifndef DINGOO_PIE_FRONTEND_VIDEO_FRAME_PROCESSOR_H
#define DINGOO_PIE_FRONTEND_VIDEO_FRAME_PROCESSOR_H

#include "config/settings/emulator_settings.h"

#include <stddef.h>
#include <stdint.h>

uint16_t frontendBlendRgb565WithBlack(uint16_t pixel, uint32_t blackAlpha);
uint16_t* frontendProcessFramePixels(
    uint16_t* sourcePixels,
    uint16_t* effectPixels,
    uint16_t* antiAliasPixels,
    size_t pixelCount,
    const EmulatorSettings* settings);

#endif
