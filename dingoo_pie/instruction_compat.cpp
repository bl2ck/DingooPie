#include "instruction_compat.h"

#include <SDL2/SDL.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <vector>

#include "emulated_memory.h"
#include "framebuffer.h"

extern uint32_t s_AppDataAddr;
extern uint32_t s_AppDataBuffSize;
extern void* s_AppDataBuff;

static EmulatorOptions g_options;
static std::vector<uint32_t> g_objectFlagsPredicateAddresses;

static bool readRuntimeInsn(NativeRuntime* runtime, uint64_t address, uint32_t* out);
static bool isLuiRt(uint32_t insn, uint32_t rt);
static bool isLwRtBase(uint32_t insn, uint32_t rt, uint32_t base);
static uint32_t jalTarget(uint32_t pc, uint32_t insn);
static bool jalTargetsNamedImportOrWrapper(
    const uint8_t* bin,
    uint32_t scanSize,
    const app* appInfo,
    uint32_t callPc,
    uint32_t callInsn,
    const char* const* importNames,
    uint32_t importNameCount);

static uint32_t readLe32(const uint8_t* data)
{
    return (uint32_t)data[0] |
        ((uint32_t)data[1] << 8) |
        ((uint32_t)data[2] << 16) |
        ((uint32_t)data[3] << 24);
}

static bool instructionNeedsCompatHook(uint32_t insn)
{
    return insn == 0x000001CD;
}

static bool instructionLooksLikeTransparentBlit16(const uint8_t* bin, uint32_t off, uint32_t scanSize)
{
    static const uint32_t signature[] =
    {
        0x00804021u, // move t0, a0
        0x8c83003cu, // lw v1, 0x3c(a0)
        0x8c820034u, // lw v0, 0x34(a0)
        0x00625823u, // subu t3, v1, v0
        0x8c840040u, // lw a0, 0x40(a0)
        0x8d030038u, // lw v1, 0x38(t0)
        0x8d02000cu, // lw v0, 0x0c(t0)
        0x00832023u, // subu a0, a0, v1
        0x1080001fu, // beqz a0, return
        0x94420000u, // lhu v0, 0(v0)
    };

    if (off + sizeof(signature) > scanSize)
    {
        return false;
    }

    for (uint32_t i = 0; i < sizeof(signature) / sizeof(signature[0]); ++i)
    {
        if (readLe32(bin + off + i * sizeof(uint32_t)) != signature[i])
        {
            return false;
        }
    }
    return true;
}

enum PixelLoopKind
{
    PIXEL_LOOP_NONE = 0,
    PIXEL_LOOP_SEQUENTIAL_COPY16,
    PIXEL_LOOP_REPEAT_STORE16
};

enum IndexedTransformBlit16Kind
{
    INDEXED_TRANSFORM_BLIT16_NONE = 0,
    INDEXED_TRANSFORM_BLIT16_320X240
};

enum CompactTransparentBlit16Kind
{
    COMPACT_TRANSPARENT_BLIT16_NONE = 0,
    COMPACT_TRANSPARENT_BLIT16_REVERSE_COPY,
    COMPACT_TRANSPARENT_BLIT16_HFLIP
};

enum MemoryRoutineKind
{
    MEMORY_ROUTINE_NONE = 0,
    MEMORY_ROUTINE_MEMCPY,
    MEMORY_ROUTINE_MEMSET
};

enum RowCopyLoopKind
{
    ROW_COPY_LOOP_NONE = 0,
    ROW_COPY_LOOP_RGB565,
    ROW_COPY_LOOP_LINEAR_RGB565_FRAME
};

enum ObjectPredicateAggregateKind
{
    OBJECT_PREDICATE_AGGREGATE_NONE = 0,
    OBJECT_PREDICATE_AGGREGATE_FLAGS_OR
};

enum TinyPredicateKind
{
    TINY_PREDICATE_NONE = 0,
    TINY_PREDICATE_OBJECT_FLAGS,
    TINY_PREDICATE_TILE_BLOCKED
};

static MemoryRoutineKind instructionLooksLikeMemoryRoutine(const uint8_t* bin, uint32_t off, uint32_t scanSize)
{
    if (off + 12 * sizeof(uint32_t) <= scanSize)
    {
        uint32_t w[12];
        for (uint32_t i = 0; i < sizeof(w) / sizeof(w[0]); ++i)
        {
            w[i] = readLe32(bin + off + i * sizeof(uint32_t));
        }

        if (w[0] == 0x00801021u && // move v0, a0
            w[1] == 0x10c00084u && // beqz a2, return
            w[2] == 0x2cca000cu && // sltiu t2, a2, 12
            w[3] == 0x1540007bu && // bnez t2, byte-copy-tail
            w[4] == 0x00a41826u && // xor v1, a1, a0
            w[5] == 0x30630003u && // andi v1, v1, 3
            w[6] == 0x00043823u && // negu a3, a0
            w[7] == 0x10600042u && // beqz v1, aligned-copy
            w[8] == 0x30e70003u && // andi a3, a3, 3
            w[9] == 0x10e00006u && // beqz a3, unaligned-copy
            w[10] == 0x00c73023u && // subu a2, a2, a3
            w[11] == 0x98a30000u)   // lwl v1, 0(a1)
        {
            return MEMORY_ROUTINE_MEMCPY;
        }
    }

    if (off + 12 * sizeof(uint32_t) <= scanSize)
    {
        uint32_t w[12];
        for (uint32_t i = 0; i < sizeof(w) / sizeof(w[0]); ++i)
        {
            w[i] = readLe32(bin + off + i * sizeof(uint32_t));
        }

        if (w[0] == 0x00801021u && // move v0, a0
            w[1] == 0x10c00026u && // beqz a2, return
            w[2] == 0x2cca0010u && // sltiu t2, a2, 16
            w[3] == 0x1540001fu && // bnez t2, byte-fill-tail
            w[4] == 0x30a500ffu && // andi a1, a1, 0xff
            w[5] == 0x00055200u && // sll t2, a1, 8
            w[6] == 0x00aa2825u && // or a1, a1, t2
            w[7] == 0x00055400u && // sll t2, a1, 16
            w[8] == 0x00aa2825u && // or a1, a1, t2
            w[9] == 0x30830003u && // andi v1, a0, 3
            w[10] == 0x10600005u && // beqz v1, aligned-fill
            w[11] == 0x24070004u)   // li a3, 4
        {
            return MEMORY_ROUTINE_MEMSET;
        }
    }

    return MEMORY_ROUTINE_NONE;
}

static TinyPredicateKind instructionLooksLikeTinyPredicate(const uint8_t* bin, uint32_t off, uint32_t scanSize)
{
    if (off + 13 * sizeof(uint32_t) <= scanSize)
    {
        uint32_t w[13];
        for (uint32_t i = 0; i < sizeof(w) / sizeof(w[0]); ++i)
        {
            w[i] = readLe32(bin + off + i * sizeof(uint32_t));
        }

        if (w[0] == 0x30a500ffu &&     // andi a1, a1, 0xff
            w[1] == 0x30c600ffu &&     // andi a2, a2, 0xff
            w[2] == 0x94820006u &&     // lhu v0, 6(a0)
            w[3] == 0x10400009u &&     // beqz v0, return
            w[4] == 0x00001821u &&     // move v1, zero
            w[5] == 0x9082000au &&     // lbu v0, 0xa(a0)
            w[6] == 0x14450005u &&     // bne v0, a1, return
            w[7] == 0x00003821u &&     // move a3, zero
            w[8] == 0x9082000bu &&     // lbu v0, 0xb(a0)
            w[9] == 0x00461026u &&     // xor v0, v0, a2
            w[10] == 0x24030001u &&    // li v1, 1
            w[11] == 0x0062380au &&    // slt a3, v1, v0
            w[12] == 0x00e01821u)      // move v1, a3
        {
            return TINY_PREDICATE_OBJECT_FLAGS;
        }
    }

    if (off + 20 * sizeof(uint32_t) <= scanSize)
    {
        uint32_t w[20];
        for (uint32_t i = 0; i < sizeof(w) / sizeof(w[0]); ++i)
        {
            w[i] = readLe32(bin + off + i * sizeof(uint32_t));
        }

        if (w[0] == 0x30a500ffu &&     // andi a1, a1, 0xff
            w[1] == 0x8c820008u &&     // lw v0, 8(a0)
            w[2] == 0x00a2102au &&     // slt v0, a1, v0
            w[3] == 0x10400005u &&     // beqz v0, return-zero
            w[4] == 0x30c600ffu &&     // andi a2, a2, 0xff
            w[5] == 0x8c82000cu &&     // lw v0, 0xc(a0)
            w[6] == 0x00c2102au &&     // slt v0, a2, v0
            w[7] == 0x14400003u &&     // bnez v0, lookup
            w[8] == 0x00000000u &&
            w[9] == 0x03e00008u &&
            w[10] == 0x00001021u &&    // move v0, zero
            w[11] == 0x8c820008u &&    // lw v0, 8(a0)
            w[12] == 0x8c830010u &&    // lw v1, 0x10(a0)
            w[13] == 0x70c22002u &&    // mul a0, a2, v0
            w[14] == 0x00851021u &&    // addu v0, a0, a1
            w[15] == 0x00621821u &&    // addu v1, v1, v0
            w[16] == 0x90620000u &&    // lbu v0, 0(v1)
            w[17] == 0x2c420004u &&    // sltiu v0, v0, 4
            w[18] == 0x03e00008u &&
            w[19] == 0x38420001u)      // xori v0, v0, 1
        {
            return TINY_PREDICATE_TILE_BLOCKED;
        }
    }

    return TINY_PREDICATE_NONE;
}

static bool instructionLooksLikeInlineObjectFlagsPredicate(const uint32_t* w)
{
    return w[0] == 0x94620006u &&     // lhu v0, 6(v1)
        w[1] == 0x10400008u &&        // beqz v0, next
        w[2] == 0x00002821u &&        // move a1, zero
        w[3] == 0x9062000au &&        // lbu v0, 0xa(v1)
        w[4] == 0x14510006u &&        // bne v0, s1, next
        w[5] == 0x24020001u &&        // li v0, 1
        w[6] == 0x9062000bu &&        // lbu v0, 0xb(v1)
        w[7] == 0x00521026u &&        // xor v0, v0, s2
        w[8] == 0x24030001u &&        // li v1, 1
        w[9] == 0x0062280au &&        // slt a1, v1, v0
        w[10] == 0x24020001u &&       // li v0, 1
        w[11] == 0x0045980bu;         // movn s3, v0, a1
}

static ObjectPredicateAggregateKind instructionLooksLikeObjectPredicateAggregate(
    const uint8_t* bin, uint32_t off, uint32_t scanSize)
{
    if (off + 192 * sizeof(uint32_t) > scanSize)
    {
        return OBJECT_PREDICATE_AGGREGATE_NONE;
    }

    uint32_t w[192];
    for (uint32_t i = 0; i < sizeof(w) / sizeof(w[0]); ++i)
    {
        w[i] = readLe32(bin + off + i * sizeof(uint32_t));
    }

    if (w[0] != 0x27bdffd8u ||
        w[1] != 0xafbf0024u ||
        w[2] != 0xafb40020u ||
        w[3] != 0xafb3001cu ||
        w[4] != 0xafb20018u ||
        w[5] != 0xafb10014u ||
        w[6] != 0xafb00010u ||
        w[7] != 0x0080a021u ||       // move s4, a0
        w[8] != 0x30b100ffu ||       // andi s1, a1, 0xff
        w[9] != 0x30d200ffu ||       // andi s2, a2, 0xff
        w[10] != 0x00009821u ||      // move s3, zero
        w[11] != 0x24830054u ||
        w[12] != 0x94620006u ||
        w[13] != 0x1040000du ||
        w[14] != 0x00002821u ||
        w[15] != 0x0828d210u ||
        w[16] != 0x9062000au ||
        w[17] != 0x0828d22au ||
        w[18] != 0x24130001u ||
        w[19] != 0x0828d23du ||
        w[20] != 0x24130001u ||
        w[21] != 0x14510006u ||
        w[22] != 0x24020001u ||
        w[23] != 0x9062000bu ||
        w[24] != 0x00521026u ||
        w[25] != 0x24030001u ||
        w[26] != 0x0062280au ||
        w[27] != 0x24020001u ||
        w[28] != 0x0045980bu)
    {
        return OBJECT_PREDICATE_AGGREGATE_NONE;
    }

    static const uint32_t firstLoop[] =
    {
        0x00008021u, 0x00101040u, 0x00501021u, 0x00021080u,
        0x00501021u, 0x000210c0u, 0x00541021u, 0x244400bcu,
        0x8c4200bcu, 0x8c420018u, 0x02202821u, 0x0040f809u,
        0x02403021u, 0x1440ffe6u, 0x26100001u, 0x2a020032u,
        0x1440fff1u, 0x00101040u
    };
    for (uint32_t i = 0; i < sizeof(firstLoop) / sizeof(firstLoop[0]); ++i)
    {
        if (w[29 + i] != firstLoop[i])
        {
            return OBJECT_PREDICATE_AGGREGATE_NONE;
        }
    }

    static const uint32_t secondLoop[] =
    {
        0x00008021u, 0x001010c0u, 0x00501021u, 0x00021080u,
        0x00501023u, 0x00021080u, 0x00541021u, 0x24441598u,
        0x8c421598u, 0x8c420018u, 0x02202821u, 0x0040f809u,
        0x02403021u, 0x1440ffd6u, 0x00000000u, 0x26100001u,
        0x2a02000au, 0x1440fff0u, 0x001010c0u
    };
    for (uint32_t i = 0; i < sizeof(secondLoop) / sizeof(secondLoop[0]); ++i)
    {
        if (w[47 + i] != secondLoop[i])
        {
            return OBJECT_PREDICATE_AGGREGATE_NONE;
        }
    }

    static const uint32_t inlineOffsets[] =
    {
        0x150c, 0x1b10, 0x1b2c, 0x1b40, 0x1b60, 0x1b80, 0x1b9c, 0x1c04
    };
    for (uint32_t i = 0; i < sizeof(inlineOffsets) / sizeof(inlineOffsets[0]); ++i)
    {
        uint32_t base = 66 + i * 13;
        if (w[base] != 0x26830000u + inlineOffsets[i] ||
            !instructionLooksLikeInlineObjectFlagsPredicate(&w[base + 1]))
        {
            return OBJECT_PREDICATE_AGGREGATE_NONE;
        }
    }

    uint32_t finalBase = 170;
    if (w[finalBase] != 0x26841c20u ||
        w[finalBase + 1] != 0x94820006u ||
        w[finalBase + 2] != 0x10400008u ||
        w[finalBase + 3] != 0x00003821u ||
        w[finalBase + 4] != 0x9082000au ||
        w[finalBase + 5] != 0x14510006u ||
        w[finalBase + 6] != 0x24020001u ||
        w[finalBase + 7] != 0x9082000bu ||
        w[finalBase + 8] != 0x00521026u ||
        w[finalBase + 9] != 0x24030001u ||
        w[finalBase + 10] != 0x0062380au ||
        w[finalBase + 11] != 0x24020001u ||
        w[finalBase + 12] != 0x0047980bu ||
        w[finalBase + 13] != 0x02601021u ||
        w[finalBase + 14] != 0x8fbf0024u ||
        w[finalBase + 15] != 0x8fb40020u ||
        w[finalBase + 16] != 0x8fb3001cu ||
        w[finalBase + 17] != 0x8fb20018u ||
        w[finalBase + 18] != 0x8fb10014u ||
        w[finalBase + 19] != 0x8fb00010u ||
        w[finalBase + 20] != 0x03e00008u ||
        w[finalBase + 21] != 0x27bd0028u)
    {
        return OBJECT_PREDICATE_AGGREGATE_NONE;
    }

    return OBJECT_PREDICATE_AGGREGATE_FLAGS_OR;
}

static RowCopyLoopKind instructionLooksLikeRowCopyLoop(
    const uint8_t* bin,
    uint32_t off,
    uint32_t scanSize,
    uint32_t address,
    const app* appInfo)
{
    if (off + 40 * sizeof(uint32_t) > scanSize)
    {
        return ROW_COPY_LOOP_NONE;
    }

    uint32_t w[40];
    for (uint32_t i = 0; i < sizeof(w) / sizeof(w[0]); ++i)
    {
        w[i] = readLe32(bin + off + i * sizeof(uint32_t));
    }

    if (w[0] == 0x27bdffd8u &&     // addiu sp, sp, -0x28
        w[1] == 0xafbf0020u &&
        w[2] == 0xafb3001cu &&
        w[3] == 0xafb20018u &&
        w[4] == 0xafb10014u &&
        w[5] == 0xafb00010u &&
        w[6] == 0x00808021u &&     // move s0, a0
        w[7] == 0x8c850034u &&     // lw a1, 0x34(a0)
        w[8] == 0x8c84002cu &&     // lw a0, 0x2c(a0)
        w[9] == 0x8e020004u &&     // lw v0, 4(s0)
        w[10] == 0x94460000u &&    // lhu a2, 0(v0)
        w[11] == 0x8e030030u &&    // lw v1, 0x30(s0)
        w[12] == 0x8e020028u &&    // lw v0, 0x28(s0)
        w[13] == 0x00629023u &&    // subu s2, v1, v0
        w[14] == 0x00a48823u &&    // subu s1, a1, a0
        w[15] == 0x12200011u &&    // beqz s1, return
        w[16] == 0x00129040u &&    // sll s2, s2, 1
        w[17] == 0x00069840u &&    // sll s3, a2, 1
        w[18] == 0x8e04001cu &&    // lw a0, 0x1c(s0)
        w[19] == 0x8e050018u &&    // lw a1, 0x18(s0)
        (w[20] >> 26) == 0x03u &&  // jal memcpy-like routine
        w[21] == 0x02403021u &&    // move a2, s2
        isLuiRt(w[22], 2) &&
        isLwRtBase(w[23], 2, 2) &&
        w[24] == 0x00021040u &&    // sll v0, v0, 1
        w[25] == 0x8e03001cu &&    // lw v1, 0x1c(s0)
        w[26] == 0x00431021u &&    // addu v0, v0, v1
        w[27] == 0xae02001cu &&    // sw v0, 0x1c(s0)
        w[28] == 0x8e020018u &&    // lw v0, 0x18(s0)
        w[29] == 0x02621021u &&    // addu v0, s3, v0
        w[30] == 0x2631ffffu &&    // addiu s1, s1, -1
        w[31] == 0x1620fff2u &&    // bnez s1, loop
        w[32] == 0xae020018u &&    // sw v0, 0x18(s0)
        w[33] == 0x8fbf0020u &&
        w[34] == 0x8fb3001cu &&
        w[35] == 0x8fb20018u &&
        w[36] == 0x8fb10014u &&
        w[37] == 0x8fb00010u &&
        w[38] == 0x03e00008u &&
        w[39] == 0x27bd0028u)
    {
        return ROW_COPY_LOOP_RGB565;
    }

    if (off + 29 * sizeof(uint32_t) <= scanSize)
    {
        uint32_t w[29];
        for (uint32_t i = 0; i < sizeof(w) / sizeof(w[0]); ++i)
        {
            w[i] = readLe32(bin + off + i * sizeof(uint32_t));
        }

        if (w[0] == 0x27bdffe8u &&
            w[1] == 0xafbf0014u &&
            w[2] == 0xafb00010u &&
            w[3] == 0x90820008u &&
            w[4] == 0x10400014u &&
            w[5] == 0x00808021u &&
            (w[6] >> 26) == 0x03u &&
            w[7] == 0xa0800008u &&
            w[8] == 0xae02000cu &&
            w[9] == 0x8e040004u &&
            w[10] == 0x00401821u &&
            w[11] == 0x00002821u &&
            w[12] == 0x3c060001u &&
            w[13] == 0x34c62bffu &&
            w[14] == 0x94820000u &&
            w[15] == 0x24840002u &&
            w[16] == 0xa4620000u &&
            w[17] == 0x24a50001u &&
            w[18] == 0x00c5102au &&
            w[19] == 0x1040fffau &&
            w[20] == 0x24630002u &&
            (w[21] >> 26) == 0x03u &&
            w[22] == 0x00000000u &&
            w[23] == 0x24020001u &&
            w[24] == 0xa2020008u &&
            w[25] == 0x8fbf0014u &&
            w[26] == 0x8fb00010u &&
            w[27] == 0x03e00008u &&
            w[28] == 0x27bd0018u)
        {
            static const char* const lcdGetNames[] =
            {
                "_lcd_get_frame", "lcd_get_frame"
            };
            static const char* const lcdSetNames[] =
            {
                "_lcd_set_frame", "lcd_set_frame", "ap_lcd_set_frame", "lcd_flip"
            };
            if (jalTargetsNamedImportOrWrapper(bin, scanSize, appInfo, address + 0x18u, w[6],
                    lcdGetNames, sizeof(lcdGetNames) / sizeof(lcdGetNames[0])) &&
                jalTargetsNamedImportOrWrapper(bin, scanSize, appInfo, address + 0x54u, w[21],
                    lcdSetNames, sizeof(lcdSetNames) / sizeof(lcdSetNames[0])))
            {
                return ROW_COPY_LOOP_LINEAR_RGB565_FRAME;
            }
        }
    }

    return ROW_COPY_LOOP_NONE;
}

static CompactTransparentBlit16Kind instructionLooksLikeCompactTransparentBlit16(
    const uint8_t* bin, uint32_t off, uint32_t scanSize)
{
    if (off + 43 * sizeof(uint32_t) > scanSize)
    {
        return COMPACT_TRANSPARENT_BLIT16_NONE;
    }

    uint32_t w[43];
    for (uint32_t i = 0; i < sizeof(w) / sizeof(w[0]); ++i)
    {
        w[i] = readLe32(bin + off + i * sizeof(uint32_t));
    }

    if (w[0] == 0x00804021u &&     // move t0, a0
        w[1] == 0x8c830030u &&     // lw v1, 0x30(a0)
        w[2] == 0x8c820028u &&     // lw v0, 0x28(a0)
        w[3] == 0x00625823u &&     // subu t3, v1, v0
        w[4] == 0x8c840034u &&     // lw a0, 0x34(a0)
        w[5] == 0x8d03002cu &&     // lw v1, 0x2c(t0)
        w[6] == 0x8d020004u &&     // lw v0, 4(t0)
        w[7] == 0x00832023u &&     // subu a0, a0, v1
        w[8] == 0x10800020u &&     // beqz a0, return
        w[9] == 0x94420000u &&     // lhu v0, 0(v0)
        w[10] == 0x240cffffu &&    // li t4, -1
        w[11] == 0x00025040u &&    // sll t2, v0, 1
        w[12] == 0x2565ffffu &&    // addiu a1, t3, -1
        w[13] == 0x10ac0010u &&    // beq a1, t4, next-row
        w[14] == 0x00000000u &&
        w[15] == 0x2409ffffu &&    // li t1, -1
        w[16] == 0x8d020018u &&    // lw v0, 0x18(t0)
        w[17] == 0x00053840u &&    // sll a3, a1, 1
        w[18] == 0x00e21021u &&    // addu v0, a3, v0
        w[19] == 0x94460000u &&    // lhu a2, 0(v0)
        w[20] == 0x30c3ffffu &&    // andi v1, a2, 0xffff
        w[21] == 0x9502000eu &&    // lhu v0, 0xe(t0)
        w[22] == 0x10620004u &&    // beq v1, v0, skip-store
        w[23] == 0x00000000u &&
        w[24] == 0x8d02001cu &&    // lw v0, 0x1c(t0)
        w[25] == 0x00e21021u &&    // addu v0, a3, v0
        w[26] == 0xa4460000u &&    // sh a2, 0(v0)
        w[27] == 0x24a5ffffu &&    // addiu a1, a1, -1
        w[28] == 0x14a9fff3u &&    // bne a1, t1, loop
        w[29] == 0x00000000u &&
        isLuiRt(w[30], 2) &&
        isLwRtBase(w[31], 2, 2) &&
        w[32] == 0x00021040u &&    // sll v0, v0, 1
        w[33] == 0x8d03001cu &&    // lw v1, 0x1c(t0)
        w[34] == 0x00431021u &&    // addu v0, v0, v1
        w[35] == 0xad02001cu &&    // sw v0, 0x1c(t0)
        w[36] == 0x8d020018u &&    // lw v0, 0x18(t0)
        w[37] == 0x01421021u &&    // addu v0, t2, v0
        w[38] == 0x2484ffffu &&    // addiu a0, a0, -1
        w[39] == 0x1480ffe4u &&    // bnez a0, row-loop
        w[40] == 0xad020018u &&    // sw v0, 0x18(t0)
        w[41] == 0x03e00008u &&
        w[42] == 0x00000000u)
    {
        return COMPACT_TRANSPARENT_BLIT16_REVERSE_COPY;
    }

    if (w[0] == 0x00803821u &&     // move a3, a0
        w[1] == 0x8c830030u &&     // lw v1, 0x30(a0)
        w[2] == 0x8c820028u &&     // lw v0, 0x28(a0)
        w[3] == 0x00624823u &&     // subu t1, v1, v0
        w[4] == 0x8c840034u &&     // lw a0, 0x34(a0)
        w[5] == 0x8ce3002cu &&     // lw v1, 0x2c(a3)
        w[6] == 0x8ce20004u &&     // lw v0, 4(a3)
        w[7] == 0x00832023u &&     // subu a0, a0, v1
        w[8] == 0x10800020u &&     // beqz a0, return
        w[9] == 0x94420000u &&     // lhu v0, 0(v0)
        w[10] == 0x240bffffu &&    // li t3, -1
        w[11] == 0x00025040u &&    // sll t2, v0, 1
        w[12] == 0x2525ffffu &&    // addiu a1, t1, -1
        w[13] == 0x10ab0010u &&    // beq a1, t3, next-row
        w[14] == 0x01251023u &&    // subu v0, t1, a1
        w[15] == 0x2408ffffu &&    // li t0, -1
        w[16] == 0x8ce30018u &&    // lw v1, 0x18(a3)
        w[17] == 0x00021040u &&    // sll v0, v0, 1
        w[18] == 0x00431021u &&    // addu v0, v0, v1
        w[19] == 0x9446fffeu &&    // lhu a2, -2(v0)
        w[20] == 0x30c3ffffu &&    // andi v1, a2, 0xffff
        w[21] == 0x94e2000eu &&    // lhu v0, 0xe(a3)
        w[22] == 0x10620004u &&    // beq v1, v0, skip-store
        w[23] == 0x00051040u &&    // sll v0, a1, 1
        w[24] == 0x8ce3001cu &&    // lw v1, 0x1c(a3)
        w[25] == 0x00431021u &&    // addu v0, v0, v1
        w[26] == 0xa4460000u &&    // sh a2, 0(v0)
        w[27] == 0x24a5ffffu &&    // addiu a1, a1, -1
        w[28] == 0x14a8fff3u &&    // bne a1, t0, loop
        w[29] == 0x01251023u &&    // subu v0, t1, a1
        isLuiRt(w[30], 2) &&
        isLwRtBase(w[31], 2, 2) &&
        w[32] == 0x00021040u &&    // sll v0, v0, 1
        w[33] == 0x8ce3001cu &&    // lw v1, 0x1c(a3)
        w[34] == 0x00431021u &&    // addu v0, v0, v1
        w[35] == 0xace2001cu &&    // sw v0, 0x1c(a3)
        w[36] == 0x8ce20018u &&    // lw v0, 0x18(a3)
        w[37] == 0x01421021u &&    // addu v0, t2, v0
        w[38] == 0x2484ffffu &&    // addiu a0, a0, -1
        w[39] == 0x1480ffe4u &&    // bnez a0, row-loop
        w[40] == 0xace20018u &&    // sw v0, 0x18(a3)
        w[41] == 0x03e00008u &&
        w[42] == 0x00000000u)
    {
        return COMPACT_TRANSPARENT_BLIT16_HFLIP;
    }

    return COMPACT_TRANSPARENT_BLIT16_NONE;
}

static bool instructionLooksLikeIndexedBlit8ToRgb565(const uint8_t* bin, uint32_t off, uint32_t scanSize)
{
    if (off + 43 * sizeof(uint32_t) > scanSize)
    {
        return false;
    }

    uint32_t w[43];
    for (uint32_t i = 0; i < sizeof(w) / sizeof(w[0]); ++i)
    {
        w[i] = readLe32(bin + off + i * sizeof(uint32_t));
    }

    return w[0] == 0x00804021u && // move t0, a0
        w[1] == 0x8c830030u &&    // lw v1, 0x30(a0)
        w[2] == 0x8c820028u &&    // lw v0, 0x28(a0)
        w[3] == 0x00624823u &&    // subu t1, v1, v0
        w[4] == 0x8c840034u &&    // lw a0, 0x34(a0)
        w[5] == 0x8d03002cu &&    // lw v1, 0x2c(t0)
        w[6] == 0x8d020004u &&    // lw v0, 4(t0)
        w[7] == 0x944a0000u &&    // lhu t2, 0(v0)
        w[8] == 0x00832023u &&    // subu a0, a0, v1
        w[9] == 0x1080001fu &&    // beqz a0, return
        w[10] == 0x8d060024u &&   // lw a2, 0x24(t0)
        w[11] == 0x11200011u &&   // beqz t1, next-row
        w[12] == 0x00003821u &&   // move a3, zero
        w[13] == 0x90c20000u &&   // lbu v0, 0(a2)
        w[14] == 0x8d030018u &&   // lw v1, 0x18(t0)
        w[15] == 0x00021040u &&   // sll v0, v0, 1
        w[16] == 0x00431021u &&   // addu v0, v0, v1
        w[17] == 0x94450000u &&   // lhu a1, 0(v0)
        w[18] == 0x30a3ffffu &&   // andi v1, a1, 0xffff
        w[19] == 0x9502000eu &&   // lhu v0, 0xe(t0)
        w[20] == 0x10620005u &&   // beq v1, v0, skip-store
        w[21] == 0x24c60001u &&   // addiu a2, a2, 1
        w[22] == 0x8d03001cu &&   // lw v1, 0x1c(t0)
        w[23] == 0x00071040u &&   // sll v0, a3, 1
        w[24] == 0x00431021u &&   // addu v0, v0, v1
        w[25] == 0xa4450000u &&   // sh a1, 0(v0)
        w[26] == 0x24e70001u &&   // addiu a3, a3, 1
        w[27] == 0x14e9fff1u &&   // bne a3, t1, loop
        w[28] == 0x00000000u &&
        isLuiRt(w[29], 2) &&
        isLwRtBase(w[30], 2, 2) &&
        w[31] == 0x00021040u &&   // sll v0, v0, 1
        w[32] == 0x8d03001cu &&   // lw v1, 0x1c(t0)
        w[33] == 0x00431021u &&   // addu v0, v0, v1
        w[34] == 0xad02001cu &&   // sw v0, 0x1c(t0)
        w[35] == 0x8d020024u &&   // lw v0, 0x24(t0)
        w[36] == 0x01421021u &&   // addu v0, t2, v0
        w[37] == 0xad020024u &&   // sw v0, 0x24(t0)
        w[38] == 0x2484ffffu &&   // addiu a0, a0, -1
        w[39] == 0x1480ffe3u &&   // bnez a0, row-loop
        w[40] == 0x00403021u &&   // move a2, v0
        w[41] == 0x03e00008u &&
        w[42] == 0x00000000u;
}

static IndexedTransformBlit16Kind instructionLooksLikeIndexedTransformBlit16(
    const uint8_t* bin,
    uint32_t off,
    uint32_t scanSize)
{
    if (off + 65 * sizeof(uint32_t) > scanSize)
    {
        return INDEXED_TRANSFORM_BLIT16_NONE;
    }

    uint32_t w[65];
    for (uint32_t i = 0; i < sizeof(w) / sizeof(w[0]); ++i)
    {
        w[i] = readLe32(bin + off + i * sizeof(uint32_t));
    }

    if (w[0] == 0x00804021u &&
        w[1] == 0x8c820008u &&
        w[2] == 0x14400005u &&
        w[3] == 0x00a03821u &&
        w[4] == 0x00001021u &&
        w[5] == 0x14400002u &&
        w[6] == 0x00000000u &&
        w[7] == 0x000001cdu &&
        w[8] == 0x240a00f0u &&
        w[9] == 0x24090140u &&
        w[10] == 0x8ce30000u &&
        w[11] == 0x00032402u &&
        w[12] == 0x10800025u &&
        w[13] == 0x24e70004u &&
        w[14] == 0x8d02000cu &&
        w[15] == 0x0082102au &&
        w[16] == 0x10400021u &&
        w[17] == 0x00000000u &&
        w[18] == 0x8d020010u &&
        w[19] == 0x0044102au &&
        w[20] == 0x14400003u &&
        w[21] == 0x00000000u &&
        w[22] == 0x0828ac75u &&
        w[23] == 0x95050028u &&
        w[24] == 0x3063ffffu &&
        w[25] == 0x8d020004u &&
        w[26] == 0x00431021u &&
        w[27] == 0x90420000u &&
        w[28] == 0x8d030008u &&
        w[29] == 0x00021040u &&
        w[30] == 0x00431021u &&
        w[31] == 0x94450000u &&
        w[32] == 0x8d030010u &&
        w[33] == 0x00831823u &&
        w[34] == 0x8d020014u &&
        w[35] == 0x00431021u &&
        w[36] == 0x90420000u &&
        w[37] == 0x30a3ffffu &&
        w[38] == 0x00032302u &&
        w[39] == 0x000318c2u &&
        w[40] == 0x30a5001fu &&
        w[41] == 0x00052842u &&
        w[42] == 0x00021300u &&
        w[43] == 0x00042200u &&
        w[44] == 0x00441025u &&
        w[45] == 0x306300f0u &&
        w[46] == 0x00431025u &&
        w[47] == 0x00451025u &&
        w[48] == 0x0828ac72u &&
        w[49] == 0x8d030018u &&
        w[50] == 0x3063ffffu &&
        w[51] == 0x8d020004u &&
        w[52] == 0x00431021u &&
        w[53] == 0x90420000u &&
        w[54] == 0x8d030008u &&
        w[55] == 0x00021040u &&
        w[56] == 0x00431021u &&
        w[57] == 0x94450000u &&
        w[58] == 0xa4c50000u &&
        w[59] == 0x2529ffffu &&
        w[60] == 0x1520ffcdu &&
        w[61] == 0x24c60002u &&
        w[62] == 0x254affffu &&
        w[63] == 0x1540ffc9u &&
        w[64] == 0x00000000u)
    {
        return INDEXED_TRANSFORM_BLIT16_320X240;
    }

    return INDEXED_TRANSFORM_BLIT16_NONE;
}

static bool isLuiRt(uint32_t insn, uint32_t rt)
{
    return (insn >> 26) == 0x0fu && ((insn >> 16) & 0x1fu) == rt;
}

static bool isLwRtBase(uint32_t insn, uint32_t rt, uint32_t base)
{
    return (insn >> 26) == 0x23u && ((insn >> 16) & 0x1fu) == rt &&
        ((insn >> 21) & 0x1fu) == base;
}

static PixelLoopKind instructionLooksLikePixelLoop16(const uint8_t* bin, uint32_t off, uint32_t scanSize)
{
    if (off + 12 * sizeof(uint32_t) <= scanSize)
    {
        uint32_t w[12];
        for (uint32_t i = 0; i < 12; ++i)
        {
            w[i] = readLe32(bin + off + i * sizeof(uint32_t));
        }
        if (w[0] == 0x94a20000u &&
            w[1] == 0x24a50002u &&
            w[2] == 0xa4820000u &&
            w[3] == 0x24c60001u &&
            isLuiRt(w[4], 2) &&
            isLwRtBase(w[5], 2, 2) &&
            isLuiRt(w[6], 3) &&
            isLwRtBase(w[7], 3, 3) &&
            w[8] == 0x70431002u &&
            w[9] == 0x00c2102au &&
            w[10] == 0x1440fff5u &&
            w[11] == 0x24840002u)
        {
            return PIXEL_LOOP_SEQUENTIAL_COPY16;
        }
    }

    if (off + 24 * sizeof(uint32_t) <= scanSize)
    {
        uint32_t w[24];
        for (uint32_t i = 0; i < 24; ++i)
        {
            w[i] = readLe32(bin + off + i * sizeof(uint32_t));
        }
        if (w[0] == 0x30a5f800u &&
            w[1] == 0x000630c0u &&
            w[2] == 0x30c607e0u &&
            w[3] == 0x00a62825u &&
            w[4] == 0x000738c2u &&
            w[5] == 0x00a73025u &&
            isLuiRt(w[6], 3) &&
            isLwRtBase(w[7], 3, 3) &&
            isLuiRt(w[8], 2) &&
            isLwRtBase(w[9], 2, 2) &&
            w[10] == 0x70621802u &&
            w[11] == 0x00032840u &&
            w[12] == 0x8c820000u &&
            w[13] == 0x00a22821u &&
            w[14] == 0x2463ffffu &&
            w[15] == 0x10600006u &&
            w[16] == 0x24a5fffeu &&
            w[17] == 0x00c01021u &&
            w[18] == 0xa4a20000u &&
            w[19] == 0x2463ffffu &&
            w[20] == 0x1460fffdu &&
            w[21] == 0x24a5fffeu &&
            w[22] == 0x03e00008u &&
            w[23] == 0x00000000u)
        {
            return PIXEL_LOOP_REPEAT_STORE16;
        }
    }

    return PIXEL_LOOP_NONE;
}

static uint32_t signExtendImm16(uint32_t value)
{
    return (value & 0x8000u) ? (value | 0xffff0000u) : value;
}

static uint32_t jalTarget(uint32_t pc, uint32_t insn)
{
    return ((pc + 4u) & 0xf0000000u) | ((insn & 0x03ffffffu) << 2);
}

static bool importOffsetHasName(
    const app* appInfo,
    uint32_t offset,
    const char* const* importNames,
    uint32_t importNameCount)
{
    if (!appInfo || !importNames)
    {
        return false;
    }

    for (uint32_t i = 0; i < appInfo->import_count; ++i)
    {
        app_import_entry* entry = appInfo->import_data[i];
        if (!entry || !entry->name || entry->offset != offset)
        {
            continue;
        }
        for (uint32_t nameIndex = 0; nameIndex < importNameCount; ++nameIndex)
        {
            if (strcmp(entry->name, importNames[nameIndex]) == 0)
            {
                return true;
            }
        }
    }
    return false;
}

static bool jalTargetsNamedImportOrWrapper(
    const uint8_t* bin,
    uint32_t scanSize,
    const app* appInfo,
    uint32_t callPc,
    uint32_t callInsn,
    const char* const* importNames,
    uint32_t importNameCount)
{
    if (!appInfo || !bin || !importNames || importNameCount == 0 || (callInsn >> 26) != 0x03u)
    {
        return false;
    }

    uint32_t target = jalTarget(callPc, callInsn);
    if (importOffsetHasName(appInfo, target, importNames, importNameCount))
    {
        return true;
    }

    if (target < appInfo->origin)
    {
        return false;
    }
    uint32_t targetOff = target - appInfo->origin;
    if (targetOff + 7u * sizeof(uint32_t) > scanSize)
    {
        return false;
    }

    uint32_t w[7];
    for (uint32_t i = 0; i < sizeof(w) / sizeof(w[0]); ++i)
    {
        w[i] = readLe32(bin + targetOff + i * sizeof(uint32_t));
    }

    return w[0] == 0x27bdffe8u &&
        w[1] == 0xafbf0010u &&
        (w[2] >> 26) == 0x03u &&
        importOffsetHasName(appInfo, jalTarget(target + 0x08u, w[2]), importNames, importNameCount) &&
        w[3] == 0x00000000u &&
        w[4] == 0x8fbf0010u &&
        w[5] == 0x03e00008u &&
        w[6] == 0x27bd0018u;
}

static uint32_t addressFromLuiLoad(uint32_t luiInsn, uint32_t loadInsn)
{
    return ((luiInsn & 0xffffu) << 16) + signExtendImm16(loadInsn & 0xffffu);
}

static bool readInstructionAddressPair(NativeRuntime* runtime, uint32_t pc, uint32_t luiOffset, uint32_t lwOffset, uint32_t* out)
{
    uint32_t luiInsn = 0;
    uint32_t lwInsn = 0;
    if (!out ||
        !readRuntimeInsn(runtime, pc + luiOffset, &luiInsn) ||
        !readRuntimeInsn(runtime, pc + lwOffset, &lwInsn))
    {
        return false;
    }
    *out = addressFromLuiLoad(luiInsn, lwInsn);
    return true;
}

static bool isCompatBreakSkipAddress(uint32_t address)
{
    return address >= 0x80A29708 && address < 0x80A2ADD4;
}

static void writeLe32(uint8_t* data, uint32_t value)
{
    data[0] = (uint8_t)(value & 0xFF);
    data[1] = (uint8_t)((value >> 8) & 0xFF);
    data[2] = (uint8_t)((value >> 16) & 0xFF);
    data[3] = (uint8_t)((value >> 24) & 0xFF);
}

static void patchCacheInstructions(app* appInfo, const EmulatorOptions& options, uint32_t* patchedCache)
{
    *patchedCache = 0;

    uint32_t scanSize = appInfo->prog_size;
    if (scanSize > appInfo->bin_size)
    {
        scanSize = appInfo->bin_size;
    }

    uint8_t* bin = (uint8_t*)appInfo->bin_data;
    for (uint32_t off = 0; off + 4 <= scanSize; off += 4)
    {
        uint32_t insn = readLe32(bin + off);
        if ((insn & 0xFC000000) == 0xBC000000)
        {
            // Packed resources may contain cache-op bit patterns. Leave the image
            // unchanged and let the precise PC hook skip real executed cache ops.
            (*patchedCache)++;
        }
    }

    if (options.profile)
    {
        printf("profile compat: cache-like words=%u\n", *patchedCache);
    }
}

static bool readRuntimeInsn(NativeRuntime* runtime, uint64_t address, uint32_t* out)
{
    if (address >= s_AppDataAddr && address + sizeof(uint32_t) <= (uint64_t)s_AppDataAddr + s_AppDataBuffSize)
    {
        uint64_t offset = address - s_AppDataAddr;
        *out = readLe32((const uint8_t*)s_AppDataBuff + offset);
        return true;
    }

    return nativeRuntimeReadMemory(runtime, address, out, sizeof(*out)) == RUNTIME_OK;
}

static void profileRuntimeCompat(bool breakInsn, bool cacheInsn)
{
    if (!g_options.profile)
    {
        return;
    }

    static uint64_t lastTicks = 0;
    static uint64_t callbacks = 0;
    static uint64_t breakHits = 0;
    static uint64_t cacheHits = 0;

    uint64_t now = SDL_GetTicks64();
    if (!lastTicks)
    {
        lastTicks = now;
    }

    callbacks++;
    if (breakInsn)
    {
        breakHits++;
    }
    if (cacheInsn)
    {
        cacheHits++;
    }

    if (now - lastTicks >= 1000)
    {
        printf("profile compat: callbacks=%llu/s break=%llu/s cache=%llu/s\n",
            (unsigned long long)callbacks,
            (unsigned long long)breakHits,
            (unsigned long long)cacheHits);
        callbacks = 0;
        breakHits = 0;
        cacheHits = 0;
        lastTicks = now;
    }
}

static uint32_t readRegister32(NativeRuntime* runtime, int regid)
{
    uint32_t value = 0;
    nativeRuntimeReadRegister(runtime, regid, &value);
    return value;
}

static void traceMemoryWindow(NativeRuntime* runtime, uint32_t address)
{
    printf("compat-trace: code");
    for (int offset = -16; offset <= 16; offset += 4)
    {
        uint32_t value = 0;
        uint32_t pc = address + offset;
        if (readRuntimeInsn(runtime, pc, &value))
        {
            printf(" [%+d]=0x%08x", offset, value);
        }
        else
        {
            printf(" [%+d]=<unreadable>", offset);
        }
    }
    printf("\n");
}

static void traceRegisterSnapshot(NativeRuntime* runtime, uint32_t address, uint32_t insn)
{
    uint32_t sp = readRegister32(runtime, RUNTIME_REG_SP);
    uint32_t stack[4] = {};
    bool hasStack = nativeRuntimeReadMemory(runtime, sp, stack, sizeof(stack)) == RUNTIME_OK;

    printf("compat-trace: address=0x%08x insn=0x%08x type=%s pc=0x%08x ra=0x%08x sp=0x%08x "
        "v0=0x%08x v1=0x%08x a0=0x%08x a1=0x%08x a2=0x%08x a3=0x%08x "
        "t0=0x%08x t1=0x%08x t2=0x%08x t3=0x%08x t4=0x%08x t5=0x%08x t6=0x%08x t7=0x%08x "
        "t8=0x%08x t9=0x%08x hi=0x%08x lo=0x%08x\n",
        address,
        insn,
        insn == 0x000001CD ? "break" : "cache",
        readRegister32(runtime, RUNTIME_REG_PC),
        readRegister32(runtime, RUNTIME_REG_RA),
        sp,
        readRegister32(runtime, RUNTIME_REG_V0),
        readRegister32(runtime, RUNTIME_REG_V1),
        readRegister32(runtime, RUNTIME_REG_A0),
        readRegister32(runtime, RUNTIME_REG_A1),
        readRegister32(runtime, RUNTIME_REG_A2),
        readRegister32(runtime, RUNTIME_REG_A3),
        readRegister32(runtime, RUNTIME_REG_T0),
        readRegister32(runtime, RUNTIME_REG_T1),
        readRegister32(runtime, RUNTIME_REG_T2),
        readRegister32(runtime, RUNTIME_REG_T3),
        readRegister32(runtime, RUNTIME_REG_T4),
        readRegister32(runtime, RUNTIME_REG_T5),
        readRegister32(runtime, RUNTIME_REG_T6),
        readRegister32(runtime, RUNTIME_REG_T7),
        readRegister32(runtime, RUNTIME_REG_T8),
        readRegister32(runtime, RUNTIME_REG_T9),
        readRegister32(runtime, RUNTIME_REG_HI),
        readRegister32(runtime, RUNTIME_REG_LO));

    if (hasStack)
    {
        printf("compat-trace: stack [0]=0x%08x [4]=0x%08x [8]=0x%08x [12]=0x%08x\n",
            stack[0], stack[1], stack[2], stack[3]);
    }
    else
    {
        printf("compat-trace: stack <unreadable>\n");
    }

    traceMemoryWindow(runtime, address);
}

static void traceRuntimeCompat(NativeRuntime* runtime, uint32_t address, uint32_t insn)
{
    if (!g_options.compatTrace || !instructionNeedsCompatHook(insn))
    {
        return;
    }

    static uint32_t traced[2048];
    static uint32_t tracedCount = 0;
    for (uint32_t i = 0; i < tracedCount; ++i)
    {
        if (traced[i] == address)
        {
            return;
        }
    }

    if (tracedCount < sizeof(traced) / sizeof(traced[0]))
    {
        traced[tracedCount++] = address;
    }

    traceRegisterSnapshot(runtime, address, insn);
}

static void hookRuntimeCompat(NativeRuntime* runtime, uint64_t address, uint32_t size, void* userData)
{
    (void)size;
    (void)userData;

    uint32_t insn = 0;
    if (!readRuntimeInsn(runtime, address, &insn))
    {
        return;
    }

    uint32_t nextPc = (uint32_t)address + 4;
    traceRuntimeCompat(runtime, (uint32_t)address, insn);

    if (insn == 0x000001CD)
    {
        profileRuntimeCompat(true, false);
        if (isCompatBreakSkipAddress((uint32_t)address))
        {
            nextPc = 0x80A2ADD4;
        }

        nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &nextPc);
        return;
    }

    if ((insn & 0xFC000000) == 0xBC000000)
    {
        profileRuntimeCompat(false, true);
        nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &nextPc);
        return;
    }

    profileRuntimeCompat(false, false);
}

static void hookTransparentBlit16(NativeRuntime* runtime, uint64_t address, uint32_t size, void* userData)
{
    (void)address;
    (void)size;
    (void)userData;

    // Transparent RGB565 blit loops can dominate frame time on some software
    // rendered samples. Install this helper only for the exact known pattern.
    uint32_t objectPtr = 0;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &objectPtr);

    uint8_t* object = (uint8_t*)toHostPtr(objectPtr);
    if (!object)
    {
        return;
    }

    uint32_t destStridePixels = readLe32(object + 0x00);
    uint32_t sourceHeaderPtr = readLe32(object + 0x0c);
    uint32_t transparent = (uint32_t)(object[0x18] | ((uint32_t)object[0x19] << 8));
    uint32_t sourcePtr = readLe32(object + 0x24);
    uint32_t destPtr = readLe32(object + 0x28);
    uint32_t x0 = readLe32(object + 0x34);
    uint32_t y0 = readLe32(object + 0x38);
    uint32_t x1 = readLe32(object + 0x3c);
    uint32_t y1 = readLe32(object + 0x40);

    if (x1 < x0 || y1 < y0)
    {
        return;
    }

    uint32_t columns = x1 - x0;
    uint32_t rows = y1 - y0;
    if (columns > 4096 || rows > 4096)
    {
        return;
    }

    uint8_t* sourceHeader = (uint8_t*)toHostPtr(sourceHeaderPtr);
    uint16_t* source = (uint16_t*)toHostPtr(sourcePtr);
    uint16_t* dest = (uint16_t*)toHostPtr(destPtr);
    if (!sourceHeader || !source || !dest)
    {
        return;
    }

    uint32_t sourceStridePixels = (uint32_t)(sourceHeader[0] | ((uint32_t)sourceHeader[1] << 8));
    for (uint32_t row = 0; row < rows; ++row)
    {
        for (uint32_t i = columns; i > 0; --i)
        {
            uint32_t x = i - 1;
            uint16_t value = source[x];
            if (value != transparent)
            {
                dest[x] = value;
            }
        }
        source += sourceStridePixels;
        dest += destStridePixels;
    }

    uint32_t updatedSource = sourcePtr + sourceStridePixels * rows * sizeof(uint16_t);
    uint32_t updatedDest = destPtr + destStridePixels * rows * sizeof(uint16_t);
    writeLe32(object + 0x24, updatedSource);
    writeLe32(object + 0x28, updatedDest);

    uint32_t returnPc = 0;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &returnPc);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &returnPc);
}

static uint32_t readRegister32(NativeRuntime* runtime, int regid);

static uint32_t readGuestLe32(uint32_t address)
{
    uint8_t* ptr = (uint8_t*)toHostPtr(address);
    return ptr ? readLe32(ptr) : 0;
}

static bool isKnownObjectFlagsPredicateAddress(uint32_t address)
{
    for (size_t i = 0; i < g_objectFlagsPredicateAddresses.size(); ++i)
    {
        if (g_objectFlagsPredicateAddresses[i] == address)
        {
            return true;
        }
    }
    return false;
}

static bool evalObjectFlagsPredicate(const uint8_t* object, uint32_t key, uint32_t mask, uint32_t* out)
{
    if (!object || !out)
    {
        return false;
    }

    uint32_t ret = 0;
    if ((uint32_t)(object[6] | ((uint32_t)object[7] << 8)) != 0 &&
        object[0x0a] == (uint8_t)key)
    {
        ret = 1u < (uint32_t)(object[0x0b] ^ (uint8_t)mask) ? 1u : 0u;
    }
    *out = ret;
    return true;
}

static bool evalObjectFlagsCallback(uint32_t objectPtr, uint32_t key, uint32_t mask, uint32_t* out)
{
    uint8_t* object = (uint8_t*)toHostPtr(objectPtr);
    if (!object)
    {
        return false;
    }

    uint32_t tablePtr = readLe32(object);
    uint8_t* table = (uint8_t*)toHostPtr(tablePtr + 0x18u);
    if (!table)
    {
        return false;
    }

    uint32_t callback = readLe32(table);
    if (!isKnownObjectFlagsPredicateAddress(callback))
    {
        return false;
    }

    return evalObjectFlagsPredicate(object, key, mask, out);
}

static void hookObjectPredicateAggregate(NativeRuntime* runtime, uint64_t address, uint32_t size, void* userData)
{
    (void)address;
    (void)size;
    ObjectPredicateAggregateKind kind = (ObjectPredicateAggregateKind)(uintptr_t)userData;
    if (kind != OBJECT_PREDICATE_AGGREGATE_FLAGS_OR)
    {
        return;
    }

    uint32_t* gpr = nativeRuntimeGpr(runtime);
    uint32_t* pc = nativeRuntimePc(runtime);
    if (!gpr || !pc)
    {
        return;
    }

    uint32_t base = gpr[RUNTIME_REG_A0];
    uint32_t key = gpr[RUNTIME_REG_A1] & 0xffu;
    uint32_t mask = gpr[RUNTIME_REG_A2] & 0xffu;
    uint32_t result = 0;
    uint32_t value = 0;

    uint8_t* initialObject = (uint8_t*)toHostPtr(base + 0x54u);
    if (!evalObjectFlagsPredicate(initialObject, key, mask, &value))
    {
        return;
    }
    result |= value;

    for (uint32_t i = 0; i < 50; ++i)
    {
        uint32_t objectPtr = base + 0xbcu + i * 104u;
        if (!evalObjectFlagsCallback(objectPtr, key, mask, &value))
        {
            return;
        }
        result |= value;
    }

    for (uint32_t i = 0; i < 10; ++i)
    {
        uint32_t objectPtr = base + 0x1598u + i * 140u;
        if (!evalObjectFlagsCallback(objectPtr, key, mask, &value))
        {
            return;
        }
        result |= value;
    }

    static const uint32_t inlineOffsets[] =
    {
        0x150c, 0x1b10, 0x1b2c, 0x1b40, 0x1b60, 0x1b80, 0x1b9c, 0x1c04, 0x1c20
    };
    for (uint32_t i = 0; i < sizeof(inlineOffsets) / sizeof(inlineOffsets[0]); ++i)
    {
        uint8_t* object = (uint8_t*)toHostPtr(base + inlineOffsets[i]);
        if (!evalObjectFlagsPredicate(object, key, mask, &value))
        {
            return;
        }
        result |= value;
    }

    gpr[RUNTIME_REG_V0] = result ? 1u : 0u;
    *pc = gpr[RUNTIME_REG_RA];
}

static void hookMemoryRoutine(NativeRuntime* runtime, uint64_t address, uint32_t size, void* userData)
{
    (void)address;
    (void)size;
    MemoryRoutineKind kind = (MemoryRoutineKind)(uintptr_t)userData;

    uint32_t dstPtr = readRegister32(runtime, RUNTIME_REG_A0);
    uint32_t arg1 = readRegister32(runtime, RUNTIME_REG_A1);
    uint32_t count = readRegister32(runtime, RUNTIME_REG_A2);
    if (count > 16u * 1024u * 1024u)
    {
        return;
    }

    uint8_t* dst = NULL;
    const uint8_t* src = NULL;
    if (count > 0)
    {
        dst = (uint8_t*)toHostPtr(dstPtr);
        if (!dst)
        {
            return;
        }
    }

    switch (kind)
    {
    case MEMORY_ROUTINE_MEMCPY:
        if (count > 0)
        {
            src = (const uint8_t*)toHostPtr(arg1);
            if (!src)
            {
                return;
            }
            memmove(dst, src, count);
        }
        break;
    case MEMORY_ROUTINE_MEMSET:
        if (count > 0)
        {
            memset(dst, (int)(arg1 & 0xffu), count);
        }
        break;
    default:
        return;
    }

    trackFramebufferWrite(dstPtr, count);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &dstPtr);
    uint32_t returnPc = readRegister32(runtime, RUNTIME_REG_RA);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &returnPc);
}

static void hookTinyPredicate(NativeRuntime* runtime, uint64_t address, uint32_t size, void* userData)
{
    (void)address;
    (void)size;
    TinyPredicateKind kind = (TinyPredicateKind)(uintptr_t)userData;

    uint32_t* gpr = nativeRuntimeGpr(runtime);
    uint32_t* pc = nativeRuntimePc(runtime);
    if (!gpr || !pc)
    {
        return;
    }

    uint32_t a0 = gpr[RUNTIME_REG_A0];
    uint32_t a1 = gpr[RUNTIME_REG_A1] & 0xffu;
    uint32_t a2 = gpr[RUNTIME_REG_A2] & 0xffu;
    uint8_t* object = (uint8_t*)toHostPtr(a0);
    if (!object)
    {
        return;
    }

    uint32_t ret = 0;
    uint32_t v1 = 0;
    uint32_t a3 = gpr[RUNTIME_REG_A3];

    switch (kind)
    {
    case TINY_PREDICATE_OBJECT_FLAGS:
        // Guest code initializes v1 to zero before checking the active flag.
        // Inactive objects must therefore return false, regardless of the
        // incoming a3 value left by the caller.
        if ((uint32_t)(object[6] | ((uint32_t)object[7] << 8)) != 0)
        {
            a3 = 0;
            if (object[0x0a] == a1)
            {
                a3 = 1u < (uint32_t)(object[0x0b] ^ (uint8_t)a2) ? 1u : 0u;
            }
            v1 = a3;
            ret = v1;
        }
        break;
    case TINY_PREDICATE_TILE_BLOCKED:
    {
        uint32_t width = readLe32(object + 0x08);
        if (a1 < width && a2 < readLe32(object + 0x0c))
        {
            uint32_t cellsPtr = readLe32(object + 0x10);
            const uint8_t* cells = (const uint8_t*)toHostPtr(cellsPtr + a2 * width + a1);
            if (!cells)
            {
                return;
            }
            ret = (*cells < 4u) ? 0u : 1u;
        }
        break;
    }
    default:
        return;
    }

    gpr[RUNTIME_REG_A1] = a1;
    gpr[RUNTIME_REG_A2] = a2;
    gpr[RUNTIME_REG_A3] = a3;
    gpr[RUNTIME_REG_V1] = v1;
    gpr[RUNTIME_REG_V0] = ret;
    *pc = gpr[RUNTIME_REG_RA];
}

static void hookRowCopyLoop(NativeRuntime* runtime, uint64_t address, uint32_t size, void* userData)
{
    (void)size;
    RowCopyLoopKind kind = (RowCopyLoopKind)(uintptr_t)userData;
    if (kind == ROW_COPY_LOOP_LINEAR_RGB565_FRAME)
    {
        uint32_t* gpr = nativeRuntimeGpr(runtime);
        uint32_t* pc = nativeRuntimePc(runtime);
        if (!gpr || !pc)
        {
            return;
        }

        uint32_t objectPtr = gpr[RUNTIME_REG_A0];
        uint8_t* object = (uint8_t*)toHostPtr(objectPtr);
        if (!object)
        {
            return;
        }

        if (object[0x08] == 0)
        {
            gpr[RUNTIME_REG_V0] = 0;
            *pc = gpr[RUNTIME_REG_RA];
            return;
        }

        static const uint32_t kLinearRgb565FramePixels = 320u * 240u;
        static const uint32_t kLinearRgb565FrameBytes = kLinearRgb565FramePixels * sizeof(uint16_t);
        uint32_t sourcePtr = readLe32(object + 0x04);
        uint32_t destPtr = _lcd_get_frame();
        const uint8_t* source = nativeRuntimeHostPointer(runtime, sourcePtr, kLinearRgb565FrameBytes);
        uint8_t* dest = nativeRuntimeHostPointer(runtime, destPtr, kLinearRgb565FrameBytes);
        if (!source || !dest)
        {
            return;
        }

        memmove(dest, source, kLinearRgb565FrameBytes);
        trackFramebufferWrite(destPtr, kLinearRgb565FrameBytes);
        requestFbUpdate();

        writeLe32(object + 0x0c, destPtr);
        object[0x08] = 1;
        gpr[RUNTIME_REG_A0] = sourcePtr + kLinearRgb565FrameBytes;
        gpr[RUNTIME_REG_A1] = kLinearRgb565FramePixels;
        gpr[RUNTIME_REG_A2] = 0x12bffu;
        gpr[RUNTIME_REG_V0] = 1;
        gpr[RUNTIME_REG_V1] = destPtr + kLinearRgb565FrameBytes;
        *pc = gpr[RUNTIME_REG_RA];
        return;
    }

    if (kind != ROW_COPY_LOOP_RGB565)
    {
        return;
    }

    uint32_t objectPtr = readRegister32(runtime, RUNTIME_REG_A0);
    uint8_t* object = (uint8_t*)toHostPtr(objectPtr);
    if (!object)
    {
        return;
    }

    uint32_t y0 = readLe32(object + 0x2c);
    uint32_t y1 = readLe32(object + 0x34);
    uint32_t x0 = readLe32(object + 0x28);
    uint32_t x1 = readLe32(object + 0x30);
    if (x1 < x0 || y1 < y0)
    {
        return;
    }

    uint32_t columns = x1 - x0;
    uint32_t rows = y1 - y0;
    if (columns > 4096 || rows > 4096)
    {
        return;
    }

    uint32_t sourceHeaderPtr = readLe32(object + 0x04);
    const uint8_t* sourceHeader = (const uint8_t*)toHostPtr(sourceHeaderPtr);
    if (!sourceHeader)
    {
        return;
    }

    uint32_t sourceStrideBytes =
        (uint32_t)(sourceHeader[0] | ((uint32_t)sourceHeader[1] << 8)) * sizeof(uint16_t);
    uint32_t destStridePixels = 0;
    uint32_t strideAddr = 0;
    if (readInstructionAddressPair(runtime, (uint32_t)address, 0x58, 0x5c, &strideAddr))
    {
        destStridePixels = readGuestLe32(strideAddr);
    }
    if (sourceStrideBytes > 32768 || destStridePixels > 16384)
    {
        return;
    }

    uint32_t destStrideBytes = destStridePixels * sizeof(uint16_t);
    uint32_t rowBytes = columns * sizeof(uint16_t);
    uint32_t sourcePtr = readLe32(object + 0x18);
    uint32_t destPtr = readLe32(object + 0x1c);
    if (rows > 0 && rowBytes > 0)
    {
        uint8_t* dest = (uint8_t*)toHostPtr(destPtr);
        const uint8_t* source = (const uint8_t*)toHostPtr(sourcePtr);
        if (!dest || !source)
        {
            return;
        }

        for (uint32_t row = 0; row < rows; ++row)
        {
            memmove(dest, source, rowBytes);
            source += sourceStrideBytes;
            dest += destStrideBytes;
        }
    }

    uint32_t updatedSource = sourcePtr + sourceStrideBytes * rows;
    uint32_t updatedDest = destPtr + destStrideBytes * rows;
    writeLe32(object + 0x18, updatedSource);
    writeLe32(object + 0x1c, updatedDest);
    trackFramebufferWrite(destPtr, destStrideBytes * rows);

    uint32_t returnPc = readRegister32(runtime, RUNTIME_REG_RA);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &returnPc);
}

static void hookCompactTransparentBlit16(NativeRuntime* runtime, uint64_t address, uint32_t size, void* userData)
{
    (void)size;
    CompactTransparentBlit16Kind kind = (CompactTransparentBlit16Kind)(uintptr_t)userData;

    uint32_t objectPtr = readRegister32(runtime, RUNTIME_REG_A0);
    uint8_t* object = (uint8_t*)toHostPtr(objectPtr);
    if (!object)
    {
        return;
    }

    uint32_t x0 = readLe32(object + 0x28);
    uint32_t y0 = readLe32(object + 0x2c);
    uint32_t x1 = readLe32(object + 0x30);
    uint32_t y1 = readLe32(object + 0x34);
    if (x1 < x0 || y1 < y0)
    {
        return;
    }

    uint32_t columns = x1 - x0;
    uint32_t rows = y1 - y0;
    if (rows > 4096 || columns > 4096)
    {
        return;
    }

    uint32_t sourceHeaderPtr = readLe32(object + 0x04);
    uint32_t sourcePtr = readLe32(object + 0x18);
    uint32_t destPtr = readLe32(object + 0x1c);
    const uint8_t* sourceHeader = (const uint8_t*)toHostPtr(sourceHeaderPtr);
    if (!sourceHeader)
    {
        return;
    }

    uint32_t sourceStridePixels = (uint32_t)(sourceHeader[0] | ((uint32_t)sourceHeader[1] << 8));
    uint32_t destStridePixels = 0;
    uint32_t strideAddr = 0;
    if (readInstructionAddressPair(runtime, (uint32_t)address, 0x78, 0x7c, &strideAddr))
    {
        destStridePixels = readGuestLe32(strideAddr);
    }
    if (sourceStridePixels > 16384 || destStridePixels > 16384)
    {
        return;
    }

    const uint16_t* source = NULL;
    uint16_t* dest = NULL;
    if (columns > 0 && rows > 0)
    {
        source = (const uint16_t*)toHostPtr(sourcePtr);
        dest = (uint16_t*)toHostPtr(destPtr);
        if (!source || !dest)
        {
            return;
        }
    }

    uint16_t transparent = (uint16_t)(object[0x0e] | ((uint16_t)object[0x0f] << 8));
    for (uint32_t row = 0; row < rows; ++row)
    {
        if (kind == COMPACT_TRANSPARENT_BLIT16_REVERSE_COPY)
        {
            for (uint32_t i = columns; i > 0; --i)
            {
                uint32_t x = i - 1;
                uint16_t color = source[x];
                if (color != transparent)
                {
                    dest[x] = color;
                }
            }
        }
        else if (kind == COMPACT_TRANSPARENT_BLIT16_HFLIP)
        {
            for (uint32_t i = columns; i > 0; --i)
            {
                uint32_t dstX = i - 1;
                uint32_t srcX = columns - i;
                uint16_t color = source[srcX];
                if (color != transparent)
                {
                    dest[dstX] = color;
                }
            }
        }
        else
        {
            return;
        }
        source += sourceStridePixels;
        dest += destStridePixels;
    }

    uint32_t updatedSource = sourcePtr + sourceStridePixels * rows * sizeof(uint16_t);
    uint32_t updatedDest = destPtr + destStridePixels * rows * sizeof(uint16_t);
    writeLe32(object + 0x18, updatedSource);
    writeLe32(object + 0x1c, updatedDest);
    trackFramebufferWrite(destPtr, destStridePixels * rows * sizeof(uint16_t));

    uint32_t returnPc = readRegister32(runtime, RUNTIME_REG_RA);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &returnPc);
}

static void hookIndexedBlit8ToRgb565(NativeRuntime* runtime, uint64_t address, uint32_t size, void* userData)
{
    (void)address;
    (void)size;
    (void)userData;

    uint32_t objectPtr = readRegister32(runtime, RUNTIME_REG_A0);
    uint8_t* object = (uint8_t*)toHostPtr(objectPtr);
    if (!object)
    {
        return;
    }

    uint32_t x0 = readLe32(object + 0x28);
    uint32_t y0 = readLe32(object + 0x2c);
    uint32_t x1 = readLe32(object + 0x30);
    uint32_t y1 = readLe32(object + 0x34);
    if (x1 < x0 || y1 < y0)
    {
        return;
    }

    uint32_t columns = x1 - x0;
    uint32_t rows = y1 - y0;
    if (columns == 0 || rows == 0 || columns > 4096 || rows > 4096)
    {
        return;
    }

    uint32_t palettePtr = readLe32(object + 0x18);
    uint32_t destPtr = readLe32(object + 0x1c);
    uint32_t sourcePtr = readLe32(object + 0x24);
    uint32_t sourceHeaderPtr = readLe32(object + 0x04);
    uint16_t transparent = (uint16_t)(object[0x0e] | ((uint16_t)object[0x0f] << 8));

    uint16_t* palette = (uint16_t*)toHostPtr(palettePtr);
    uint16_t* dest = (uint16_t*)toHostPtr(destPtr);
    const uint8_t* source = (const uint8_t*)toHostPtr(sourcePtr);
    const uint8_t* sourceHeader = (const uint8_t*)toHostPtr(sourceHeaderPtr);
    if (!palette || !dest || !source || !sourceHeader)
    {
        return;
    }

    uint32_t sourceStride = (uint32_t)(sourceHeader[0] | ((uint32_t)sourceHeader[1] << 8));
    uint32_t destStride = 0;
    uint32_t strideAddr = 0;
    if (readInstructionAddressPair(runtime, (uint32_t)address, 0x74, 0x78, &strideAddr))
    {
        destStride = readGuestLe32(strideAddr);
    }
    if (sourceStride == 0 || destStride == 0 || sourceStride > 16384 || destStride > 16384)
    {
        return;
    }

    for (uint32_t row = 0; row < rows; ++row)
    {
        for (uint32_t x = 0; x < columns; ++x)
        {
            uint16_t color = palette[source[x]];
            if (color != transparent)
            {
                dest[x] = color;
            }
        }
        source += sourceStride;
        dest += destStride;
    }

    uint32_t updatedDest = destPtr + destStride * rows * sizeof(uint16_t);
    uint32_t updatedSource = sourcePtr + sourceStride * rows;
    writeLe32(object + 0x1c, updatedDest);
    writeLe32(object + 0x24, updatedSource);
    trackFramebufferWrite(destPtr, destStride * rows * sizeof(uint16_t));

    uint32_t returnPc = readRegister32(runtime, RUNTIME_REG_RA);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &returnPc);
}

static void hookIndexedTransformBlit16(NativeRuntime* runtime, uint64_t address, uint32_t size, void* userData)
{
    (void)address;
    (void)size;
    IndexedTransformBlit16Kind kind = (IndexedTransformBlit16Kind)(uintptr_t)userData;
    if (kind != INDEXED_TRANSFORM_BLIT16_320X240)
    {
        return;
    }

    uint32_t* gpr = nativeRuntimeGpr(runtime);
    uint32_t* pc = nativeRuntimePc(runtime);
    if (!gpr || !pc)
    {
        return;
    }

    uint32_t objectPtr = gpr[RUNTIME_REG_A0];
    uint32_t sourcePtr = gpr[RUNTIME_REG_A1];
    uint32_t destPtr = gpr[RUNTIME_REG_A2];
    static const uint32_t kColumns = 320u;
    static const uint32_t kRows = 240u;
    static const uint32_t kSourceBytes = kColumns * kRows * sizeof(uint32_t);
    static const uint32_t kDestBytes = kColumns * kRows * sizeof(uint16_t);
    uint8_t* object = (uint8_t*)toHostPtr(objectPtr);
    const uint8_t* source = nativeRuntimeHostPointer(runtime, sourcePtr, kSourceBytes);
    uint16_t* dest = (uint16_t*)nativeRuntimeHostPointer(runtime, destPtr, kDestBytes);
    if (!object || !source || !dest)
    {
        return;
    }

    uint32_t lowPalettePtr = readLe32(object + 0x08);
    if (!lowPalettePtr)
    {
        return;
    }

    const uint32_t lowThreshold = readLe32(object + 0x0c);
    const uint32_t highThreshold = readLe32(object + 0x10);
    uint32_t lowIndexTablePtr = readLe32(object + 0x04);
    uint32_t highIndexTablePtr = readLe32(object + 0x14);
    uint32_t highPalettePtr = readLe32(object + 0x18);
    const uint8_t* lowIndexTable = nativeRuntimeHostPointer(runtime, lowIndexTablePtr, 65536u);
    const uint16_t* lowPalette = (const uint16_t*)nativeRuntimeHostPointer(runtime, lowPalettePtr, 512u);
    const uint8_t* highIndexTable = NULL;
    const uint16_t* highPalette = NULL;
    if (highThreshold < 0xffffu)
    {
        highIndexTable = nativeRuntimeHostPointer(runtime, highIndexTablePtr, 0x10000u - highThreshold);
        highPalette = (const uint16_t*)nativeRuntimeHostPointer(runtime, highPalettePtr, 0x100000u * sizeof(uint16_t));
        if (!highIndexTable || !highPalette)
        {
            return;
        }
    }
    if (!lowIndexTable || !lowPalette)
    {
        return;
    }

    for (uint32_t row = 0; row < kRows; ++row)
    {
        for (uint32_t column = 0; column < kColumns; ++column)
        {
            uint32_t word = readLe32(source + column * sizeof(uint32_t));
            uint32_t hi = word >> 16;
            uint16_t color = 0;
            if (hi != 0 && hi < lowThreshold)
            {
                if (highThreshold < hi)
                {
                    uint32_t low = word & 0xffffu;
                    uint16_t baseColor = lowPalette[lowIndexTable[low]];
                    uint32_t indexByte = highIndexTable[hi - highThreshold];
                    uint32_t index = (indexByte << 12) |
                        (((baseColor & 0xffffu) >> 12) << 8) |
                        (((baseColor & 0xffffu) >> 3) & 0xf0u) |
                        ((baseColor & 0x1fu) >> 1);
                    color = highPalette[index];
                }
                else
                {
                    color = (uint16_t)(object[0x28] | ((uint16_t)object[0x29] << 8));
                }
            }
            else
            {
                uint32_t low = word & 0xffffu;
                uint32_t index = lowIndexTable[low];
                color = lowPalette[index];
            }
            dest[column] = color;
        }

        source += kColumns * sizeof(uint32_t);
        dest += kColumns;
    }

    trackFramebufferWrite(destPtr, kDestBytes);
    gpr[RUNTIME_REG_A1] = sourcePtr;
    gpr[RUNTIME_REG_A2] = destPtr + kDestBytes;
    gpr[RUNTIME_REG_A3] = sourcePtr + kSourceBytes;
    gpr[RUNTIME_REG_T1] = 0;
    gpr[RUNTIME_REG_T2] = 0;
    *pc = gpr[RUNTIME_REG_RA];
}

static void hookPixelLoop16(NativeRuntime* runtime, uint64_t address, uint32_t size, void* userData)
{
    (void)address;
    (void)size;
    PixelLoopKind kind = (PixelLoopKind)(uintptr_t)userData;

    switch (kind)
    {
    case PIXEL_LOOP_SEQUENTIAL_COPY16:
    {
        uint32_t dstPtr = readRegister32(runtime, RUNTIME_REG_A0);
        uint32_t srcPtr = readRegister32(runtime, RUNTIME_REG_A1);
        uint32_t index = readRegister32(runtime, RUNTIME_REG_A2);
        uint32_t factorAddr = 0;
        uint32_t countAddr = 0;
        if (!readInstructionAddressPair(runtime, (uint32_t)address, 0x10, 0x14, &factorAddr) ||
            !readInstructionAddressPair(runtime, (uint32_t)address, 0x18, 0x1c, &countAddr))
        {
            return;
        }
        uint32_t total = readGuestLe32(factorAddr) * readGuestLe32(countAddr);
        if (total <= index || total - index > 1024u * 1024u)
        {
            return;
        }

        uint32_t count = total - index;
        uint16_t* dst = (uint16_t*)toHostPtr(dstPtr);
        const uint16_t* src = (const uint16_t*)toHostPtr(srcPtr);
        if (!dst || !src)
        {
            return;
        }

        memmove(dst, src, (size_t)count * sizeof(uint16_t));
        trackFramebufferWrite(dstPtr, count * sizeof(uint16_t));
        uint32_t updatedDst = dstPtr + count * 2u;
        uint32_t updatedSrc = srcPtr + count * 2u;
        nativeRuntimeWriteRegister(runtime, RUNTIME_REG_A0, &updatedDst);
        nativeRuntimeWriteRegister(runtime, RUNTIME_REG_A1, &updatedSrc);
        nativeRuntimeWriteRegister(runtime, RUNTIME_REG_A2, &total);
        uint32_t nextPc = (uint32_t)address + 0x30u;
        nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &nextPc);
        return;
    }
    case PIXEL_LOOP_REPEAT_STORE16:
    {
        // Match the guest RGB565 packing sequence: a1 already carries the red
        // bits, while green and blue are shifted into place before the fill.
        uint32_t red = readRegister32(runtime, RUNTIME_REG_A1) & 0xf800u;
        uint32_t green = (readRegister32(runtime, RUNTIME_REG_A2) << 3) & 0x07e0u;
        uint32_t blue = readRegister32(runtime, RUNTIME_REG_A3) >> 3;
        uint32_t color = (red | green | blue) & 0xffffu;
        uint32_t objectPtr = readRegister32(runtime, RUNTIME_REG_A0);
        uint32_t basePtr = readGuestLe32(objectPtr);
        uint32_t countAddr = 0;
        uint32_t factorAddr = 0;
        if (!readInstructionAddressPair(runtime, (uint32_t)address, 0x18, 0x1c, &countAddr) ||
            !readInstructionAddressPair(runtime, (uint32_t)address, 0x20, 0x24, &factorAddr))
        {
            return;
        }
        uint32_t count = readGuestLe32(countAddr) * readGuestLe32(factorAddr);
        if (count <= 1 || count > 1024u * 1024u)
        {
            return;
        }

        uint16_t* dst = (uint16_t*)toHostPtr(basePtr);
        if (!dst)
        {
            return;
        }

        uint32_t writes = count - 1;
        for (uint32_t i = 0; i < writes; ++i)
        {
            dst[count - 1 - i] = (uint16_t)color;
        }
        trackFramebufferWrite(basePtr + sizeof(uint16_t), writes * sizeof(uint16_t));
        uint32_t finalA1 = basePtr;
        uint32_t zero = 0;
        uint32_t finalA3 = readRegister32(runtime, RUNTIME_REG_A3) >> 3;
        nativeRuntimeWriteRegister(runtime, RUNTIME_REG_A1, &finalA1);
        nativeRuntimeWriteRegister(runtime, RUNTIME_REG_A2, &color);
        nativeRuntimeWriteRegister(runtime, RUNTIME_REG_A3, &finalA3);
        nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V1, &zero);
        nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &color);
        uint32_t returnPc = readRegister32(runtime, RUNTIME_REG_RA);
        nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &returnPc);
        return;
    }
    default:
        return;
    }
}

RuntimeError runtimeCompatInstallHooks(NativeRuntime* runtime, app* appInfo, const EmulatorOptions& options)
{
    g_options = options;
    g_objectFlagsPredicateAddresses.clear();

    uint32_t scanSize = appInfo->prog_size;
    if (scanSize > appInfo->bin_size)
    {
        scanSize = appInfo->bin_size;
    }

    uint32_t patchedCache = 0;
    patchCacheInstructions(appInfo, options, &patchedCache);

    RuntimeHook trace;
    const uint8_t* bin = (const uint8_t*)appInfo->bin_data;
    uint32_t hookCount = 0;
    uint32_t blitHookCount = 0;
    uint32_t pixelLoopHookCount = 0;
    uint32_t indexedBlitHookCount = 0;
    uint32_t indexedTransformBlitHookCount = 0;
    uint32_t memoryRoutineHookCount = 0;
    uint32_t rowCopyHookCount = 0;
    uint32_t tinyPredicateHookCount = 0;
    uint32_t objectPredicateAggregateHookCount = 0;
    for (uint32_t off = 0; off + 4 <= scanSize; off += 4)
    {
        uint32_t address = appInfo->origin + off;
        uint32_t insn = readLe32(bin + off);
        TinyPredicateKind tinyPredicateKind = instructionLooksLikeTinyPredicate(bin, off, scanSize);
        if (tinyPredicateKind != TINY_PREDICATE_NONE)
        {
            RuntimeError err = nativeRuntimeAddHook(runtime, &trace, RUNTIME_HOOK_CODE,
                (void*)hookTinyPredicate, (void*)(uintptr_t)tinyPredicateKind, address, address, 0);
            if (err != RUNTIME_OK)
            {
                return err;
            }
            tinyPredicateHookCount++;
            if (tinyPredicateKind == TINY_PREDICATE_OBJECT_FLAGS)
            {
                g_objectFlagsPredicateAddresses.push_back(address);
            }
        }

        ObjectPredicateAggregateKind objectPredicateAggregateKind =
            instructionLooksLikeObjectPredicateAggregate(bin, off, scanSize);
        if (objectPredicateAggregateKind != OBJECT_PREDICATE_AGGREGATE_NONE)
        {
            RuntimeError err = nativeRuntimeAddHook(runtime, &trace, RUNTIME_HOOK_CODE,
                (void*)hookObjectPredicateAggregate, (void*)(uintptr_t)objectPredicateAggregateKind, address, address, 0);
            if (err != RUNTIME_OK)
            {
                return err;
            }
            objectPredicateAggregateHookCount++;
        }

        RowCopyLoopKind rowCopyLoopKind = instructionLooksLikeRowCopyLoop(bin, off, scanSize, address, appInfo);
        if (rowCopyLoopKind != ROW_COPY_LOOP_NONE)
        {
            RuntimeError err = nativeRuntimeAddHook(runtime, &trace, RUNTIME_HOOK_CODE,
                (void*)hookRowCopyLoop, (void*)(uintptr_t)rowCopyLoopKind, address, address, 0);
            if (err != RUNTIME_OK)
            {
                return err;
            }
            rowCopyHookCount++;
        }

        MemoryRoutineKind memoryRoutineKind = instructionLooksLikeMemoryRoutine(bin, off, scanSize);
        if (memoryRoutineKind != MEMORY_ROUTINE_NONE)
        {
            RuntimeError err = nativeRuntimeAddHook(runtime, &trace, RUNTIME_HOOK_CODE,
                (void*)hookMemoryRoutine, (void*)(uintptr_t)memoryRoutineKind, address, address, 0);
            if (err != RUNTIME_OK)
            {
                return err;
            }
            memoryRoutineHookCount++;
        }

        CompactTransparentBlit16Kind compactTransparentBlit16Kind =
            instructionLooksLikeCompactTransparentBlit16(bin, off, scanSize);
        if (compactTransparentBlit16Kind != COMPACT_TRANSPARENT_BLIT16_NONE)
        {
            RuntimeError err = nativeRuntimeAddHook(runtime, &trace, RUNTIME_HOOK_CODE,
                (void*)hookCompactTransparentBlit16, (void*)(uintptr_t)compactTransparentBlit16Kind,
                address, address, 0);
            if (err != RUNTIME_OK)
            {
                return err;
            }
            blitHookCount++;
        }

        if (instructionLooksLikeIndexedBlit8ToRgb565(bin, off, scanSize))
        {
            RuntimeError err = nativeRuntimeAddHook(runtime, &trace, RUNTIME_HOOK_CODE,
                (void*)hookIndexedBlit8ToRgb565, NULL, address, address, 0);
            if (err != RUNTIME_OK)
            {
                return err;
            }
            indexedBlitHookCount++;
        }

        IndexedTransformBlit16Kind indexedTransformBlitKind =
            instructionLooksLikeIndexedTransformBlit16(bin, off, scanSize);
        if (indexedTransformBlitKind != INDEXED_TRANSFORM_BLIT16_NONE)
        {
            RuntimeError err = nativeRuntimeAddHook(runtime, &trace, RUNTIME_HOOK_CODE,
                (void*)hookIndexedTransformBlit16, (void*)(uintptr_t)indexedTransformBlitKind, address, address, 0);
            if (err != RUNTIME_OK)
            {
                return err;
            }
            indexedTransformBlitHookCount++;
        }

        PixelLoopKind pixelLoopKind = instructionLooksLikePixelLoop16(bin, off, scanSize);
        if (pixelLoopKind != PIXEL_LOOP_NONE)
        {
            RuntimeError err = nativeRuntimeAddHook(runtime, &trace, RUNTIME_HOOK_CODE, (void*)hookPixelLoop16,
                (void*)(uintptr_t)pixelLoopKind, address, address, 0);
            if (err != RUNTIME_OK)
            {
                return err;
            }
            pixelLoopHookCount++;
        }

        if (instructionLooksLikeTransparentBlit16(bin, off, scanSize))
        {
            RuntimeError err = nativeRuntimeAddHook(runtime, &trace, RUNTIME_HOOK_CODE, (void*)hookTransparentBlit16, NULL,
                address, address, 0);
            if (err != RUNTIME_OK)
            {
                return err;
            }
            blitHookCount++;
        }

        if (!instructionNeedsCompatHook(insn))
        {
            continue;
        }

        RuntimeError err = nativeRuntimeAddHook(runtime, &trace, RUNTIME_HOOK_CODE, (void*)hookRuntimeCompat, NULL,
            address, address, 0);
        if (err != RUNTIME_OK)
        {
            return err;
        }
        hookCount++;
    }

    if (options.profile)
    {
        printf("profile compat: precise hooks=%u blit16_hooks=%u indexed8_hooks=%u indexed_transform_hooks=%u pixel16_hooks=%u mem_hooks=%u rowcopy_hooks=%u tiny_hooks=%u object_predicate_hooks=%u scan_size=0x%x\n",
            hookCount, blitHookCount, indexedBlitHookCount, indexedTransformBlitHookCount, pixelLoopHookCount, memoryRoutineHookCount,
            rowCopyHookCount, tinyPredicateHookCount, objectPredicateAggregateHookCount, scanSize);
    }
    return RUNTIME_OK;
}

