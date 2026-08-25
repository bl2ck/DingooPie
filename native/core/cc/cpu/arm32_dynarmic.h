#ifndef DINGOO_PIE_CC_CPU_ARM32_DYNARMIC_H
#define DINGOO_PIE_CC_CPU_ARM32_DYNARMIC_H

#include "cc/cpu/arm32_interpreter.h"

Arm32RunResult arm32RunDynarmic(Arm32State* state, const Arm32Bus* bus,
    uint32_t stopPc, uint64_t instructionLimit);
void arm32DynarmicReset();

#endif
