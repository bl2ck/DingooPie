#include "cc/cpu/arm32_interpreter.h"

#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#define ARM32_ALWAYS_INLINE inline __attribute__((always_inline))
#else
#define ARM32_ALWAYS_INLINE inline
#endif

static const uint32_t kFlagN = 1u << 31;
static const uint32_t kFlagZ = 1u << 30;
static const uint32_t kFlagC = 1u << 29;
static const uint32_t kFlagV = 1u << 28;

static const uint32_t kFlagQ = 1u << 27;
static const uint32_t kFlagT = 1u << 5;
static const uint16_t kConditionMasks[16] = {
    0xf0f0u, 0x0f0fu, 0xccccu, 0x3333u,
    0xff00u, 0x00ffu, 0xaaaau, 0x5555u,
    0x0c0cu, 0xf3f3u, 0xaa55u, 0x55aau,
    0x0a05u, 0xf5fau, 0xffffu, 0x0000u,
};

static uint32_t rotateRight(uint32_t value, uint32_t amount)
{
    amount &= 31u;
    return amount ? (value >> amount) | (value << (32u - amount)) : value;
}

static ARM32_ALWAYS_INLINE uint8_t* directPointer(const Arm32Bus* bus, uint32_t address,
    size_t size)
{
    if (!bus) return NULL;
    uint32_t offset = address - bus->directFramebufferBase;
    if (bus->directFramebuffer && address >= bus->directFramebufferBase &&
        offset < bus->directFramebufferSize &&
        size <= bus->directFramebufferSize - offset)
    {
        return bus->directFramebuffer + offset;
    }
    offset = address - bus->directHeapBase;
    if (bus->directHeap && address >= bus->directHeapBase &&
        offset < bus->directHeapSize &&
        size <= bus->directHeapSize - offset)
    {
        return bus->directHeap + offset;
    }
    offset = address - bus->directRamBase;
    if (bus->directRam && address >= bus->directRamBase &&
        offset < bus->directRamSize &&
        size <= bus->directRamSize - offset)
    {
        return bus->directRam + offset;
    }
    offset = address - bus->directSystemRamBase;
    if (bus->directSystemRam && address >= bus->directSystemRamBase &&
        offset < bus->directSystemRamSize &&
        size <= bus->directSystemRamSize - offset)
    {
        return bus->directSystemRam + offset;
    }
    offset = address - bus->directStackBase;
    if (bus->directStack && address >= bus->directStackBase &&
        offset < bus->directStackSize &&
        size <= bus->directStackSize - offset)
    {
        return bus->directStack + offset;
    }
    return NULL;
}

static ARM32_ALWAYS_INLINE bool readMemory(const Arm32Bus* bus, uint32_t address,
    void* output, size_t size)
{
    uint8_t* direct = directPointer(bus, address, size);
    if (direct)
    {
        if (size == sizeof(uint8_t)) *(uint8_t*)output = direct[0];
        else if (size == sizeof(uint16_t)) memcpy(output, direct, sizeof(uint16_t));
        else if (size == sizeof(uint32_t)) memcpy(output, direct, sizeof(uint32_t));
        else memcpy(output, direct, size);
        return true;
    }
    return bus && bus->read && bus->read(bus->userData, address, output, size);
}

static ARM32_ALWAYS_INLINE bool fetchMemory(const Arm32Bus* bus, uint32_t address,
    void* output, size_t size)
{
    if (bus)
    {
        uint32_t offset = address - bus->directProgramBase;
        bool program = address >= bus->directProgramBase &&
            offset < bus->directProgramSize &&
            size <= bus->directProgramSize - offset;
        if (program && bus->directProgram)
        {
            memcpy(output, bus->directProgram + offset, size);
            return true;
        }
        offset = address - bus->directThunkBase;
        bool thunk = address >= bus->directThunkBase &&
            offset < bus->directThunkSize &&
            size <= bus->directThunkSize - offset;
        if (program || thunk)
        {
            uint8_t* direct = directPointer(bus, address, size);
            if (direct)
            {
                memcpy(output, direct, size);
                return true;
            }
        }
    }
    Arm32ReadCallback fetch = bus && bus->fetch ? bus->fetch :
        (bus ? bus->read : NULL);
    return fetch && fetch(bus->userData, address, output, size);
}

static ARM32_ALWAYS_INLINE bool writeMemory(const Arm32Bus* bus, uint32_t address,
    const void* input, size_t size)
{
    uint8_t* direct = directPointer(bus, address, size);
    if (direct)
    {
        if (size == sizeof(uint8_t)) direct[0] = *(const uint8_t*)input;
        else if (size == sizeof(uint16_t)) memcpy(direct, input, sizeof(uint16_t));
        else if (size == sizeof(uint32_t)) memcpy(direct, input, sizeof(uint32_t));
        else memcpy(direct, input, size);
        return true;
    }
    return bus && bus->write && bus->write(bus->userData, address, input, size);
}

static ARM32_ALWAYS_INLINE uint32_t readRegister(const Arm32State* state, uint32_t index)
{
    // During instruction execution r15 already points to the next ARM word.
    return index == 15 ? state->r[15] + 4u : state->r[index];
}

static void writeLoadedPc(Arm32State* state, uint32_t value)
{
    if (value & 1u)
    {
        state->cpsr |= kFlagT;
        state->r[15] = value & ~1u;
    }
    else
    {
        state->cpsr &= ~kFlagT;
        state->r[15] = value & ~3u;
    }
}

static ARM32_ALWAYS_INLINE bool conditionPassed(uint32_t cpsr, uint32_t condition)
{
    uint32_t flags = (cpsr >> 28) & 0xfu;
    return ((kConditionMasks[condition & 0xfu] >> flags) & 1u) != 0u;
}

static void setNz(Arm32State* state, uint32_t value)
{
    state->cpsr &= ~(kFlagN | kFlagZ);
    if (value & 0x80000000u) state->cpsr |= kFlagN;
    if (!value) state->cpsr |= kFlagZ;
}

static void setLogicalFlags(Arm32State* state, uint32_t value,
    bool carryValid, bool carry)
{
    setNz(state, value);
    if (carryValid)
    {
        state->cpsr &= ~kFlagC;
        if (carry) state->cpsr |= kFlagC;
    }
}

static void setAddFlags(Arm32State* state, uint32_t left, uint32_t right,
    uint32_t carryIn, uint32_t result)
{
    uint64_t wide = (uint64_t)left + right + carryIn;
    setNz(state, result);
    state->cpsr &= ~(kFlagC | kFlagV);
    if (wide >> 32) state->cpsr |= kFlagC;
    int64_t signedWide = (int64_t)(int32_t)left + (int32_t)right + carryIn;
    if (signedWide > INT32_MAX || signedWide < INT32_MIN) state->cpsr |= kFlagV;
}

static void setSubFlags(Arm32State* state, uint32_t left, uint32_t right,
    uint32_t borrow, uint32_t result)
{
    uint64_t subtrahend = (uint64_t)right + borrow;
    setNz(state, result);
    state->cpsr &= ~(kFlagC | kFlagV);
    if ((uint64_t)left >= subtrahend) state->cpsr |= kFlagC;
    int64_t signedWide = (int64_t)(int32_t)left - (int32_t)right - borrow;
    if (signedWide > INT32_MAX || signedWide < INT32_MIN) state->cpsr |= kFlagV;
}

struct ShiftResult
{
    uint32_t value;
    bool carry;
    bool carryValid;
};

static ShiftResult shiftValue(uint32_t value, uint32_t type, uint32_t amount,
    bool registerShift, bool oldCarry)
{
    ShiftResult result = { value, oldCarry, false };
    if (registerShift && amount == 0) return result;
    switch (type)
    {
    case 0:
        if (amount == 0) return result;
        result.carryValid = true;
        result.carry = amount <= 32 ? ((value >> (32 - amount)) & 1u) != 0 : false;
        result.value = amount < 32 ? value << amount : 0;
        break;
    case 1:
        if (!registerShift && amount == 0) amount = 32;
        result.carryValid = true;
        result.carry = amount <= 32 ? ((value >> (amount - 1)) & 1u) != 0 : false;
        result.value = amount < 32 ? value >> amount : 0;
        break;
    case 2:
        if (!registerShift && amount == 0) amount = 32;
        result.carryValid = true;
        if (amount >= 32)
        {
            result.carry = (value & 0x80000000u) != 0;
            result.value = (uint32_t)((int32_t)value >> 31);
        }
        else
        {
            result.carry = ((value >> (amount - 1)) & 1u) != 0;
            result.value = (uint32_t)((int32_t)value >> amount);
        }
        break;
    default:
        if (!registerShift && amount == 0)
        {
            result.carryValid = true;
            result.carry = (value & 1u) != 0;
            result.value = (oldCarry ? 0x80000000u : 0) | (value >> 1);
        }
        else
        {
            amount &= 31u;
            if (amount)
            {
                result.carryValid = true;
                result.value = rotateRight(value, amount);
                result.carry = (result.value & 0x80000000u) != 0;
            }
        }
        break;
    }
    return result;
}

static ShiftResult decodeOperand2(const Arm32State* state, uint32_t instruction)
{
    bool oldCarry = (state->cpsr & kFlagC) != 0;
    if (instruction & (1u << 25))
    {
        uint32_t amount = ((instruction >> 8) & 0xfu) * 2u;
        uint32_t value = rotateRight(instruction & 0xffu, amount);
        ShiftResult result = { value, (value & 0x80000000u) != 0, amount != 0 };
        return result;
    }
    uint32_t rm = instruction & 0xfu;
    bool registerShift = (instruction & (1u << 4)) != 0;
    uint32_t amount = registerShift ?
        (readRegister(state, (instruction >> 8) & 0xfu) & 0xffu) :
        ((instruction >> 7) & 0x1fu);
    return shiftValue(readRegister(state, rm), (instruction >> 5) & 3u,
        amount, registerShift, oldCarry);
}

static ARM32_ALWAYS_INLINE uint32_t decodeUnflaggedOperand2(
    const Arm32State* state, uint32_t instruction)
{
    if (instruction & (1u << 25))
    {
        uint32_t amount = ((instruction >> 8) & 0xfu) * 2u;
        return rotateRight(instruction & 0xffu, amount);
    }
    uint32_t value = readRegister(state, instruction & 0xfu);
    uint32_t amount = (instruction >> 7) & 0x1fu;
    switch ((instruction >> 5) & 3u)
    {
    case 0:
        return amount ? value << amount : value;
    case 1:
        return amount ? value >> amount : 0u;
    case 2:
        return amount ? (uint32_t)((int32_t)value >> amount) :
            (uint32_t)((int32_t)value >> 31);
    default:
        if (amount) return rotateRight(value, amount);
        return ((state->cpsr & kFlagC) ? 0x80000000u : 0u) | (value >> 1);
    }
}

static ARM32_ALWAYS_INLINE bool executeDataProcessing(Arm32State* state,
    uint32_t instruction)
{
    uint32_t opcode = (instruction >> 21) & 0xfu;
    bool setFlags = (instruction & (1u << 20)) != 0;
    uint32_t rd = (instruction >> 12) & 0xfu;
    if (!setFlags && (opcode < 8u || opcode >= 12u) &&
        (instruction & (1u << 4)) == 0u)
    {
        uint32_t operand = decodeUnflaggedOperand2(state, instruction);
        uint32_t left = readRegister(state, (instruction >> 16) & 0xfu);
        uint32_t result = 0;
        switch (opcode)
        {
        case 0x0: result = left & operand; break;
        case 0x1: result = left ^ operand; break;
        case 0x2: result = left - operand; break;
        case 0x3: result = operand - left; break;
        case 0x4: result = left + operand; break;
        case 0x5: result = left + operand +
            ((state->cpsr & kFlagC) ? 1u : 0u); break;
        case 0x6: result = left - operand -
            ((state->cpsr & kFlagC) ? 0u : 1u); break;
        case 0x7: result = operand - left -
            ((state->cpsr & kFlagC) ? 0u : 1u); break;
        case 0xc: result = left | operand; break;
        case 0xd: result = operand; break;
        case 0xe: result = left & ~operand; break;
        default: result = ~operand; break;
        }
        state->r[rd] = result;
        if (rd == 15u) state->r[15] &= ~3u;
        return true;
    }
    uint32_t rn = (instruction >> 16) & 0xfu;
    uint32_t left = readRegister(state, rn);
    ShiftResult operand = decodeOperand2(state, instruction);
    uint32_t carry = (state->cpsr & kFlagC) ? 1u : 0u;
    uint32_t result = 0;
    bool writesResult = true;
    enum FlagMode { FLAG_LOGICAL, FLAG_ADD, FLAG_SUB } flagMode = FLAG_LOGICAL;
    uint32_t flagLeft = left;
    uint32_t flagRight = operand.value;
    uint32_t flagExtra = 0;
    switch (opcode)
    {
    case 0x0: result = left & operand.value; break;
    case 0x1: result = left ^ operand.value; break;
    case 0x2: result = left - operand.value; flagMode = FLAG_SUB; break;
    case 0x3: result = operand.value - left; flagMode = FLAG_SUB; flagLeft = operand.value; flagRight = left; break;
    case 0x4: result = left + operand.value; flagMode = FLAG_ADD; break;
    case 0x5: result = left + operand.value + carry; flagMode = FLAG_ADD; flagExtra = carry; break;
    case 0x6: result = left - operand.value - (1u - carry); flagMode = FLAG_SUB; flagExtra = 1u - carry; break;
    case 0x7: result = operand.value - left - (1u - carry); flagMode = FLAG_SUB; flagLeft = operand.value; flagRight = left; flagExtra = 1u - carry; break;
    case 0x8: result = left & operand.value; writesResult = false; setFlags = true; break;
    case 0x9: result = left ^ operand.value; writesResult = false; setFlags = true; break;
    case 0xa: result = left - operand.value; writesResult = false; setFlags = true; flagMode = FLAG_SUB; break;
    case 0xb: result = left + operand.value; writesResult = false; setFlags = true; flagMode = FLAG_ADD; break;
    case 0xc: result = left | operand.value; break;
    case 0xd: result = operand.value; break;
    case 0xe: result = left & ~operand.value; break;
    case 0xf: result = ~operand.value; break;
    }
    if (setFlags)
    {
        if (flagMode == FLAG_ADD) setAddFlags(state, flagLeft, flagRight, flagExtra, result);
        else if (flagMode == FLAG_SUB) setSubFlags(state, flagLeft, flagRight, flagExtra, result);
        else setLogicalFlags(state, result, operand.carryValid, operand.carry);
    }
    if (writesResult)
    {
        state->r[rd] = result;
        if (rd == 15) state->r[15] &= ~3u;
    }
    return true;
}

static ARM32_ALWAYS_INLINE bool executeMultiply(Arm32State* state, uint32_t instruction)
{
    bool longMultiply = (instruction & (1u << 23)) != 0;
    bool signedMultiply = (instruction & (1u << 22)) != 0;
    bool accumulate = (instruction & (1u << 21)) != 0;
    bool setFlags = (instruction & (1u << 20)) != 0;
    uint32_t rm = readRegister(state, instruction & 0xfu);
    uint32_t rs = readRegister(state, (instruction >> 8) & 0xfu);
    if (longMultiply)
    {
        uint32_t rdLo = (instruction >> 12) & 0xfu;
        uint32_t rdHi = (instruction >> 16) & 0xfu;
        uint64_t result = signedMultiply ?
            (uint64_t)((int64_t)(int32_t)rm * (int32_t)rs) :
            (uint64_t)rm * rs;
        if (accumulate) result += ((uint64_t)state->r[rdHi] << 32) | state->r[rdLo];
        state->r[rdLo] = (uint32_t)result;
        state->r[rdHi] = (uint32_t)(result >> 32);
        if (setFlags)
        {
            state->cpsr &= ~(kFlagN | kFlagZ);
            if (result & (1ull << 63)) state->cpsr |= kFlagN;
            if (!result) state->cpsr |= kFlagZ;
        }
    }
    else
    {
        uint32_t rd = (instruction >> 16) & 0xfu;
        uint32_t rn = (instruction >> 12) & 0xfu;
        uint32_t result = rm * rs;
        if (accumulate) result += state->r[rn];
        state->r[rd] = result;
        if (setFlags) setNz(state, result);
    }
    return true;
}

static ARM32_ALWAYS_INLINE bool executeCountLeadingZeros(Arm32State* state,
    uint32_t instruction)
{
    uint32_t value = readRegister(state, instruction & 0xfu);
    uint32_t count = 32u;
    if (value)
    {
#if defined(__GNUC__) || defined(__clang__)
        count = (uint32_t)__builtin_clz(value);
#else
        count = 0u;
        for (uint32_t bit = 0x80000000u; !(value & bit); bit >>= 1u) ++count;
#endif
    }
    state->r[(instruction >> 12) & 0xfu] = count;
    return true;
}

static int32_t signedHalfword(uint32_t value, bool top)
{
    return (int16_t)(top ? value >> 16 : value);
}

static ARM32_ALWAYS_INLINE bool executeSignedHalfwordMultiply(Arm32State* state,
    uint32_t instruction)
{
    uint32_t operation = instruction & 0x0ff00090u;
    uint32_t rm = readRegister(state, instruction & 0xfu);
    uint32_t rs = readRegister(state, (instruction >> 8) & 0xfu);
    int32_t left = signedHalfword(rm, (instruction & (1u << 5)) != 0);
    int32_t right = signedHalfword(rs, (instruction & (1u << 6)) != 0);
    int64_t product = (int64_t)left * right;
    if (operation == 0x01600080u)
    {
        state->r[(instruction >> 16) & 0xfu] = (uint32_t)product;
        return true;
    }
    if (operation == 0x01000080u)
    {
        uint32_t rd = (instruction >> 16) & 0xfu;
        uint32_t rn = (instruction >> 12) & 0xfu;
        int64_t result = product + (int32_t)state->r[rn];
        state->r[rd] = (uint32_t)result;
        if (result < INT32_MIN || result > INT32_MAX) state->cpsr |= kFlagQ;
        return true;
    }
    if (operation == 0x01400080u)
    {
        uint32_t rdHi = (instruction >> 16) & 0xfu;
        uint32_t rdLo = (instruction >> 12) & 0xfu;
        int64_t accumulator = (int64_t)(((uint64_t)state->r[rdHi] << 32) |
            state->r[rdLo]);
        uint64_t result = (uint64_t)(accumulator + product);
        state->r[rdLo] = (uint32_t)result;
        state->r[rdHi] = (uint32_t)(result >> 32);
        return true;
    }
    return false;
}

static ARM32_ALWAYS_INLINE bool executeHalfwordTransfer(Arm32State* state, const Arm32Bus* bus,
    uint32_t instruction)
{
    bool pre = (instruction & (1u << 24)) != 0;
    bool up = (instruction & (1u << 23)) != 0;
    bool immediate = (instruction & (1u << 22)) != 0;
    bool writeback = (instruction & (1u << 21)) != 0;
    bool load = (instruction & (1u << 20)) != 0;
    uint32_t rn = (instruction >> 16) & 0xfu;
    uint32_t rd = (instruction >> 12) & 0xfu;
    uint32_t type = (instruction >> 5) & 3u;
    uint32_t offset = immediate ?
        (((instruction >> 4) & 0xf0u) | (instruction & 0xfu)) :
        readRegister(state, instruction & 0xfu);
    uint32_t base = readRegister(state, rn);
    uint32_t adjusted = up ? base + offset : base - offset;
    uint32_t address = pre ? adjusted : base;
    if (load)
    {
        uint32_t value = 0;
        if (type == 1)
        {
            uint16_t half = 0;
            if (!readMemory(bus, address, &half, sizeof(half))) return false;
            value = half;
        }
        else if (type == 2)
        {
            int8_t byte = 0;
            if (!readMemory(bus, address, &byte, sizeof(byte))) return false;
            value = (uint32_t)(int32_t)byte;
        }
        else if (type == 3)
        {
            int16_t half = 0;
            if (!readMemory(bus, address, &half, sizeof(half))) return false;
            value = (uint32_t)(int32_t)half;
        }
        else return false;
        if (rd == 15) writeLoadedPc(state, value);
        else state->r[rd] = value;
    }
    else
    {
        if (type == 1)
        {
            uint16_t value = (uint16_t)readRegister(state, rd);
            if (!writeMemory(bus, address, &value, sizeof(value))) return false;
        }
        else if ((type == 2 || type == 3) && rd < 15)
        {
            uint32_t values[2] = {};
            if (type == 2)
            {
                if (!readMemory(bus, address, values, sizeof(values))) return false;
                state->r[rd] = values[0];
                state->r[rd + 1u] = values[1];
            }
            else
            {
                values[0] = readRegister(state, rd);
                values[1] = readRegister(state, rd + 1u);
                if (!writeMemory(bus, address, values, sizeof(values))) return false;
            }
        }
        else return false;
    }
    if (!pre || writeback) state->r[rn] = adjusted;
    return true;
}

static ARM32_ALWAYS_INLINE bool executeSingleTransfer(Arm32State* state, const Arm32Bus* bus,
    uint32_t instruction)
{
    bool registerOffset = (instruction & (1u << 25)) != 0;
    bool pre = (instruction & (1u << 24)) != 0;
    bool up = (instruction & (1u << 23)) != 0;
    bool byteTransfer = (instruction & (1u << 22)) != 0;
    bool writeback = (instruction & (1u << 21)) != 0;
    bool load = (instruction & (1u << 20)) != 0;
    uint32_t rn = (instruction >> 16) & 0xfu;
    uint32_t rd = (instruction >> 12) & 0xfu;
    uint32_t offset = instruction & 0xfffu;
    if (registerOffset)
    {
        ShiftResult shifted = shiftValue(readRegister(state, instruction & 0xfu),
            (instruction >> 5) & 3u, (instruction >> 7) & 0x1fu,
            false, (state->cpsr & kFlagC) != 0);
        offset = shifted.value;
    }
    uint32_t base = readRegister(state, rn);
    uint32_t adjusted = up ? base + offset : base - offset;
    uint32_t address = pre ? adjusted : base;
    if (load)
    {
        uint32_t value = 0;
        if (byteTransfer)
        {
            uint8_t byte = 0;
            if (!readMemory(bus, address, &byte, sizeof(byte))) return false;
            value = byte;
        }
        else if (!readMemory(bus, address, &value, sizeof(value))) return false;
        if (rd == 15) writeLoadedPc(state, value);
        else state->r[rd] = value;
    }
    else
    {
        uint32_t value = rd == 15 ? state->r[15] + 8u : state->r[rd];
        if (byteTransfer)
        {
            uint8_t byte = (uint8_t)value;
            if (!writeMemory(bus, address, &byte, sizeof(byte))) return false;
        }
        else if (!writeMemory(bus, address, &value, sizeof(value))) return false;
    }
    if (!pre || writeback) state->r[rn] = adjusted;
    return true;
}

static ARM32_ALWAYS_INLINE bool executeBlockTransfer(Arm32State* state, const Arm32Bus* bus,
    uint32_t instruction)
{
    bool pre = (instruction & (1u << 24)) != 0;
    bool up = (instruction & (1u << 23)) != 0;
    bool writeback = (instruction & (1u << 21)) != 0;
    bool load = (instruction & (1u << 20)) != 0;
    uint32_t rn = (instruction >> 16) & 0xfu;
    uint32_t list = instruction & 0xffffu;
    uint32_t count = 0;
    for (uint32_t i = 0; i < 16; ++i) if (list & (1u << i)) count++;
    if (!count) return false;
    uint32_t base = readRegister(state, rn);
    uint32_t address = up ? base + (pre ? 4u : 0u) :
        base - count * 4u + (pre ? 0u : 4u);
    uint8_t* direct = directPointer(bus, address, count * sizeof(uint32_t));
    if (direct)
    {
        uint32_t values[16] = {};
        if (load)
        {
            memcpy(values, direct, count * sizeof(uint32_t));
            uint32_t valueIndex = 0;
            for (uint32_t reg = 0; reg < 16; ++reg)
            {
                if (!(list & (1u << reg))) continue;
                if (reg == 15) writeLoadedPc(state, values[valueIndex]);
                else state->r[reg] = values[valueIndex];
                ++valueIndex;
            }
        }
        else
        {
            uint32_t valueIndex = 0;
            for (uint32_t reg = 0; reg < 16; ++reg)
            {
                if (!(list & (1u << reg))) continue;
                values[valueIndex++] = reg == 15 ? state->r[15] + 8u : state->r[reg];
            }
            memcpy(direct, values, count * sizeof(uint32_t));
        }
        if (writeback) state->r[rn] = up ? base + count * 4u : base - count * 4u;
        return true;
    }
    for (uint32_t reg = 0; reg < 16; ++reg)
    {
        if (!(list & (1u << reg))) continue;
        if (load)
        {
            uint32_t value = 0;
            if (!readMemory(bus, address, &value, sizeof(value))) return false;
            if (reg == 15) writeLoadedPc(state, value);
            else state->r[reg] = value;
        }
        else
        {
            uint32_t value = reg == 15 ? state->r[15] + 8u : state->r[reg];
            if (!writeMemory(bus, address, &value, sizeof(value))) return false;
        }
        address += 4;
    }
    if (writeback) state->r[rn] = up ? base + count * 4u : base - count * 4u;
    return true;
}

enum Arm32InstructionKind
{
    ARM32_KIND_UNKNOWN = 0,
    ARM32_KIND_BLX_IMMEDIATE,
    ARM32_KIND_BRANCH_EXCHANGE,
    ARM32_KIND_COUNT_LEADING_ZEROS,
    ARM32_KIND_SIGNED_HALFWORD_MULTIPLY,
    ARM32_KIND_MULTIPLY,
    ARM32_KIND_HALFWORD_TRANSFER,
    ARM32_KIND_MRS,
    ARM32_KIND_DATA_PROCESSING,
    ARM32_KIND_SINGLE_TRANSFER,
    ARM32_KIND_BLOCK_TRANSFER,
    ARM32_KIND_BRANCH,
    ARM32_KIND_SVC,
    ARM32_KIND_UNSUPPORTED
};

static ARM32_ALWAYS_INLINE uint8_t decodeArmInstruction(uint32_t instruction)
{
    if ((instruction & 0xfe000000u) == 0xfa000000u)
    {
        return ARM32_KIND_BLX_IMMEDIATE;
    }
    if ((instruction & 0x0ffffff0u) == 0x012fff10u ||
        (instruction & 0x0ffffff0u) == 0x012fff30u)
    {
        return ARM32_KIND_BRANCH_EXCHANGE;
    }
    if ((instruction & 0x0fff0ff0u) == 0x016f0f10u)
    {
        return ARM32_KIND_COUNT_LEADING_ZEROS;
    }
    uint32_t signedHalfwordMultiply = instruction & 0x0ff00090u;
    if (signedHalfwordMultiply == 0x01000080u ||
        signedHalfwordMultiply == 0x01400080u ||
        signedHalfwordMultiply == 0x01600080u)
    {
        return ARM32_KIND_SIGNED_HALFWORD_MULTIPLY;
    }
    if ((instruction & 0x0f0000f0u) == 0x00000090u)
    {
        return ARM32_KIND_MULTIPLY;
    }
    if ((instruction & 0x0e000090u) == 0x00000090u)
    {
        return ARM32_KIND_HALFWORD_TRANSFER;
    }
    if ((instruction & 0x0fbf0fffu) == 0x010f0000u)
    {
        return ARM32_KIND_MRS;
    }
    switch ((instruction >> 25) & 7u)
    {
    case 0:
    case 1:
        return ARM32_KIND_DATA_PROCESSING;
    case 2:
    case 3:
        return ARM32_KIND_SINGLE_TRANSFER;
    case 4:
        return ARM32_KIND_BLOCK_TRANSFER;
    case 5:
        return ARM32_KIND_BRANCH;
    case 7:
        return (instruction & 0x0f000000u) == 0x0f000000u ?
            ARM32_KIND_SVC : ARM32_KIND_UNSUPPORTED;
    default:
        return ARM32_KIND_UNSUPPORTED;
    }
}

static ARM32_ALWAYS_INLINE uint8_t armInstructionMayChangeFlow(
    uint32_t instruction, uint8_t kind)
{
    if (kind == ARM32_KIND_DATA_PROCESSING)
    {
        return ((instruction >> 12) & 0xfu) == 15u;
    }
    if (kind == ARM32_KIND_SINGLE_TRANSFER)
    {
        return (instruction & (1u << 20)) != 0u &&
            ((instruction >> 12) & 0xfu) == 15u;
    }
    return kind == ARM32_KIND_BLX_IMMEDIATE ||
        kind == ARM32_KIND_BRANCH_EXCHANGE ||
        kind == ARM32_KIND_BRANCH ||
        kind == ARM32_KIND_SVC ||
        kind == ARM32_KIND_MULTIPLY ||
        kind == ARM32_KIND_SIGNED_HALFWORD_MULTIPLY ||
        kind == ARM32_KIND_HALFWORD_TRANSFER ||
        kind == ARM32_KIND_BLOCK_TRANSFER ||
        kind == ARM32_KIND_MRS;
}
static ARM32_ALWAYS_INLINE uint8_t cachedArmInstructionKind(const Arm32Bus* bus,
    uint32_t pc, uint32_t instruction)
{
    uint32_t offset = pc - bus->directProgramBase;
    if (!bus->instructionCache || pc < bus->directProgramBase ||
        (offset & 3u) != 0 || offset >= bus->directProgramSize)
    {
        return decodeArmInstruction(instruction);
    }
    uint32_t index = offset >> 2;
    if (index >= bus->instructionCacheCount)
    {
        return decodeArmInstruction(instruction);
    }
    Arm32InstructionCacheEntry* entry = bus->instructionCache + index;
    if (entry->kind == ARM32_KIND_UNKNOWN || entry->instruction != instruction)
    {
        entry->instruction = instruction;
        entry->kind = decodeArmInstruction(instruction);
        entry->reserved = armInstructionMayChangeFlow(instruction, entry->kind);
    }
    return entry->kind;
}

static ARM32_ALWAYS_INLINE bool fetchCachedArmInstruction(const Arm32Bus* bus,
    uint32_t pc, uint32_t* instruction, uint8_t* kind,
    uint16_t* conditionMask)
{
    uint32_t offset = pc - bus->directProgramBase;
    if (bus->instructionCache && pc >= bus->directProgramBase &&
        (offset & 3u) == 0 && offset < bus->directProgramSize)
    {
        uint32_t index = offset >> 2;
        if (index < bus->instructionCacheCount)
        {
            Arm32InstructionCacheEntry* entry = bus->instructionCache + index;
            if (entry->kind != ARM32_KIND_UNKNOWN && bus->directProgram)
            {
                *instruction = entry->instruction;
                *kind = entry->kind;
                *conditionMask = entry->conditionMask;
                return true;
            }
            if (!fetchMemory(bus, pc, instruction, sizeof(*instruction)))
            {
                return false;
            }
            if (entry->kind == ARM32_KIND_UNKNOWN ||
                entry->instruction != *instruction)
            {
                entry->instruction = *instruction;
                entry->kind = decodeArmInstruction(*instruction);
                entry->reserved = armInstructionMayChangeFlow(*instruction,
                    entry->kind);
                entry->conditionMask = kConditionMasks[*instruction >> 28];
            }
            *kind = entry->kind;
            *conditionMask = entry->conditionMask;
            return true;
        }
    }
    if (!fetchMemory(bus, pc, instruction, sizeof(*instruction)))
    {
        return false;
    }
    *kind = cachedArmInstructionKind(bus, pc, *instruction);
    *conditionMask = kConditionMasks[*instruction >> 28];
    return true;
}

static ARM32_ALWAYS_INLINE bool executeArmInstruction(Arm32State* state,
    const Arm32Bus* bus, uint32_t instruction, uint8_t kind,
    uint16_t conditionMask, bool* stopped)
{
    uint32_t pc = state->r[15];
    state->r[15] = pc + 4u;
    if (kind == ARM32_KIND_BLX_IMMEDIATE)
    {
        int32_t displacement = (int32_t)(instruction << 8) >> 6;
        uint32_t lowBit = (instruction >> 23) & 2u;
        state->r[14] = pc + 4u;
        state->r[15] = pc + 8u + (uint32_t)displacement + lowBit;
        state->cpsr |= kFlagT;
        return true;
    }
    if (conditionMask != 0xffffu)
    {
        uint32_t flags = (state->cpsr >> 28) & 0xfu;
        if (((conditionMask >> flags) & 1u) == 0u)
        {
            return true;
        }
    }

    switch (kind)
    {
    case ARM32_KIND_BRANCH_EXCHANGE:
    {
        bool link = (instruction & 0x20u) != 0;
        uint32_t target = readRegister(state, instruction & 0xfu);
        if (link) state->r[14] = pc + 4u;
        if (target & 1u) state->cpsr |= kFlagT;
        else state->cpsr &= ~kFlagT;
        state->r[15] = target & ~1u;
        return true;
    }
    case ARM32_KIND_COUNT_LEADING_ZEROS:
        return executeCountLeadingZeros(state, instruction);
    case ARM32_KIND_SIGNED_HALFWORD_MULTIPLY:
        return executeSignedHalfwordMultiply(state, instruction);
    case ARM32_KIND_MULTIPLY:
        return executeMultiply(state, instruction);
    case ARM32_KIND_HALFWORD_TRANSFER:
        return executeHalfwordTransfer(state, bus, instruction);
    case ARM32_KIND_MRS:
        state->r[(instruction >> 12) & 0xfu] = state->cpsr;
        return true;
    case ARM32_KIND_DATA_PROCESSING:
        return executeDataProcessing(state, instruction);
    case ARM32_KIND_SINGLE_TRANSFER:
        return executeSingleTransfer(state, bus, instruction);
    case ARM32_KIND_BLOCK_TRANSFER:
        return executeBlockTransfer(state, bus, instruction);
    case ARM32_KIND_BRANCH:
    {
        int32_t displacement = (int32_t)(instruction << 8) >> 6;
        if (instruction & (1u << 24)) state->r[14] = pc + 4u;
        state->r[15] = pc + 8u + (uint32_t)displacement;
        return true;
    }
    case ARM32_KIND_SVC:
        if (!bus->svc || !bus->svc(bus->userData, state,
                instruction & 0x00ffffffu))
        {
            *stopped = true;
        }
        return true;
    default:
        state->unsupportedInstruction = instruction;
        state->unsupportedPc = pc;
        return false;
    }
}

static uint32_t thumbPcValue(const Arm32State* state)
{
    return (state->r[15] + 2u) & ~3u;
}

static bool executeThumbInstruction(Arm32State* state, const Arm32Bus* bus,
    uint16_t instruction, bool* stopped)
{
    uint32_t pc = state->r[15];
    state->r[15] = pc + 2u;

    if ((instruction & 0xe000u) == 0x0000u)
    {
        uint32_t op = (instruction >> 11) & 3u;
        uint32_t rd = instruction & 7u;
        uint32_t rs = (instruction >> 3) & 7u;
        if (op < 3)
        {
            uint32_t amount = (instruction >> 6) & 0x1fu;
            ShiftResult shifted = shiftValue(state->r[rs], op, amount,
                false, (state->cpsr & kFlagC) != 0);
            state->r[rd] = shifted.value;
            setLogicalFlags(state, shifted.value, shifted.carryValid, shifted.carry);
            return true;
        }
        bool immediate = (instruction & (1u << 10)) != 0;
        bool subtract = (instruction & (1u << 9)) != 0;
        uint32_t right = immediate ? ((instruction >> 6) & 7u) :
            state->r[(instruction >> 6) & 7u];
        uint32_t left = state->r[rs];
        uint32_t result = subtract ? left - right : left + right;
        state->r[rd] = result;
        if (subtract) setSubFlags(state, left, right, 0, result);
        else setAddFlags(state, left, right, 0, result);
        return true;
    }
    if ((instruction & 0xe000u) == 0x2000u)
    {
        uint32_t op = (instruction >> 11) & 3u;
        uint32_t rd = (instruction >> 8) & 7u;
        uint32_t immediate = instruction & 0xffu;
        uint32_t left = state->r[rd];
        uint32_t result = 0;
        if (op == 0)
        {
            state->r[rd] = immediate;
            setNz(state, immediate);
        }
        else if (op == 1)
        {
            result = left - immediate;
            setSubFlags(state, left, immediate, 0, result);
        }
        else if (op == 2)
        {
            result = left + immediate;
            state->r[rd] = result;
            setAddFlags(state, left, immediate, 0, result);
        }
        else
        {
            result = left - immediate;
            state->r[rd] = result;
            setSubFlags(state, left, immediate, 0, result);
        }
        return true;
    }
    if ((instruction & 0xfc00u) == 0x4000u)
    {
        uint32_t op = (instruction >> 6) & 0xfu;
        uint32_t rd = instruction & 7u;
        uint32_t rs = (instruction >> 3) & 7u;
        uint32_t left = state->r[rd];
        uint32_t right = state->r[rs];
        uint32_t result = 0;
        switch (op)
        {
        case 0: result = left & right; state->r[rd] = result; setNz(state, result); break;
        case 1: result = left ^ right; state->r[rd] = result; setNz(state, result); break;
        case 2: { ShiftResult s = shiftValue(left, 0, right & 0xffu, true, (state->cpsr & kFlagC) != 0); state->r[rd] = s.value; setLogicalFlags(state, s.value, s.carryValid, s.carry); break; }
        case 3: { ShiftResult s = shiftValue(left, 1, right & 0xffu, true, (state->cpsr & kFlagC) != 0); state->r[rd] = s.value; setLogicalFlags(state, s.value, s.carryValid, s.carry); break; }
        case 4: { ShiftResult s = shiftValue(left, 2, right & 0xffu, true, (state->cpsr & kFlagC) != 0); state->r[rd] = s.value; setLogicalFlags(state, s.value, s.carryValid, s.carry); break; }
        case 5: { uint32_t c = (state->cpsr & kFlagC) ? 1u : 0u; result = left + right + c; state->r[rd] = result; setAddFlags(state, left, right, c, result); break; }
        case 6: { uint32_t b = (state->cpsr & kFlagC) ? 0u : 1u; result = left - right - b; state->r[rd] = result; setSubFlags(state, left, right, b, result); break; }
        case 7: { ShiftResult s = shiftValue(left, 3, right & 0xffu, true, (state->cpsr & kFlagC) != 0); state->r[rd] = s.value; setLogicalFlags(state, s.value, s.carryValid, s.carry); break; }
        case 8: setNz(state, left & right); break;
        case 9: result = 0u - right; state->r[rd] = result; setSubFlags(state, 0, right, 0, result); break;
        case 10: result = left - right; setSubFlags(state, left, right, 0, result); break;
        case 11: result = left + right; setAddFlags(state, left, right, 0, result); break;
        case 12: result = left | right; state->r[rd] = result; setNz(state, result); break;
        case 13: result = left * right; state->r[rd] = result; setNz(state, result); break;
        case 14: result = left & ~right; state->r[rd] = result; setNz(state, result); break;
        case 15: result = ~right; state->r[rd] = result; setNz(state, result); break;
        }
        return true;
    }
    if ((instruction & 0xfc00u) == 0x4400u)
    {
        uint32_t op = (instruction >> 8) & 3u;
        uint32_t rd = (instruction & 7u) | ((instruction >> 4) & 8u);
        uint32_t rs = ((instruction >> 3) & 7u) | ((instruction >> 3) & 8u);
        uint32_t right = rs == 15 ? thumbPcValue(state) : state->r[rs];
        if (op == 0)
        {
            state->r[rd] += right;
            if (rd == 15) state->r[15] &= ~1u;
        }
        else if (op == 1)
        {
            uint32_t left = rd == 15 ? thumbPcValue(state) : state->r[rd];
            setSubFlags(state, left, right, 0, left - right);
        }
        else if (op == 2)
        {
            state->r[rd] = right;
            if (rd == 15) state->r[15] &= ~1u;
        }
        else
        {
            uint32_t target = right;
            if (instruction & 0x0080u) state->r[14] = (pc + 2u) | 1u;
            if (target & 1u) state->cpsr |= kFlagT;
            else state->cpsr &= ~kFlagT;
            state->r[15] = target & ~1u;
        }
        return true;
    }
    if ((instruction & 0xf800u) == 0x4800u)
    {
        uint32_t address = thumbPcValue(state) + (instruction & 0xffu) * 4u;
        uint32_t value = 0;
        if (!readMemory(bus, address, &value, sizeof(value))) return false;
        state->r[(instruction >> 8) & 7u] = value;
        return true;
    }
    if ((instruction & 0xf000u) == 0x5000u)
    {
        uint32_t op = (instruction >> 9) & 7u;
        uint32_t address = state->r[(instruction >> 3) & 7u] +
            state->r[(instruction >> 6) & 7u];
        uint32_t rd = instruction & 7u;
        if (op == 0) return writeMemory(bus, address, &state->r[rd], 4);
        if (op == 1) { uint16_t v = (uint16_t)state->r[rd]; return writeMemory(bus, address, &v, 2); }
        if (op == 2) { uint8_t v = (uint8_t)state->r[rd]; return writeMemory(bus, address, &v, 1); }
        if (op == 3) { int8_t v = 0; if (!readMemory(bus, address, &v, 1)) return false; state->r[rd] = (uint32_t)(int32_t)v; return true; }
        if (op == 4) return readMemory(bus, address, &state->r[rd], 4);
        if (op == 5) { uint16_t v = 0; if (!readMemory(bus, address, &v, 2)) return false; state->r[rd] = v; return true; }
        if (op == 6) { uint8_t v = 0; if (!readMemory(bus, address, &v, 1)) return false; state->r[rd] = v; return true; }
        int16_t v = 0; if (!readMemory(bus, address, &v, 2)) return false; state->r[rd] = (uint32_t)(int32_t)v; return true;
    }
    if ((instruction & 0xe000u) == 0x6000u || (instruction & 0xf000u) == 0x8000u)
    {
        bool halfword = (instruction & 0xf000u) == 0x8000u;
        bool byteTransfer = !halfword && (instruction & 0x1000u) != 0;
        bool load = (instruction & 0x0800u) != 0;
        uint32_t rn = (instruction >> 3) & 7u;
        uint32_t rd = instruction & 7u;
        uint32_t scale = halfword ? 2u : (byteTransfer ? 1u : 4u);
        uint32_t address = state->r[rn] + ((instruction >> 6) & 0x1fu) * scale;
        if (load)
        {
            uint32_t value = 0;
            if (halfword) { uint16_t v = 0; if (!readMemory(bus, address, &v, 2)) return false; value = v; }
            else if (byteTransfer) { uint8_t v = 0; if (!readMemory(bus, address, &v, 1)) return false; value = v; }
            else if (!readMemory(bus, address, &value, 4)) return false;
            state->r[rd] = value;
        }
        else
        {
            if (halfword) { uint16_t v = (uint16_t)state->r[rd]; return writeMemory(bus, address, &v, 2); }
            if (byteTransfer) { uint8_t v = (uint8_t)state->r[rd]; return writeMemory(bus, address, &v, 1); }
            return writeMemory(bus, address, &state->r[rd], 4);
        }
        return true;
    }
    if ((instruction & 0xf000u) == 0x9000u)
    {
        bool load = (instruction & 0x0800u) != 0;
        uint32_t rd = (instruction >> 8) & 7u;
        uint32_t address = state->r[13] + (instruction & 0xffu) * 4u;
        return load ? readMemory(bus, address, &state->r[rd], 4) :
            writeMemory(bus, address, &state->r[rd], 4);
    }
    if ((instruction & 0xf000u) == 0xa000u)
    {
        uint32_t rd = (instruction >> 8) & 7u;
        uint32_t base = (instruction & 0x0800u) ? state->r[13] : thumbPcValue(state);
        state->r[rd] = base + (instruction & 0xffu) * 4u;
        return true;
    }
    if ((instruction & 0xff00u) == 0xb000u)
    {
        uint32_t amount = (instruction & 0x7fu) * 4u;
        state->r[13] = (instruction & 0x80u) ? state->r[13] - amount : state->r[13] + amount;
        return true;
    }
    if ((instruction & 0xf600u) == 0xb400u)
    {
        bool pop = (instruction & 0x0800u) != 0;
        uint32_t list = instruction & 0xffu;
        uint32_t count = 0;
        for (uint32_t i = 0; i < 8; ++i) if (list & (1u << i)) count++;
        if (instruction & 0x0100u) count++;
        if (!pop)
        {
            uint32_t address = state->r[13] - count * 4u;
            for (uint32_t reg = 0; reg < 8; ++reg) if (list & (1u << reg)) { if (!writeMemory(bus, address, &state->r[reg], 4)) return false; address += 4; }
            if (instruction & 0x0100u) { if (!writeMemory(bus, address, &state->r[14], 4)) return false; }
            state->r[13] -= count * 4u;
        }
        else
        {
            uint32_t address = state->r[13];
            for (uint32_t reg = 0; reg < 8; ++reg) if (list & (1u << reg)) { if (!readMemory(bus, address, &state->r[reg], 4)) return false; address += 4; }
            if (instruction & 0x0100u)
            {
                uint32_t target = 0;
                if (!readMemory(bus, address, &target, 4)) return false;
                state->r[15] = target & ~1u;
                if (target & 1u) state->cpsr |= kFlagT; else state->cpsr &= ~kFlagT;
            }
            state->r[13] += count * 4u;
        }
        return true;
    }
    if ((instruction & 0xf000u) == 0xc000u)
    {
        bool load = (instruction & 0x0800u) != 0;
        uint32_t rn = (instruction >> 8) & 7u;
        uint32_t address = state->r[rn];
        uint32_t list = instruction & 0xffu;
        for (uint32_t reg = 0; reg < 8; ++reg)
        {
            if (!(list & (1u << reg))) continue;
            if (load) { if (!readMemory(bus, address, &state->r[reg], 4)) return false; }
            else if (!writeMemory(bus, address, &state->r[reg], 4)) return false;
            address += 4;
        }
        if (!load || !(list & (1u << rn))) state->r[rn] = address;
        return true;
    }
    if ((instruction & 0xf000u) == 0xd000u)
    {
        uint32_t condition = (instruction >> 8) & 0xfu;
        if (condition == 0xfu)
        {
            if (!bus->svc || !bus->svc(bus->userData, state, instruction & 0xffu)) *stopped = true;
        }
        else if (condition != 0xeu && conditionPassed(state->cpsr, condition))
        {
            int32_t offset = (int8_t)(instruction & 0xffu);
            state->r[15] = pc + 4u + (uint32_t)(offset << 1);
        }
        return condition != 0xeu;
    }
    if ((instruction & 0xf800u) == 0xe000u)
    {
        int32_t offset = (int32_t)(instruction << 21) >> 20;
        state->r[15] = pc + 4u + (uint32_t)offset;
        return true;
    }
    if ((instruction & 0xf800u) == 0xe800u)
    {
        uint32_t target = state->r[14] + ((instruction & 0x07ffu) << 1);
        state->r[14] = (pc + 2u) | 1u;
        state->r[15] = target & ~3u;
        state->cpsr &= ~kFlagT;
        return true;
    }
    if ((instruction & 0xf800u) == 0xf000u)
    {
        int32_t high = (int32_t)(instruction & 0x07ffu);
        if (high & 0x400) high |= ~0x7ff;
        state->r[14] = pc + 4u + (uint32_t)(high << 12);
        return true;
    }
    if ((instruction & 0xf800u) == 0xf800u)
    {
        uint32_t target = state->r[14] + ((instruction & 0x07ffu) << 1);
        state->r[14] = (pc + 2u) | 1u;
        state->r[15] = target & ~1u;
        state->cpsr |= kFlagT;
        return true;
    }
    state->unsupportedInstruction = instruction;
    state->unsupportedPc = pc;
    return false;
}

void arm32Reset(Arm32State* state, uint32_t entry, uint32_t stack,
    uint32_t linkRegister)
{
    if (!state) return;
    memset(state, 0, sizeof(*state));
    state->r[13] = stack;
    state->r[14] = linkRegister;
    state->r[15] = entry & ~1u;
    state->cpsr = entry & 1u ? kFlagT : 0;
}

Arm32RunResult arm32Run(Arm32State* state, const Arm32Bus* bus,
    uint32_t stopPc, uint64_t instructionLimit)
{
    if (!state || !bus || !bus->read || !bus->write)
    {
        return ARM32_RUN_INVALID_MEMORY;
    }
    while (!instructionLimit || state->instructions < instructionLimit)
    {
        if (state->r[15] == stopPc) return ARM32_RUN_OK;
        if (bus->profilePcSamples && (state->instructions & 0x3ffu) == 0u)
        {
            uint32_t pcOffset = state->r[15] - bus->directProgramBase;
            uint32_t pcIndex = pcOffset >> 2;
            if (state->r[15] >= bus->directProgramBase &&
                pcIndex < bus->profileSampleCount)
            {
                ++bus->profilePcSamples[pcIndex];
            }
            uint32_t lrOffset = state->r[14] - bus->directProgramBase;
            uint32_t lrIndex = lrOffset >> 2;
            if (state->r[14] >= bus->directProgramBase &&
                lrIndex < bus->profileSampleCount)
            {
                ++bus->profileLrSamples[lrIndex];
            }
        }
        if (state->cpsr & kFlagT)
        {
            uint16_t instruction = 0;
            if (!fetchMemory(bus, state->r[15], &instruction, sizeof(instruction)))
            {
                return ARM32_RUN_INVALID_MEMORY;
            }
            bool stopped = false;
            if (!executeThumbInstruction(state, bus, instruction, &stopped))
            {
                return ARM32_RUN_UNSUPPORTED;
            }
            state->instructions++;
            if (stopped) return ARM32_RUN_STOPPED;
            continue;
        }
        if (!bus->profilePcSamples && bus->directProgram &&
            bus->instructionCache)
        {
            uint32_t blockPc = state->r[15];
            uint32_t blockOffset = blockPc - bus->directProgramBase;
            if (blockPc >= bus->directProgramBase &&
                (blockOffset & 3u) == 0u &&
                blockOffset < bus->directProgramSize)
            {
                uint32_t cacheIndex = blockOffset >> 2;
                uint32_t blockCount = 0;
                uint32_t blockLimit = 32u;
                if (instructionLimit)
                {
                    uint64_t remaining = instructionLimit - state->instructions;
                    if (remaining < blockLimit) blockLimit = (uint32_t)remaining;
                }
                while (cacheIndex < bus->instructionCacheCount &&
                    blockOffset + sizeof(uint32_t) <= bus->directProgramSize &&
                    blockCount < blockLimit)
                {
                    uint32_t pc = state->r[15];
                    Arm32InstructionCacheEntry* entry =
                        bus->instructionCache + cacheIndex;
                    if (entry->kind == ARM32_KIND_UNKNOWN)
                    {
                        memcpy(&entry->instruction,
                            bus->directProgram + blockOffset,
                            sizeof(entry->instruction));
                        entry->kind = decodeArmInstruction(entry->instruction);
                        entry->reserved = armInstructionMayChangeFlow(
                            entry->instruction, entry->kind);
                        entry->conditionMask =
                            kConditionMasks[entry->instruction >> 28];
                    }
                    bool stopped = false;
                    if (!executeArmInstruction(state, bus, entry->instruction,
                            entry->kind, entry->conditionMask, &stopped))
                    {
                        return ARM32_RUN_UNSUPPORTED;
                    }
                    state->instructions++;
                    if (entry->kind == ARM32_KIND_SVC && stopped)
                    {
                        return ARM32_RUN_STOPPED;
                    }
                    if (entry->reserved &&
                        ((state->cpsr & kFlagT) || state->r[15] != pc + 4u))
                    {
                        break;
                    }
                    blockOffset += sizeof(uint32_t);
                    ++cacheIndex;
                    ++blockCount;
                }
                continue;
            }
        }
        uint32_t instruction = 0;
        uint32_t pc = state->r[15];
        uint8_t kind = ARM32_KIND_UNKNOWN;
        uint16_t conditionMask = 0u;
        if (!fetchCachedArmInstruction(bus, pc, &instruction, &kind,
                &conditionMask))
        {
            return ARM32_RUN_INVALID_MEMORY;
        }
        bool stopped = false;
        if (!executeArmInstruction(state, bus, instruction, kind,
                conditionMask, &stopped))
        {
            return ARM32_RUN_UNSUPPORTED;
        }
        state->instructions++;
        if (stopped) return ARM32_RUN_STOPPED;
    }
    return ARM32_RUN_LIMIT;
}
