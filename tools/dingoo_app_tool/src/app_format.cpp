#include "app_format.h"

#include "file_util.h"
#include "json_manifest.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>

namespace dingoo {
namespace {

constexpr std::uint32_t kCcdlOffset = 0;
constexpr std::uint32_t kImptOffset = 32;
constexpr std::uint32_t kExptOffset = 64;
constexpr std::uint32_t kRawdOffset = 96;
constexpr std::uint32_t kOptionalErptOffset = 128;

// The validated samples use fixed-position chunk descriptors at the start of
// the image. Variable-length data is reached through offsets stored in them.
std::uint16_t readU16(const std::vector<std::uint8_t>& data, std::uint32_t offset) {
    if (offset + 2 > data.size()) {
        throw std::runtime_error("unexpected end of file while reading u16");
    }
    return static_cast<std::uint16_t>(data[offset]) |
           static_cast<std::uint16_t>(data[offset + 1] << 8);
}

std::uint32_t readU32(const std::vector<std::uint8_t>& data, std::uint32_t offset) {
    if (offset + 4 > data.size()) {
        throw std::runtime_error("unexpected end of file while reading u32");
    }
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

void writeU32(std::vector<std::uint8_t>& data, std::uint32_t offset, std::uint32_t value) {
    if (offset + 4 > data.size()) {
        throw std::runtime_error("unexpected end of file while writing u32");
    }
    data[offset] = static_cast<std::uint8_t>(value & 0xff);
    data[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xff);
    data[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xff);
    data[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xff);
}

std::string readIdent(const std::vector<std::uint8_t>& data, std::uint32_t offset) {
    if (offset + 4 > data.size()) {
        throw std::runtime_error("unexpected end of file while reading chunk id");
    }
    return std::string(reinterpret_cast<const char*>(data.data() + offset), 4);
}

ChunkHeader readChunkHeader(const std::vector<std::uint8_t>& data, std::uint32_t offset) {
    ChunkHeader chunk;
    chunk.ident = readIdent(data, offset);
    chunk.type = readU32(data, offset + 4);
    chunk.offset = readU32(data, offset + 8);
    chunk.size = readU32(data, offset + 12);
    return chunk;
}

RawdHeader readRawdHeader(const std::vector<std::uint8_t>& data) {
    if (readIdent(data, kRawdOffset) != "RAWD") {
        throw std::runtime_error("missing RAWD header");
    }
    RawdHeader rawd;
    rawd.type = readU32(data, kRawdOffset + 4);
    rawd.offset = readU32(data, kRawdOffset + 8);
    rawd.size = readU32(data, kRawdOffset + 12);
    rawd.reserved = readU32(data, kRawdOffset + 16);
    rawd.entry = readU32(data, kRawdOffset + 20);
    rawd.origin = readU32(data, kRawdOffset + 24);
    rawd.programSize = readU32(data, kRawdOffset + 28);
    return rawd;
}

std::string readCString(const std::vector<std::uint8_t>& data, std::uint32_t offset, std::uint32_t maxLength) {
    if (offset > data.size()) {
        throw std::runtime_error("string offset is outside file");
    }
    const std::uint32_t end = std::min<std::uint32_t>(offset + maxLength, static_cast<std::uint32_t>(data.size()));
    std::uint32_t cursor = offset;
    while (cursor < end && data[cursor] != 0) {
        ++cursor;
    }
    return std::string(reinterpret_cast<const char*>(data.data() + offset), cursor - offset);
}

std::vector<SymbolEntry> parseSymbolTable(
    const std::vector<std::uint8_t>& data,
    const ChunkHeader& chunk,
    const char* label) {
    if (chunk.offset + 16 > data.size()) {
        throw std::runtime_error(std::string(label) + " table is outside file");
    }

    const std::uint32_t count = readU32(data, chunk.offset);
    if (count > 4096) {
        throw std::runtime_error(std::string(label) + " table count is unreasonable");
    }
    const std::uint32_t entriesOffset = chunk.offset + 16;
    const std::uint32_t stringsOffset = entriesOffset + count * 16;
    if (stringsOffset > data.size()) {
        throw std::runtime_error(std::string(label) + " entries exceed file size");
    }

    std::vector<SymbolEntry> entries;
    entries.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t rec = entriesOffset + i * 16;
        SymbolEntry entry;
        entry.stringOffset = readU32(data, rec);
        entry.unknown0 = readU32(data, rec + 4);
        entry.unknown1 = readU32(data, rec + 8);
        entry.offset = readU32(data, rec + 12);
        entry.name = readCString(data, stringsOffset + entry.stringOffset, 256);
        entries.push_back(entry);
    }
    return entries;
}

bool resourceNameCharOk(std::uint8_t c) {
    return c >= 0x20 && c <= 0x7e;
}

int resourceNameLength(const std::vector<std::uint8_t>& data, std::uint32_t offset, std::uint32_t maxLength) {
    if (offset + maxLength > data.size()) {
        return -1;
    }
    for (std::uint32_t i = 0; i < maxLength; ++i) {
        const std::uint8_t c = data[offset + i];
        if (c == 0) {
            return static_cast<int>(i);
        }
        if (!resourceNameCharOk(c)) {
            return -1;
        }
    }
    return -1;
}

bool knownResourceExtension(const std::string& name) {
    const auto dot = name.find_last_of('.');
    if (dot == std::string::npos) {
        return false;
    }
    std::string ext = name.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext == ".war" || ext == ".pcm" || ext == ".dat" || ext == ".txt" || ext == ".log";
}

void addErptResources(AppImage& image, const std::vector<std::uint8_t>& data) {
    // ERPT records have fixed-size names and payload metadata. Observed ERPT
    // payloads are XOR-encoded with 0x40, so export stores decoded bytes.
    if (!image.hasErpt || image.erpt.offset + 4 > data.size()) {
        return;
    }

    const std::uint32_t count = readU32(data, image.erpt.offset);
    constexpr std::uint32_t recordSize = 0x1fc;
    constexpr std::uint32_t nameSize = 0x1f4;
    const std::uint32_t tableEnd = image.erpt.offset + 4 + count * recordSize;
    if (count == 0 || count > 4096 || tableEnd > data.size()) {
        return;
    }

    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t rec = image.erpt.offset + 4 + i * recordSize;
        const auto name = readCString(data, rec, nameSize);
        const std::uint32_t size = readU32(data, rec + nameSize);
        const std::uint32_t relOffset = readU32(data, rec + nameSize + 4);
        const std::uint32_t offset = image.erpt.offset + relOffset;
        if (name.empty() || offset > data.size() || size > data.size() - offset) {
            continue;
        }
        ResourceEntry entry;
        entry.kind = ResourceKind::Erpt;
        entry.name = name;
        entry.offset = offset;
        entry.size = size;
        entry.xorKey = 0x40;
        image.resources.push_back(entry);
    }
}

struct PackedTable {
    std::uint32_t base = 0;
    std::uint32_t count = 0;
    std::uint32_t tableEnd = 0;
    int score = 0;
};

bool probePackedTable(const std::vector<std::uint8_t>& data, std::uint32_t base, PackedTable& out) {
    // Short-name packed tables have no magic value. The probe therefore scores
    // a candidate by printable names, known file extensions, and monotonic data
    // offsets to avoid treating arbitrary appended bytes as resources.
    if (base + 2 > data.size()) {
        return false;
    }

    const std::uint32_t count = readU16(data, base);
    constexpr std::uint32_t recordSize = 36;
    constexpr std::uint32_t nameSize = 32;
    const std::uint32_t tableEnd = base + 2 + count * recordSize;
    if (count == 0 || count > 1024 || tableEnd > data.size()) {
        return false;
    }

    const std::uint32_t tableSize = 2 + count * recordSize;
    std::uint32_t validNames = 0;
    std::uint32_t knownNames = 0;
    std::uint32_t validOffsets = 0;
    std::uint32_t lastOffset = tableSize;

    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t rec = base + 2 + i * recordSize;
        const int nameLen = resourceNameLength(data, rec, nameSize);
        const std::uint32_t relOffset = readU32(data, rec + nameSize);
        if (relOffset >= tableSize && base + relOffset < data.size()) {
            ++validOffsets;
            if (relOffset >= lastOffset) {
                lastOffset = relOffset;
            }
        }

        if (nameLen <= 0) {
            continue;
        }
        const std::string name(reinterpret_cast<const char*>(data.data() + rec), static_cast<std::size_t>(nameLen));
        ++validNames;
        if (knownResourceExtension(name)) {
            ++knownNames;
        }
    }

    if (validNames < count * 8 / 10 || validOffsets < count * 8 / 10 || knownNames < 8) {
        return false;
    }

    out.base = base;
    out.count = count;
    out.tableEnd = tableEnd;
    out.score = static_cast<int>(knownNames);
    return true;
}

std::uint32_t packedNextOffset(
    const std::vector<std::uint8_t>& data,
    const PackedTable& table,
    std::uint32_t index,
    std::uint32_t currentOffset,
    std::uint32_t packageEnd) {
    constexpr std::uint32_t recordSize = 36;
    constexpr std::uint32_t nameSize = 32;
    std::uint32_t best = packageEnd - table.base;
    for (std::uint32_t i = index + 1; i < table.count; ++i) {
        const std::uint32_t rec = table.base + 2 + i * recordSize;
        const std::uint32_t relOffset = readU32(data, rec + nameSize);
        if (relOffset > currentOffset && relOffset < best) {
            best = relOffset;
        }
    }
    return best;
}

void addPackedResources(AppImage& image, const std::vector<std::uint8_t>& data) {
    constexpr std::uint32_t maxTables = 8;
    constexpr std::uint32_t recordSize = 36;
    constexpr std::uint32_t nameSize = 32;
    std::vector<PackedTable> tables;

    const std::uint32_t rawEnd = image.rawd.offset + image.rawd.size;
    const std::uint32_t scan = (rawEnd + 0xfffu) & ~0xfffu;
    for (std::uint32_t base = scan; base + 2 < data.size() && tables.size() < maxTables; base += 0x1000) {
        PackedTable table;
        if (probePackedTable(data, base, table)) {
            tables.push_back(table);
        }
    }

    for (std::size_t t = 0; t < tables.size(); ++t) {
        const auto& table = tables[t];
        const std::uint32_t packageEnd = (t + 1 < tables.size()) ? tables[t + 1].base : static_cast<std::uint32_t>(data.size());
        for (std::uint32_t i = 0; i < table.count; ++i) {
            const std::uint32_t rec = table.base + 2 + i * recordSize;
            const int nameLen = resourceNameLength(data, rec, nameSize);
            const std::uint32_t relOffset = readU32(data, rec + nameSize);
            if (nameLen <= 0 || relOffset < (table.tableEnd - table.base) || table.base + relOffset >= packageEnd) {
                continue;
            }

            const std::uint32_t nextOffset = packedNextOffset(data, table, i, relOffset, packageEnd);
            if (nextOffset <= relOffset || table.base + nextOffset > packageEnd) {
                continue;
            }

            ResourceEntry entry;
            entry.kind = ResourceKind::Packed;
            entry.name = std::string(reinterpret_cast<const char*>(data.data() + rec), static_cast<std::size_t>(nameLen));
            entry.offset = table.base + relOffset;
            entry.size = nextOffset - relOffset;
            entry.xorKey = 0;
            image.resources.push_back(entry);
        }
    }
}

struct Packed64Table {
    std::uint32_t base = 0;
    std::uint32_t count = 0;
    std::uint32_t firstRecord = 0;
    std::uint32_t dataBias = 0;
    std::uint32_t dataEnd = 0;
    int score = 0;
};

bool packed64NameOk(const std::string& name) {
    if (name.size() < 5 || name.rfind(".\\", 0) != 0) {
        return false;
    }
    if (name.find('.') == std::string::npos) {
        return false;
    }
    for (unsigned char c : name) {
        if (c < 0x20 || c > 0x7e) {
            return false;
        }
    }
    return true;
}

std::string readFixedName(
    const std::vector<std::uint8_t>& data,
    std::uint32_t offset,
    std::uint32_t maxLength) {
    if (offset + maxLength > data.size()) {
        return {};
    }
    std::uint32_t len = 0;
    while (len < maxLength && data[offset + len] != 0) {
        ++len;
    }
    return std::string(reinterpret_cast<const char*>(data.data() + offset), len);
}

bool probePacked64At(const std::vector<std::uint8_t>& data, std::uint32_t base, Packed64Table& out) {
    // packed64 tables are long-path resource records: uint32 stored offset
    // followed by a 64-byte relative path. Some samples include one invalid
    // sentinel record before the first real resource, represented by firstRecord.
    constexpr std::uint32_t recordSize = 0x44;
    constexpr std::uint32_t nameSize = 0x40;
    constexpr std::uint32_t minRecords = 16;
    constexpr std::uint32_t maxRecords = 20000;

    std::uint32_t count = 0;
    std::uint32_t previousOffset = 0;
    int knownExtensions = 0;
    int invalidOffsets = 0;

    for (; count < maxRecords; ++count) {
        const std::uint32_t rec = base + count * recordSize;
        if (rec + recordSize > data.size()) {
            break;
        }
        const std::uint32_t offset = readU32(data, rec);
        const std::string name = readFixedName(data, rec + 4, nameSize);
        if (!packed64NameOk(name)) {
            break;
        }
        if (offset >= data.size()) {
            ++invalidOffsets;
            if (invalidOffsets > 1 || count != 0) {
                break;
            }
        } else if (previousOffset != 0 && offset < previousOffset) {
            break;
        } else {
            previousOffset = offset;
        }
        if (knownResourceExtension(name)) {
            ++knownExtensions;
        }
    }

    const std::uint32_t firstRecord = invalidOffsets == 1 ? 1u : 0u;
    if (count < minRecords || count <= firstRecord) {
        return false;
    }

    const std::uint32_t tableEnd = base + count * recordSize;
    const std::uint32_t firstOffset = readU32(data, base + firstRecord * recordSize);
    if (firstOffset < tableEnd) {
        return false;
    }

    out.base = base;
    out.count = count;
    out.firstRecord = firstRecord;
    out.dataBias = firstOffset - tableEnd;
    out.dataEnd = static_cast<std::uint32_t>(data.size());
    out.score = static_cast<int>(count - firstRecord) + knownExtensions * 4;
    return true;
}

void addPacked64Resources(AppImage& image, const std::vector<std::uint8_t>& data) {
    constexpr std::uint32_t recordSize = 0x44;
    constexpr std::uint32_t nameSize = 0x40;

    const std::uint32_t rawEnd = image.rawd.offset + image.rawd.size;
    std::vector<Packed64Table> tables;

    // The long-path package table normally appears after an 0xFF-filled gap.
    // Scan only the appended area and score by continuous valid records.
    for (std::uint32_t base = rawEnd; base + recordSize < data.size(); base += 2) {
        Packed64Table candidate;
        if (probePacked64At(data, base, candidate)) {
            bool overlaps = false;
            for (const auto& existing : tables) {
                const std::uint32_t existingEnd = existing.base + existing.count * recordSize;
                const std::uint32_t candidateEnd = candidate.base + candidate.count * recordSize;
                if (!(candidateEnd <= existing.base || candidate.base >= existingEnd)) {
                    overlaps = true;
                    break;
                }
            }
            if (!overlaps) {
                tables.push_back(candidate);
                base = candidate.base + candidate.count * recordSize;
            }
        }
        if (base > rawEnd + 0x400000 && !tables.empty()) {
            break;
        }
    }

    if (tables.empty()) {
        return;
    }

    for (const auto& table : tables) {
        for (std::uint32_t i = table.firstRecord; i < table.count; ++i) {
            const std::uint32_t rec = table.base + i * recordSize;
            const std::uint32_t storedOffset = readU32(data, rec);
            const std::uint32_t nextStoredOffset = (i + 1 < table.count)
                ? readU32(data, rec + recordSize)
                : (table.dataEnd + table.dataBias);
            if (nextStoredOffset <= storedOffset) {
                continue;
            }

            const std::uint32_t offset = storedOffset - table.dataBias;
            const std::uint32_t nextOffset = nextStoredOffset - table.dataBias;
            if (offset > data.size() || nextOffset > data.size() || nextOffset <= offset) {
                continue;
            }

            ResourceEntry entry;
            entry.kind = ResourceKind::Packed64;
            entry.name = readFixedName(data, rec + 4, nameSize);
            entry.offset = offset;
            entry.size = nextOffset - offset;
            entry.xorKey = 0;
            image.resources.push_back(entry);
        }
    }
}

std::vector<std::uint8_t> slice(const std::vector<std::uint8_t>& data, std::uint32_t offset, std::uint32_t size) {
    if (offset > data.size() || size > data.size() - offset) {
        throw std::runtime_error("requested slice is outside file");
    }
    return std::vector<std::uint8_t>(data.begin() + offset, data.begin() + offset + size);
}

std::vector<std::uint8_t> decodeResource(
    const std::vector<std::uint8_t>& data,
    std::uint32_t offset,
    std::uint32_t size,
    std::uint8_t xorKey) {
    auto out = slice(data, offset, size);
    if (xorKey != 0) {
        for (auto& b : out) {
            b ^= xorKey;
        }
    }
    return out;
}

std::vector<std::uint8_t> encodeResource(const std::vector<std::uint8_t>& data, std::uint8_t xorKey) {
    auto out = data;
    if (xorKey != 0) {
        for (auto& b : out) {
            b ^= xorKey;
        }
    }
    return out;
}

struct Packed64OffsetRef {
    std::string name;
    std::uint32_t fieldOffset = 0;
    std::uint32_t storedOffset = 0;
    std::uint32_t actualOffset = 0;
};

std::vector<Packed64OffsetRef> collectPacked64OffsetRefs(
    const std::vector<std::uint8_t>& data,
    const std::vector<ResourceEntry>& resources) {
    // Repacking packed64 resources needs writable references back to the table
    // offset fields. Names are used as anchors, while actual offsets disambiguate
    // records if the same resource name appears in unrelated byte ranges.
    constexpr std::uint32_t recordSize = 0x44;
    constexpr std::uint32_t nameSize = 0x40;
    std::vector<Packed64OffsetRef> refs;
    refs.reserve(resources.size());
    std::map<std::string, std::vector<std::uint32_t>> recordsByName;

    for (std::uint32_t rec = 0; rec + recordSize <= data.size(); rec += 2) {
        const std::string name = readFixedName(data, rec + 4, nameSize);
        if (!packed64NameOk(name)) {
            continue;
        }
        recordsByName[name].push_back(rec);
    }

    for (const auto& resource : resources) {
        if (resource.kind != ResourceKind::Packed64 || resource.exportPath.empty()) {
            continue;
        }

        bool found = false;
        const auto foundRecords = recordsByName.find(resource.name);
        if (foundRecords != recordsByName.end()) {
            for (std::uint32_t rec : foundRecords->second) {
                const std::uint32_t storedOffset = readU32(data, rec);
                if (storedOffset < resource.offset || storedOffset - resource.offset > data.size()) {
                    continue;
                }

                Packed64OffsetRef ref;
                ref.name = resource.name;
                ref.fieldOffset = rec;
                ref.storedOffset = storedOffset;
                ref.actualOffset = resource.offset;
                refs.push_back(ref);
                found = true;
                break;
            }
        }
        if (!found) {
            throw std::runtime_error("failed to locate packed64 table record for " + resource.name);
        }
    }

    std::sort(refs.begin(), refs.end(), [](const Packed64OffsetRef& a, const Packed64OffsetRef& b) {
        return a.actualOffset < b.actualOffset;
    });
    return refs;
}

bool canPatchResizablePacked64(const Manifest& manifest) {
    return std::all_of(
        manifest.image.resources.begin(),
        manifest.image.resources.end(),
        [](const ResourceEntry& resource) {
            return resource.kind == ResourceKind::Packed64 && !resource.exportPath.empty();
        });
}

void reportProgress(
    const ProgressCallback& progress,
    std::uint32_t current,
    std::uint32_t total,
    const std::string& message) {
    if (progress) {
        progress(current, total, message);
    }
}

// Keeps determinate progress accounting in the workflow layer so resource
// patchers only report completed units instead of owning step math.
class ProgressTracker {
public:
    ProgressTracker(const ProgressCallback& progress, std::uint32_t total)
        : progress_(progress), total_(total) {}

    void advance(const std::string& message) {
        ++current_;
        reportProgress(progress_, current_, total_, message);
    }

private:
    const ProgressCallback& progress_;
    std::uint32_t total_ = 0;
    std::uint32_t current_ = 0;
};

std::string resourceLabel(const ResourceEntry& resource) {
    if (!resource.name.empty()) {
        return resource.name;
    }
    return resource.exportPath.empty() ? "resource" : resource.exportPath;
}

std::uint32_t countPatchableResources(const Manifest& manifest) {
    std::uint32_t count = 0;
    for (const auto& resource : manifest.image.resources) {
        if (!resource.exportPath.empty()) {
            ++count;
        }
    }
    return count;
}

std::string resourceProgressMessage(
    const char* action,
    std::uint32_t current,
    std::uint32_t total,
    const ResourceEntry& resource) {
    std::ostringstream message;
    message << action << " " << current << "/" << total << ": " << resourceLabel(resource);
    return message.str();
}

void patchResizablePacked64Resources(
    const Manifest& manifest,
    const std::filesystem::path& root,
    std::vector<std::uint8_t>& output,
    ProgressTracker& progress) {
    // packed64 resources may grow or shrink. Repacking edits the byte vector in
    // place and updates later table offsets by the cumulative size delta.
    auto refs = collectPacked64OffsetRefs(output, manifest.image.resources);
    std::int64_t cumulativeDelta = 0;
    const std::uint32_t resourceTotal = countPatchableResources(manifest);
    std::uint32_t resourceIndex = 0;

    for (const auto& resource : manifest.image.resources) {
        if (resource.exportPath.empty()) {
            continue;
        }
        const auto decoded = readFile(root / resource.exportPath);
        const auto encoded = encodeResource(decoded, resource.xorKey);
        const std::int64_t delta = static_cast<std::int64_t>(encoded.size()) - static_cast<std::int64_t>(resource.size);

        const std::uint32_t adjustedOffset = static_cast<std::uint32_t>(
            static_cast<std::int64_t>(resource.offset) + cumulativeDelta);
        if (adjustedOffset > output.size() || resource.size > output.size() - adjustedOffset) {
            throw std::runtime_error("resource range moved outside output while packing " + resource.name);
        }

        output.erase(output.begin() + adjustedOffset, output.begin() + adjustedOffset + resource.size);
        output.insert(output.begin() + adjustedOffset, encoded.begin(), encoded.end());

        if (delta != 0) {
            for (auto& ref : refs) {
                if (ref.actualOffset > resource.offset) {
                    ref.actualOffset = static_cast<std::uint32_t>(static_cast<std::int64_t>(ref.actualOffset) + delta);
                    ref.storedOffset = static_cast<std::uint32_t>(static_cast<std::int64_t>(ref.storedOffset) + delta);
                    writeU32(output, ref.fieldOffset, ref.storedOffset);
                }
            }
            cumulativeDelta += delta;
        }

        ++resourceIndex;
        progress.advance(resourceProgressMessage("Packed resource", resourceIndex, resourceTotal, resource));
    }
}

void patchFixedSizeResources(
    const Manifest& manifest,
    const std::filesystem::path& root,
    std::vector<std::uint8_t>& output,
    ProgressTracker& progress) {
    const std::uint32_t resourceTotal = countPatchableResources(manifest);
    std::uint32_t resourceIndex = 0;

    for (const auto& resource : manifest.image.resources) {
        if (resource.exportPath.empty()) {
            continue;
        }
        const auto decoded = readFile(root / resource.exportPath);
        if (decoded.size() != resource.size) {
            throw std::runtime_error("resource size changed for " + resource.name + "; this package type still requires the original size");
        }
        const auto encoded = encodeResource(decoded, resource.xorKey);
        std::copy(encoded.begin(), encoded.end(), output.begin() + resource.offset);

        ++resourceIndex;
        progress.advance(resourceProgressMessage("Packed resource", resourceIndex, resourceTotal, resource));
    }
}

std::string uniqueResourcePath(const ResourceEntry& resource, std::uint32_t index) {
    std::filesystem::path safe = sanitizeRelativePath(resource.name);
    std::ostringstream prefix;
    prefix << std::setw(4) << std::setfill('0') << index << "_";
    auto fileName = prefix.str() + safe.filename().u8string();
    if (safe.has_parent_path()) {
        return (safe.parent_path() / fileName).generic_u8string();
    }
    return fileName;
}

} // namespace

std::string resourceKindName(ResourceKind kind) {
    switch (kind) {
    case ResourceKind::Erpt:
        return "erpt";
    case ResourceKind::Packed:
        return "packed";
    case ResourceKind::Packed64:
        return "packed64";
    }
    return "unknown";
}

ResourceKind parseResourceKind(const std::string& value) {
    if (value == "erpt") {
        return ResourceKind::Erpt;
    }
    if (value == "packed") {
        return ResourceKind::Packed;
    }
    if (value == "packed64") {
        return ResourceKind::Packed64;
    }
    throw std::runtime_error("unknown resource kind: " + value);
}

AppImage parseAppImage(const std::vector<std::uint8_t>& data) {
    if (data.size() < kRawdOffset + 32) {
        throw std::runtime_error("file is too small to be a Dingoo .app");
    }
    if (readIdent(data, kCcdlOffset) != "CCDL") {
        throw std::runtime_error("missing CCDL header");
    }

    AppImage image;
    image.originalBytes = data;
    image.impt = readChunkHeader(data, kImptOffset);
    image.expt = readChunkHeader(data, kExptOffset);
    image.rawd = readRawdHeader(data);

    if (image.impt.ident != "IMPT" || image.expt.ident != "EXPT") {
        throw std::runtime_error("missing IMPT or EXPT header");
    }
    if (image.rawd.offset > data.size() || image.rawd.size > data.size() - image.rawd.offset) {
        throw std::runtime_error("RAWD payload range is outside file");
    }
    if (image.rawd.entry == 0 || image.rawd.programSize < image.rawd.size) {
        throw std::runtime_error("RAWD header has invalid entry or program size");
    }

    image.imports = parseSymbolTable(data, image.impt, "import");
    image.exports = parseSymbolTable(data, image.expt, "export");

    if (data.size() >= kOptionalErptOffset + 32 && readIdent(data, kOptionalErptOffset) == "ERPT") {
        image.hasErpt = true;
        image.erpt = readChunkHeader(data, kOptionalErptOffset);
        addErptResources(image, data);
    }
    // Resource formats are probed from most explicit to most heuristic. If no
    // known table is found, the appended bytes are preserved as unparsed tail.
    if (image.resources.empty()) {
        addPackedResources(image, data);
    }
    if (image.resources.empty()) {
        addPacked64Resources(image, data);
    }
    if (image.resources.empty()) {
        const std::uint32_t rawEnd = image.rawd.offset + image.rawd.size;
        if (rawEnd < data.size()) {
            image.hasUnparsedTail = true;
            image.unparsedTailOffset = rawEnd;
            image.unparsedTailSize = static_cast<std::uint32_t>(data.size() - rawEnd);
            image.unparsedTailPath = "tail/after_rawd.bin";
        }
    }

    return image;
}

void unpackApp(
    const std::filesystem::path& appPath,
    const std::filesystem::path& outputDir,
    const ProgressCallback& progress) {
    reportProgress(progress, 0, 0, "Reading app file");
    const auto bytes = readFile(appPath);
    reportProgress(progress, 0, 0, "Parsing app image");
    auto image = parseAppImage(bytes);
    const std::uint32_t total = 4u + static_cast<std::uint32_t>(image.resources.size()) + (image.hasUnparsedTail ? 1u : 0u);
    ProgressTracker progressTracker(progress, total);

    ensureDirectory(outputDir);
    ensureDirectory(outputDir / "payload");
    ensureDirectory(outputDir / "resources");
    progressTracker.advance("Created output directories");

    writeFile(outputDir / "original.app.bin", image.originalBytes);
    progressTracker.advance("Wrote original.app.bin");
    writeFile(outputDir / "payload" / "rawd.bin", slice(bytes, image.rawd.offset, image.rawd.size));
    progressTracker.advance("Wrote payload/rawd.bin");
    if (image.hasUnparsedTail) {
        // The tail directory is created only when there is real unparsed data.
        writeFile(outputDir / image.unparsedTailPath, slice(bytes, image.unparsedTailOffset, image.unparsedTailSize));
        progressTracker.advance("Wrote unparsed tail");
    }

    for (std::uint32_t i = 0; i < image.resources.size(); ++i) {
        auto& resource = image.resources[i];
        resource.exportPath = (std::filesystem::path("resources") / uniqueResourcePath(resource, i)).generic_u8string();
        writeFile(outputDir / resource.exportPath, decodeResource(bytes, resource.offset, resource.size, resource.xorKey));
        progressTracker.advance(
            resourceProgressMessage(
                "Unpacked resource",
                i + 1,
                static_cast<std::uint32_t>(image.resources.size()),
                resource));
    }

    writeTextFile(outputDir / "manifest.json", writeManifest(image, "original.app.bin", "payload/rawd.bin"));
    progressTracker.advance("Wrote manifest.json");
}

void packApp(
    const std::filesystem::path& manifestPath,
    const std::filesystem::path& outputPath,
    const ProgressCallback& progress) {
    reportProgress(progress, 0, 0, "Reading manifest");
    Manifest manifest = readManifest(readTextFile(manifestPath));
    const auto root = manifestPath.parent_path();
    const std::uint32_t total = 3u + countPatchableResources(manifest) + (manifest.image.hasUnparsedTail ? 1u : 0u);
    ProgressTracker progressTracker(progress, total);

    auto output = readFile(root / manifest.originalImagePath);
    progressTracker.advance("Read original image");

    const auto rawd = readFile(root / manifest.rawPayloadPath);
    if (rawd.size() != manifest.image.rawd.size) {
        throw std::runtime_error("payload size changed; conservative pack currently requires the original RAWD size");
    }
    std::copy(rawd.begin(), rawd.end(), output.begin() + manifest.image.rawd.offset);
    progressTracker.advance("Patched RAWD payload");

    if (manifest.image.hasUnparsedTail) {
        const auto tail = readFile(root / manifest.image.unparsedTailPath);
        if (tail.size() != manifest.image.unparsedTailSize) {
            throw std::runtime_error("unparsed tail size changed; conservative pack currently requires the original size");
        }
        std::copy(tail.begin(), tail.end(), output.begin() + manifest.image.unparsedTailOffset);
        progressTracker.advance("Patched unparsed tail");
    }

    if (canPatchResizablePacked64(manifest)) {
        patchResizablePacked64Resources(manifest, root, output, progressTracker);
    } else {
        patchFixedSizeResources(manifest, root, output, progressTracker);
    }

    writeFile(outputPath, output);
    progressTracker.advance("Wrote output app");
}

std::string describeApp(const AppImage& image) {
    std::uint32_t erptCount = 0;
    std::uint32_t packedCount = 0;
    std::uint32_t packed64Count = 0;
    for (const auto& resource : image.resources) {
        switch (resource.kind) {
        case ResourceKind::Erpt:
            ++erptCount;
            break;
        case ResourceKind::Packed:
            ++packedCount;
            break;
        case ResourceKind::Packed64:
            ++packed64Count;
            break;
        }
    }

    std::ostringstream out;
    out << "Dingoo .app\n";
    out << "  file_size: " << image.originalBytes.size() << "\n";
    out << "  imports:   " << image.imports.size() << "\n";
    out << "  exports:   " << image.exports.size() << "\n";
    out << "  resources: " << image.resources.size() << "\n";
    if (!image.resources.empty()) {
        out << "    erpt:     " << erptCount << "\n";
        out << "    packed:   " << packedCount << "\n";
        out << "    packed64: " << packed64Count << "\n";
    }
    if (image.hasUnparsedTail) {
        out << "  unparsed_tail: 0x" << std::hex << image.unparsedTailOffset << "..0x"
            << (image.unparsedTailOffset + image.unparsedTailSize) << std::dec
            << " (" << image.unparsedTailSize << " bytes)\n";
    }
    out << "  rawd:\n";
    out << "    offset: 0x" << std::hex << image.rawd.offset << std::dec << "\n";
    out << "    size:   0x" << std::hex << image.rawd.size << std::dec << "\n";
    out << "    entry:  0x" << std::hex << image.rawd.entry << std::dec << "\n";
    out << "    origin: 0x" << std::hex << image.rawd.origin << std::dec << "\n";
    out << "    program_size: 0x" << std::hex << image.rawd.programSize << std::dec << "\n";
    if (image.hasErpt) {
        out << "  erpt:\n";
        out << "    offset: 0x" << std::hex << image.erpt.offset << std::dec << "\n";
        out << "    size:   0x" << std::hex << image.erpt.size << std::dec << "\n";
    }
    return out.str();
}

} // namespace dingoo
