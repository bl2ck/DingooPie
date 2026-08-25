#ifndef DINGOO_PIE_CC_MEMORY_CC_MEMORY_LAYOUT_H
#define DINGOO_PIE_CC_MEMORY_CC_MEMORY_LAYOUT_H

#include <stdint.h>

static const uint32_t kCcRetailProgramOrigin = 0x10100000u;
static const uint32_t kCcHomebrewProgramOrigin = 0x13800000u;

static inline bool ccPackageUsesRetailLayout(uint32_t origin)
{
    return origin == kCcRetailProgramOrigin;
}

static inline bool ccPackageUsesHomebrewLayout(uint32_t origin)
{
    return origin == kCcHomebrewProgramOrigin;
}

#endif
