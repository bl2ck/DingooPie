#include "cc/save/cc_save_state.h"
#include "shared/save/save_state_format.h"
#include "shared/save/save_file_storage.h"

#include <algorithm>
#include <mutex>
#include <stdio.h>
#include <string.h>
#include <utility>

static const uint32_t kCcMaxRecords = 65536u;
static const uint32_t kCcMaxString = 4096u;
static const uint32_t kCcMaxMemory = 0x08000000u;
static const uint32_t kCcMaxPayload = 0x20000000u;
static const uint32_t kCcPayloadFlagCompressed = 1u;

static std::mutex s_ccSaveStateMutex;

static void appendRaw(std::vector<uint8_t>* output, const void* data, size_t size);
static bool readRaw(const std::vector<uint8_t>& data, size_t* offset,
    void* output, size_t size);
static bool readUint32(const std::vector<uint8_t>& data, size_t* offset,
    uint32_t* output);

static void appendUint32(std::vector<uint8_t>* output, uint32_t value)
{
    output->push_back((uint8_t)value);
    output->push_back((uint8_t)(value >> 8));
    output->push_back((uint8_t)(value >> 16));
    output->push_back((uint8_t)(value >> 24));
}

static void appendUint64(std::vector<uint8_t>* output, uint64_t value)
{
    appendUint32(output, (uint32_t)value);
    appendUint32(output, (uint32_t)(value >> 32));
}

static void appendHeader(std::vector<uint8_t>* output, const CcSaveStateFileHeader& header)
{
    appendUint32(output, header.magic);
    appendUint32(output, header.headerSize);
    appendUint32(output, header.payloadSize);
    appendUint32(output, header.payloadStoredSize);
    appendUint32(output, header.memoryFlags);
    appendUint32(output, header.taskCount);
    appendUint32(output, header.heapCount);
    appendUint32(output, header.resourceCount);
    appendUint32(output, header.streamCount);
    appendUint32(output, header.fileCount);
    appendUint32(output, header.semaphoreCount);
    appendUint32(output, header.dynamicImportCount);
    appendUint32(output, header.unknownImportCount);
    appendRaw(output, header.gameSha256, sizeof(header.gameSha256));
}

static bool readHeader(const std::vector<uint8_t>& data, CcSaveStateFileHeader* header)
{
    if (!header)
    {
        return false;
    }
    size_t offset = 0;
    uint32_t* fields[] = {
        &header->magic, &header->headerSize, &header->payloadSize,
        &header->payloadStoredSize, &header->memoryFlags, &header->taskCount,
        &header->heapCount, &header->resourceCount, &header->streamCount,
        &header->fileCount, &header->semaphoreCount,
        &header->dynamicImportCount, &header->unknownImportCount };
    for (size_t index = 0; index < sizeof(fields) / sizeof(fields[0]); ++index)
    {
        if (!readUint32(data, &offset, fields[index]))
        {
            return false;
        }
    }
    return readRaw(data, &offset, header->gameSha256, sizeof(header->gameSha256));
}

static void appendRaw(std::vector<uint8_t>* output, const void* data, size_t size)
{
    if (size == 0)
    {
        return;
    }
    const uint8_t* bytes = (const uint8_t*)data;
    output->insert(output->end(), bytes, bytes + size);
}

static void appendBytes(std::vector<uint8_t>* output, const std::vector<uint8_t>& value)
{
    appendUint32(output, (uint32_t)value.size());
    appendRaw(output, value.data(), value.size());
}

static void appendText(std::vector<uint8_t>* output, const std::string& value)
{
    appendUint32(output, (uint32_t)value.size());
    appendRaw(output, value.data(), value.size());
}

static bool readRaw(const std::vector<uint8_t>& data, size_t* offset,
    void* output, size_t size)
{
    if (!offset || !output || *offset > data.size() || size > data.size() - *offset)
    {
        return false;
    }
    memcpy(output, data.data() + *offset, size);
    *offset += size;
    return true;
}

static bool readUint32(const std::vector<uint8_t>& data, size_t* offset,
    uint32_t* output)
{
    uint8_t bytes[4];
    if (!output || !readRaw(data, offset, bytes, sizeof(bytes)))
    {
        return false;
    }
    *output = (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) |
        ((uint32_t)bytes[3] << 24);
    return true;
}

static bool readUint64(const std::vector<uint8_t>& data, size_t* offset,
    uint64_t* output)
{
    uint32_t low = 0;
    uint32_t high = 0;
    if (!output || !readUint32(data, offset, &low) ||
        !readUint32(data, offset, &high))
    {
        return false;
    }
    *output = (uint64_t)low | ((uint64_t)high << 32);
    return true;
}

static bool readBytes(const std::vector<uint8_t>& data, size_t* offset,
    std::vector<uint8_t>* output)
{
    uint32_t size = 0;
    if (!offset || !output || !readUint32(data, offset, &size) ||
        size > kCcMaxMemory || *offset > data.size() || size > data.size() - *offset)
    {
        return false;
    }
    output->assign(data.data() + *offset, data.data() + *offset + size);
    *offset += size;
    return true;
}

static bool readText(const std::vector<uint8_t>& data, size_t* offset,
    std::string* output)
{
    uint32_t size = 0;
    if (!offset || !output || !readUint32(data, offset, &size) ||
        size > kCcMaxString || *offset > data.size() || size > data.size() - *offset)
    {
        return false;
    }
    output->assign((const char*)data.data() + *offset, size);
    *offset += size;
    return true;
}

static void appendArmState(std::vector<uint8_t>* output, const Arm32State& state)
{
    appendRaw(output, state.r, sizeof(state.r));
    appendUint32(output, state.cpsr);
    appendUint64(output, state.instructions);
    appendUint32(output, state.unsupportedInstruction);
    appendUint32(output, state.unsupportedPc);
}

static bool readArmState(const std::vector<uint8_t>& data, size_t* offset,
    Arm32State* state)
{
    return state && readRaw(data, offset, state->r, sizeof(state->r)) &&
        readUint32(data, offset, &state->cpsr) &&
        readUint64(data, offset, &state->instructions) &&
        readUint32(data, offset, &state->unsupportedInstruction) &&
        readUint32(data, offset, &state->unsupportedPc);
}

static bool recordCountsValid(const CcRuntimeState& state)
{
    return state.tasks.size() <= kCcMaxRecords &&
        state.heap.size() <= kCcMaxRecords &&
        state.resources.size() <= kCcMaxRecords &&
        state.streams.size() <= kCcMaxRecords &&
        state.files.size() <= kCcMaxRecords &&
        state.semaphores.size() <= kCcMaxRecords &&
        state.dynamicImports.size() <= kCcMaxRecords &&
        state.unknownImportNames.size() <= kCcMaxRecords;
}

static void encodePayload(const CcRuntimeState& state, std::vector<uint8_t>* payload)
{
    payload->clear();
    appendUint64(payload, state.elapsedGuestMicros);
    appendUint64(payload, state.runtimeInstructions);
    appendUint32(payload, state.heapStart);
    appendUint32(payload, state.heapCursor);
    appendUint32(payload, state.dvcAudioHandle);
    appendUint32(payload, state.dvcAudioSampleRate);
    appendUint32(payload, state.dvcAudioVolume);
    appendUint32(payload, state.framebufferAddress);
    appendUint32(payload, state.framebufferBits);
    appendRaw(payload, state.framebufferWriteHighWater,
        sizeof(state.framebufferWriteHighWater));
    appendUint32(payload, state.framebufferBitsExplicit ? 1u : 0u);
    appendUint32(payload, 0u);
    appendUint32(payload, state.dvcAudioStarted ? 1u : 0u);
    appendBytes(payload, state.ram);
    appendBytes(payload, state.systemMemory);
    appendBytes(payload, state.stack);
    appendBytes(payload, state.heapMemory);
    appendBytes(payload, state.legacyLowMemory);
    appendBytes(payload, state.framebuffer);
    appendBytes(payload, state.legacyMmio);
    appendBytes(payload, state.legacyAudioMmio);
    appendBytes(payload, state.legacySystemMmio);
    for (size_t index = 0; index < state.heap.size(); ++index)
    {
        const CcSaveHeapBlock& block = state.heap[index];
        appendUint32(payload, block.address);
        appendUint32(payload, block.size);
        appendUint32(payload, block.free ? 1u : 0u);
    }
    for (size_t index = 0; index < state.tasks.size(); ++index)
    {
        const CcSaveTask& task = state.tasks[index];
        appendArmState(payload, task.state);
        appendUint32(payload, task.entry);
        appendUint32(payload, task.argument);
        appendUint32(payload, task.stack);
        appendUint32(payload, task.priority);
        appendUint32(payload, task.delayTicks);
        appendUint32(payload, task.started ? 1u : 0u);
        appendUint32(payload, task.finished ? 1u : 0u);
        appendUint32(payload, task.audioProducer ? 1u : 0u);
    }
    for (size_t index = 0; index < state.resources.size(); ++index)
    {
        const CcSaveResource& resource = state.resources[index];
        appendUint32(payload, resource.address);
        appendText(payload, resource.name);
        appendUint32(payload, resource.position);
        appendUint32(payload, resource.dataAddress);
    }
    for (size_t index = 0; index < state.streams.size(); ++index)
    {
        const CcSaveFileStream& stream = state.streams[index];
        appendUint32(payload, stream.stream);
        appendText(payload, stream.requestName);
        appendUint32(payload, stream.position);
    }
    for (size_t index = 0; index < state.files.size(); ++index)
    {
        appendUint32(payload, state.files[index].address);
        appendUint32(payload, state.files[index].stream);
    }
    for (size_t index = 0; index < state.semaphores.size(); ++index)
    {
        appendUint32(payload, state.semaphores[index].address);
        appendUint32(payload, state.semaphores[index].count);
    }
    for (size_t index = 0; index < state.dynamicImports.size(); ++index)
    {
        appendText(payload, state.dynamicImports[index]);
    }
    for (size_t index = 0; index < state.unknownImportNames.size(); ++index)
    {
        appendText(payload, state.unknownImportNames[index]);
    }
}

static bool headerCountsValid(const CcSaveStateFileHeader& header)
{
    return header.taskCount <= kCcMaxRecords &&
        header.heapCount <= kCcMaxRecords &&
        header.resourceCount <= kCcMaxRecords &&
        header.streamCount <= kCcMaxRecords &&
        header.fileCount <= kCcMaxRecords &&
        header.semaphoreCount <= kCcMaxRecords &&
        header.dynamicImportCount <= kCcMaxRecords &&
        header.unknownImportCount <= kCcMaxRecords;
}

static bool decodePayload(const std::vector<uint8_t>& data,
    const CcSaveStateFileHeader& header, CcRuntimeState* state)
{
    if (!state || !headerCountsValid(header))
    {
        return false;
    }

    size_t offset = 0;
    uint32_t value = 0;
    if (!readUint64(data, &offset, &state->elapsedGuestMicros) ||
        !readUint64(data, &offset, &state->runtimeInstructions) ||
        !readUint32(data, &offset, &state->heapStart) ||
        !readUint32(data, &offset, &state->heapCursor) ||
        !readUint32(data, &offset, &state->dvcAudioHandle) ||
        !readUint32(data, &offset, &state->dvcAudioSampleRate) ||
        !readUint32(data, &offset, &state->dvcAudioVolume) ||
        !readUint32(data, &offset, &state->framebufferAddress) ||
        !readUint32(data, &offset, &state->framebufferBits) ||
        !readRaw(data, &offset, state->framebufferWriteHighWater,
            sizeof(state->framebufferWriteHighWater)) ||
        !readUint32(data, &offset, &value))
    {
        return false;
    }
    state->framebufferBitsExplicit = value != 0;
    if (!readUint32(data, &offset, &value))
    {
        return false;
    }
    // Reserved field retained for save-state compatibility.
    if (!readUint32(data, &offset, &value))
    {
        return false;
    }
    state->dvcAudioStarted = value != 0;
    if (!readBytes(data, &offset, &state->ram) ||
        !readBytes(data, &offset, &state->systemMemory) ||
        !readBytes(data, &offset, &state->stack) ||
        !readBytes(data, &offset, &state->heapMemory) ||
        !readBytes(data, &offset, &state->legacyLowMemory) ||
        !readBytes(data, &offset, &state->framebuffer) ||
        !readBytes(data, &offset, &state->legacyMmio) ||
        !readBytes(data, &offset, &state->legacyAudioMmio) ||
        !readBytes(data, &offset, &state->legacySystemMmio))
    {
        return false;
    }

    state->heap.reserve(header.heapCount);
    for (uint32_t index = 0; index < header.heapCount; ++index)
    {
        CcSaveHeapBlock block;
        if (!readUint32(data, &offset, &block.address) ||
            !readUint32(data, &offset, &block.size) ||
            !readUint32(data, &offset, &value))
        {
            return false;
        }
        block.free = value != 0;
        state->heap.push_back(block);
    }
    state->tasks.reserve(header.taskCount);
    for (uint32_t index = 0; index < header.taskCount; ++index)
    {
        CcSaveTask task;
        if (!readArmState(data, &offset, &task.state) ||
            !readUint32(data, &offset, &task.entry) ||
            !readUint32(data, &offset, &task.argument) ||
            !readUint32(data, &offset, &task.stack) ||
            !readUint32(data, &offset, &task.priority) ||
            !readUint32(data, &offset, &task.delayTicks) ||
            !readUint32(data, &offset, &value))
        {
            return false;
        }
        task.started = value != 0;
        if (!readUint32(data, &offset, &value))
        {
            return false;
        }
        task.finished = value != 0;
        if (!readUint32(data, &offset, &value))
        {
            return false;
        }
        task.audioProducer = value != 0;
        state->tasks.push_back(task);
    }
    state->resources.reserve(header.resourceCount);
    for (uint32_t index = 0; index < header.resourceCount; ++index)
    {
        CcSaveResource resource;
        if (!readUint32(data, &offset, &resource.address) ||
            !readText(data, &offset, &resource.name) ||
            !readUint32(data, &offset, &resource.position) ||
            !readUint32(data, &offset, &resource.dataAddress))
        {
            return false;
        }
        state->resources.push_back(resource);
    }
    state->streams.reserve(header.streamCount);
    for (uint32_t index = 0; index < header.streamCount; ++index)
    {
        CcSaveFileStream stream;
        if (!readUint32(data, &offset, &stream.stream) ||
            !readText(data, &offset, &stream.requestName) ||
            !readUint32(data, &offset, &stream.position))
        {
            return false;
        }
        state->streams.push_back(stream);
    }
    state->files.reserve(header.fileCount);
    for (uint32_t index = 0; index < header.fileCount; ++index)
    {
        CcSaveFileHandle file;
        if (!readUint32(data, &offset, &file.address) ||
            !readUint32(data, &offset, &file.stream))
        {
            return false;
        }
        state->files.push_back(file);
    }
    state->semaphores.reserve(header.semaphoreCount);
    for (uint32_t index = 0; index < header.semaphoreCount; ++index)
    {
        CcSaveSemaphore semaphore;
        if (!readUint32(data, &offset, &semaphore.address) ||
            !readUint32(data, &offset, &semaphore.count))
        {
            return false;
        }
        state->semaphores.push_back(semaphore);
    }
    state->dynamicImports.reserve(header.dynamicImportCount);
    for (uint32_t index = 0; index < header.dynamicImportCount; ++index)
    {
        std::string name;
        if (!readText(data, &offset, &name))
        {
            return false;
        }
        state->dynamicImports.push_back(name);
    }
    state->unknownImportNames.reserve(header.unknownImportCount);
    for (uint32_t index = 0; index < header.unknownImportCount; ++index)
    {
        std::string name;
        if (!readText(data, &offset, &name))
        {
            return false;
        }
        state->unknownImportNames.push_back(name);
    }
    return offset == data.size();
}

bool saveStateWriteCcSlot(const std::string& appPath, int slot,
    const CcRuntimeState& state, std::string* error,
    SaveStateProgressCallback progressCallback, void* progressUserData)
{
    std::lock_guard<std::mutex> lock(s_ccSaveStateMutex);
    if (slot < 1 || slot > kSaveStateSlotCount ||
        state.gameSha256.size() != 64 || !recordCountsValid(state))
    {
        if (error) *error = "invalid CC runtime state";
        return false;
    }
    std::vector<uint8_t> payload;
    encodePayload(state, &payload);
    std::vector<uint8_t> compressedPayload;
    if (!saveStateCompressPayload(payload, &compressedPayload,
        progressCallback, progressUserData))
    {
        if (error) *error = "failed to compress save-state file";
        return false;
    }
    if (payload.size() > UINT32_MAX || compressedPayload.size() > UINT32_MAX)
    {
        if (error) *error = "save-state file is too large";
        return false;
    }
    CcSaveStateFileHeader header = {};
    header.magic = kCcSaveStateMagic;
    header.headerSize = kCcSaveStateHeaderSize;
    header.payloadSize = (uint32_t)payload.size();
    header.payloadStoredSize = (uint32_t)compressedPayload.size();
    header.memoryFlags = kCcPayloadFlagCompressed;
    header.taskCount = (uint32_t)state.tasks.size();
    header.heapCount = (uint32_t)state.heap.size();
    header.resourceCount = (uint32_t)state.resources.size();
    header.streamCount = (uint32_t)state.streams.size();
    header.fileCount = (uint32_t)state.files.size();
    header.semaphoreCount = (uint32_t)state.semaphores.size();
    header.dynamicImportCount = (uint32_t)state.dynamicImports.size();
    header.unknownImportCount = (uint32_t)state.unknownImportNames.size();
    memcpy(header.gameSha256, state.gameSha256.data(), sizeof(header.gameSha256));
    std::vector<uint8_t> file;
    file.reserve(kCcSaveStateHeaderSize + compressedPayload.size());
    appendHeader(&file, header);
    appendRaw(&file, compressedPayload.data(), compressedPayload.size());
    if (!saveFileReplace(
            saveStatePathForSlot(appPath, SAVE_STATE_FORMAT_CC, slot),
            file.data(), file.size()))
    {
        if (error) *error = "failed to write save-state file";
        return false;
    }
    return true;
}

bool saveStateReadCcSlot(const std::string& appPath, int slot,
    CcRuntimeState* state, std::string* error,
    SaveStateProgressCallback progressCallback, void* progressUserData)
{
    std::lock_guard<std::mutex> lock(s_ccSaveStateMutex);
    if (!state)
    {
        if (error) *error = "runtime state output is invalid";
        return false;
    }
    const std::string savePath = saveStatePathForSlot(appPath, SAVE_STATE_FORMAT_CC, slot);
    const std::vector<std::string> candidatePaths =
        saveFileRecoveryCandidates(savePath);
    if (candidatePaths.empty())
    {
        if (error) *error = "save-state path is invalid";
        return false;
    }
    std::string lastError = "save-state file is truncated";
    for (size_t candidateIndex = 0; candidateIndex < candidatePaths.size();
        ++candidateIndex)
    {
        std::vector<uint8_t> file;
        if (!saveFileReadAll(candidatePaths[candidateIndex], &file,
                kCcSaveStateHeaderSize + kCcMaxPayload) ||
            file.size() < kCcSaveStateHeaderSize)
        {
            continue;
        }
        CcSaveStateFileHeader header = {};
        if (!readHeader(file, &header) || header.magic != kCcSaveStateMagic ||
            header.headerSize != kCcSaveStateHeaderSize || header.payloadSize == 0 ||
            header.payloadStoredSize == 0 || header.payloadSize > kCcMaxPayload ||
            header.payloadStoredSize > kCcMaxPayload ||
            !(header.memoryFlags & kCcPayloadFlagCompressed) ||
            (uint64_t)kCcSaveStateHeaderSize + header.payloadStoredSize != file.size())
        {
            lastError = "unsupported save-state file";
            continue;
        }
        std::string id = saveStateAppIdForPath(appPath);
        if (id.size() != 64 ||
            memcmp(header.gameSha256, id.data(), sizeof(header.gameSha256)) != 0)
        {
            lastError = "save-state belongs to a different game";
            continue;
        }
        std::vector<uint8_t> stored(file.begin() + kCcSaveStateHeaderSize, file.end());
        std::vector<uint8_t> payload;
        if (!saveStateDecompressPayload(
                stored, 0, stored.size(), header.payloadSize,
                &payload, progressCallback, progressUserData))
        {
            lastError = "failed to decompress save-state file";
            continue;
        }
        CcRuntimeState decodedState;
        decodedState.gameSha256 = id;
        if (!decodePayload(payload, header, &decodedState))
        {
            lastError = "save-state file is invalid";
            continue;
        }
        *state = std::move(decodedState);
        if (candidateIndex != 0)
        {
            saveFilePromoteRecoveryCandidate(
                candidatePaths[candidateIndex], savePath);
        }
        return true;
    }
    if (error) *error = lastError;
    return false;
}
