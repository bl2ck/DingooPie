#ifndef DINGOO_PIE_CC_CPU_ARM32_INTERPRETER_H
#define DINGOO_PIE_CC_CPU_ARM32_INTERPRETER_H

#include <stddef.h>
#include <stdint.h>

struct Arm32State
{
    uint32_t r[16];
    uint32_t cpsr;
    uint64_t instructions;
    uint32_t unsupportedInstruction;
    uint32_t unsupportedPc;
};

typedef bool (*Arm32ReadCallback)(void* userData, uint32_t address,
    void* output, size_t size);
typedef bool (*Arm32WriteCallback)(void* userData, uint32_t address,
    const void* input, size_t size);
typedef bool (*Arm32SvcCallback)(void* userData, Arm32State* state,
    uint32_t immediate);

struct Arm32InstructionCacheEntry
{
    uint32_t instruction;
    uint16_t conditionMask;
    uint8_t kind;
    uint8_t reserved;
};

struct Arm32Bus
{
    void* userData;
    Arm32ReadCallback fetch;
    Arm32ReadCallback read;
    Arm32WriteCallback write;
    Arm32SvcCallback svc;
    uint8_t* directSystemRam;
    uint32_t directSystemRamBase;
    uint32_t directSystemRamSize;
    uint8_t* directRam;
    uint32_t directRamBase;
    uint32_t directRamSize;
    uint8_t* directStack;
    uint32_t directStackBase;
    uint32_t directStackSize;
    uint8_t* directHeap;
    uint32_t directHeapBase;
    uint32_t directHeapSize;
    uint8_t* directFramebuffer;
    uint32_t directFramebufferBase;
    uint32_t directFramebufferSize;
    uint8_t* directProgram;
    uint32_t directProgramBase;
    uint32_t directProgramSize;
    uint32_t directThunkBase;
    uint32_t directThunkSize;
    Arm32InstructionCacheEntry* instructionCache;
    uint32_t instructionCacheCount;
    uint32_t* profilePcSamples;
    uint32_t* profileLrSamples;
    uint32_t profileSampleCount;
};

enum Arm32RunResult
{
    ARM32_RUN_OK = 0,
    ARM32_RUN_LIMIT = 1,
    ARM32_RUN_STOPPED = 2,
    ARM32_RUN_INVALID_MEMORY = 3,
    ARM32_RUN_UNSUPPORTED = 4
};

void arm32Reset(Arm32State* state, uint32_t entry, uint32_t stack,
    uint32_t linkRegister);
Arm32RunResult arm32Run(Arm32State* state, const Arm32Bus* bus,
    uint32_t stopPc, uint64_t instructionLimit);

#endif
