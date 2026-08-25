#ifndef DINGOO_PIE_CC_HLE_CC_INPUT_MAPPING_H
#define DINGOO_PIE_CC_HLE_CC_INPUT_MAPPING_H

#include "cc/memory/cc_memory_layout.h"

#include <stdint.h>

// Values are CC runtime source masks; keep them explicit when reordering.
enum CcInputSourceMask : uint32_t
{
    CC_INPUT_SOURCE_A = 0x80000000u,
    CC_INPUT_SOURCE_B = 0x00200000u,
    CC_INPUT_SOURCE_X = 0x00010000u,
    CC_INPUT_SOURCE_Y = 0x00000040u,
    CC_INPUT_SOURCE_START = 0x00000800u,
    CC_INPUT_SOURCE_SELECT = 0x00000400u,
    CC_INPUT_SOURCE_L = 0x00000100u,
    CC_INPUT_SOURCE_R = 0x20000000u,
    CC_INPUT_SOURCE_UP = 0x00100000u,
    CC_INPUT_SOURCE_DOWN = 0x08000000u,
    CC_INPUT_SOURCE_LEFT = 0x10000000u,
    CC_INPUT_SOURCE_RIGHT = 0x00040000u,
    CC_INPUT_SOURCE_POWER = 0x00000080u
};

static inline bool ccUsesRetailInputMapping(uint32_t packageOrigin)
{
    return ccPackageUsesRetailLayout(packageOrigin);
}

static inline uint32_t ccMapInputToRetailLayout(uint32_t input)
{
    uint32_t mapped = input & (CC_INPUT_SOURCE_POWER |
        CC_INPUT_SOURCE_RIGHT | CC_INPUT_SOURCE_UP | CC_INPUT_SOURCE_DOWN |
        CC_INPUT_SOURCE_LEFT | CC_INPUT_SOURCE_A);
    if (input & CC_INPUT_SOURCE_B) mapped |= 0x00001000u;
    if (input & CC_INPUT_SOURCE_X) mapped |= 0x00010000u;
    if (input & CC_INPUT_SOURCE_Y) mapped |= 0x20000000u;
    if (input & CC_INPUT_SOURCE_L) mapped |= 0x00020000u;
    if (input & CC_INPUT_SOURCE_R) mapped |= 0x40000000u;
    if (input & CC_INPUT_SOURCE_START) mapped |= 0x00008000u;
    if (input & CC_INPUT_SOURCE_SELECT) mapped |= 0x00800000u;
    return mapped;
}

static inline uint32_t ccMapInputToHomebrewLayout(uint32_t input)
{
    uint32_t mapped = input &
        (CC_INPUT_SOURCE_UP | CC_INPUT_SOURCE_DOWN | CC_INPUT_SOURCE_LEFT |
            CC_INPUT_SOURCE_RIGHT | CC_INPUT_SOURCE_A);
    if (input & CC_INPUT_SOURCE_B) mapped |= 0x00001000u;
    if (input & CC_INPUT_SOURCE_X) mapped |= 0x20000000u;
    if (input & CC_INPUT_SOURCE_Y) mapped |= 0x00010000u;
    if (input & CC_INPUT_SOURCE_SELECT) mapped |= 0x00004000u;
    if (input & CC_INPUT_SOURCE_START) mapped |= 0x00000080u;
    if (input & CC_INPUT_SOURCE_L) mapped |= 0x00020000u;
    if (input & CC_INPUT_SOURCE_R) mapped |= 0x40000000u;
    if (input & CC_INPUT_SOURCE_POWER) mapped |= 0x00000001u;
    return mapped;
}

#endif
