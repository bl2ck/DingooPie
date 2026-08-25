#include "cc/cpu/arm32_dynarmic.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>

#include "dynarmic/interface/A32/a32.h"
#include "shared/execution/direct_memory_region.h"

namespace {

class DynarmicCallbacks final : public Dynarmic::A32::UserCallbacks
{
public:
    const Arm32Bus* bus = nullptr;
    Arm32State* state = nullptr;
    Dynarmic::A32::Jit* jit = nullptr;
    uint32_t stopPc = 0;
    uint64_t ticksRemaining = 0;
    uint64_t ticksExecuted = 0;
    Arm32RunResult result = ARM32_RUN_LIMIT;

    std::optional<uint32_t> MemoryReadCode(uint32_t address) override
    {
        if (address == stopPc)
        {
            result = ARM32_RUN_OK;
            return std::nullopt;
        }
        uint32_t value = 0;
        if (!bus || !bus->fetch ||
            !bus->fetch(bus->userData, address, &value, sizeof(value)))
        {
            result = ARM32_RUN_INVALID_MEMORY;
            return std::nullopt;
        }
        return value;
    }

    uint8_t MemoryRead8(uint32_t address) override
    {
        uint8_t value = 0;
        read(address, &value, sizeof(value));
        return value;
    }

    uint16_t MemoryRead16(uint32_t address) override
    {
        uint16_t value = 0;
        read(address, &value, sizeof(value));
        return value;
    }

    uint32_t MemoryRead32(uint32_t address) override
    {
        uint32_t value = 0;
        read(address, &value, sizeof(value));
        return value;
    }

    uint64_t MemoryRead64(uint32_t address) override
    {
        uint64_t value = 0;
        read(address, &value, sizeof(value));
        return value;
    }

    void MemoryWrite8(uint32_t address, uint8_t value) override
    {
        write(address, &value, sizeof(value));
    }

    void MemoryWrite16(uint32_t address, uint16_t value) override
    {
        write(address, &value, sizeof(value));
    }

    void MemoryWrite32(uint32_t address, uint32_t value) override
    {
        write(address, &value, sizeof(value));
    }

    void MemoryWrite64(uint32_t address, uint64_t value) override
    {
        write(address, &value, sizeof(value));
    }

    void InterpreterFallback(uint32_t, size_t instructionCount) override
    {
        syncFromJit();
        uint64_t target = state->instructions + instructionCount;
        Arm32RunResult fallback = arm32Run(state, bus, stopPc, target);
        syncToJit();
        if (fallback != ARM32_RUN_LIMIT)
        {
            result = fallback;
            jit->HaltExecution(Dynarmic::HaltReason::UserDefined2);
        }
    }

    void CallSVC(uint32_t immediate) override
    {
        syncFromJit();
        if (!bus || !bus->svc || !bus->svc(bus->userData, state, immediate))
        {
            result = ARM32_RUN_STOPPED;
            syncToJit();
            jit->HaltExecution(Dynarmic::HaltReason::UserDefined1);
            return;
        }
        syncToJit();
        if (state->r[15] == stopPc)
        {
            result = ARM32_RUN_OK;
            jit->HaltExecution(Dynarmic::HaltReason::UserDefined1);
        }
    }

    void ExceptionRaised(uint32_t address, Dynarmic::A32::Exception) override
    {
        if (address == stopPc)
        {
            result = ARM32_RUN_OK;
        }
        else if (result == ARM32_RUN_LIMIT)
        {
            result = ARM32_RUN_UNSUPPORTED;
        }
        jit->HaltExecution(Dynarmic::HaltReason::UserDefined3);
    }

    void AddTicks(uint64_t ticks) override
    {
        uint64_t consumed = std::min(ticks, ticksRemaining);
        ticksRemaining -= consumed;
        ticksExecuted += consumed;
    }

    uint64_t GetTicksRemaining() override
    {
        return ticksRemaining;
    }

    void syncFromJit()
    {
        const std::array<uint32_t, 16>& registers = jit->Regs();
        std::copy(registers.begin(), registers.end(), state->r);
        state->cpsr = jit->Cpsr();
    }

    void syncToJit()
    {
        std::array<uint32_t, 16>& registers = jit->Regs();
        std::copy(state->r, state->r + 16, registers.begin());
        jit->SetCpsr(state->cpsr);
    }

private:
    bool read(uint32_t address, void* output, size_t size)
    {
        if (bus && bus->read && bus->read(bus->userData, address, output, size))
        {
            return true;
        }
        std::memset(output, 0, size);
        result = ARM32_RUN_INVALID_MEMORY;
        if (jit) jit->HaltExecution(Dynarmic::HaltReason::MemoryAbort);
        return false;
    }

    bool write(uint32_t address, const void* input, size_t size)
    {
        if (bus && bus->write && bus->write(bus->userData, address, input, size))
        {
            return true;
        }
        result = ARM32_RUN_INVALID_MEMORY;
        if (jit) jit->HaltExecution(Dynarmic::HaltReason::MemoryAbort);
        return false;
    }
};

class DynarmicContext
{
public:
    explicit DynarmicContext(const Arm32Bus* initialBus)
        : identity(initialBus->userData), program(initialBus->directProgram),
          programBase(initialBus->directProgramBase),
          programSize(initialBus->directProgramSize)
    {
        Dynarmic::A32::UserConfig config{&callbacks};
        config.arch_version = Dynarmic::A32::ArchVersion::v5TE;
        config.always_little_endian = true;
        config.check_halt_on_memory_access = true;
        config.code_cache_size = 64 * 1024 * 1024;
        config.page_table = &pageTable;
        config.detect_misaligned_access_via_page_table = 8 | 16 | 32 | 64;
        config.only_detect_misalignment_via_page_table_on_page_boundary = true;
        jit = std::make_unique<Dynarmic::A32::Jit>(config);
        callbacks.jit = jit.get();
        rebuildPageTable(initialBus);
        std::printf("cc-arm: dynarmic initialized cache_mb=64 page_table=1\n");
    }

    bool matches(const Arm32Bus* candidate) const
    {
        return identity == candidate->userData &&
            program == candidate->directProgram &&
            programBase == candidate->directProgramBase &&
            programSize == candidate->directProgramSize;
    }

    void refreshPageTable(const Arm32Bus* bus)
    {
        std::array<DirectMemoryRegion, 5> regions = directRegions(bus);
        if (regions != mappedRegions)
        {
            rebuildPageTable(bus);
        }
    }

    void rebuildPageTable(const Arm32Bus* bus)
    {
        pageTable.fill(nullptr);
        mappedRegions = directRegions(bus);
        for (const DirectMemoryRegion& region : mappedRegions)
        {
            mapRegion(region);
        }
        if (jit) jit->ClearCache();
    }

    DynarmicCallbacks callbacks;
    std::unique_ptr<Dynarmic::A32::Jit> jit;

private:
    static std::array<DirectMemoryRegion, 5> directRegions(const Arm32Bus* bus)
    {
        return {{
            {bus->directFramebuffer, bus->directFramebufferBase,
                bus->directFramebufferSize},
            {bus->directHeap, bus->directHeapBase, bus->directHeapSize},
            {bus->directRam, bus->directRamBase, bus->directRamSize},
            {bus->directSystemRam, bus->directSystemRamBase,
                bus->directSystemRamSize},
            {bus->directStack, bus->directStackBase, bus->directStackSize}
        }};
    }

    void mapRegion(const DirectMemoryRegion& region)
    {
        if (!region.data || region.size < 4096u) return;
        uint64_t regionEnd = region.end();
        uint64_t firstPage = ((uint64_t)region.base + 4095u) & ~4095ull;
        uint64_t endPage = regionEnd & ~4095ull;
        for (uint64_t address = firstPage; address < endPage; address += 4096u)
        {
            pageTable[(uint32_t)address >> 12] =
                region.data + ((uint32_t)address - region.base);
        }
    }

    void* identity;
    uint8_t* program;
    uint32_t programBase;
    uint32_t programSize;
    std::array<DirectMemoryRegion, 5> mappedRegions{};
    std::array<uint8_t*, Dynarmic::A32::UserConfig::NUM_PAGE_TABLE_ENTRIES>
        pageTable{};
};

thread_local std::unique_ptr<DynarmicContext> s_context;

}  // namespace

Arm32RunResult arm32RunDynarmic(Arm32State* state, const Arm32Bus* bus,
    uint32_t stopPc, uint64_t instructionLimit)
{
    if (!state || !bus) return ARM32_RUN_INVALID_MEMORY;
    if (state->r[15] == stopPc) return ARM32_RUN_OK;
    if (instructionLimit && state->instructions >= instructionLimit)
    {
        return ARM32_RUN_LIMIT;
    }
    if (!s_context || !s_context->matches(bus))
    {
        s_context = std::make_unique<DynarmicContext>(bus);
    }
    s_context->refreshPageTable(bus);
    DynarmicCallbacks& callbacks = s_context->callbacks;
    callbacks.bus = bus;
    callbacks.state = state;
    callbacks.stopPc = stopPc;
    callbacks.ticksRemaining = instructionLimit ?
        instructionLimit - state->instructions : UINT64_MAX;
    callbacks.ticksExecuted = 0;
    callbacks.result = ARM32_RUN_LIMIT;
    callbacks.syncToJit();
    s_context->jit->ClearHalt(Dynarmic::HaltReason::UserDefined1 |
        Dynarmic::HaltReason::UserDefined2 |
        Dynarmic::HaltReason::UserDefined3 |
        Dynarmic::HaltReason::MemoryAbort);
    s_context->jit->Run();
    callbacks.syncFromJit();
    state->instructions += callbacks.ticksExecuted;
    if (state->r[15] == stopPc) return ARM32_RUN_OK;
    return callbacks.result;
}

void arm32DynarmicReset()
{
    s_context.reset();
}
