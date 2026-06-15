#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace dingoo {

struct ChunkHeader {
    std::string ident;
    std::uint32_t type = 0;
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
};

struct RawdHeader {
    std::uint32_t type = 0;
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    std::uint32_t reserved = 0;
    std::uint32_t entry = 0;
    std::uint32_t origin = 0;
    std::uint32_t programSize = 0;
};

struct SymbolEntry {
    std::uint32_t stringOffset = 0;
    std::uint32_t unknown0 = 0;
    std::uint32_t unknown1 = 0;
    std::uint32_t offset = 0;
    std::string name;
};

enum class ResourceKind {
    Erpt,
    Packed,
    Packed64,
};

struct ResourceEntry {
    ResourceKind kind = ResourceKind::Packed;
    std::string name;
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    std::uint8_t xorKey = 0;
    std::string exportPath;
};

struct AppImage {
    std::vector<std::uint8_t> originalBytes;
    ChunkHeader impt;
    ChunkHeader expt;
    RawdHeader rawd;
    bool hasErpt = false;
    ChunkHeader erpt;
    std::vector<SymbolEntry> imports;
    std::vector<SymbolEntry> exports;
    std::vector<ResourceEntry> resources;
    // Unparsed tail is only populated when bytes after RAWD were not recognized
    // as one of the known resource table formats.
    bool hasUnparsedTail = false;
    std::uint32_t unparsedTailOffset = 0;
    std::uint32_t unparsedTailSize = 0;
    std::string unparsedTailPath;
};

// A total value of 0 means the current phase is indeterminate.
using ProgressCallback = std::function<void(std::uint32_t current, std::uint32_t total, const std::string& message)>;

AppImage parseAppImage(const std::vector<std::uint8_t>& data);
void unpackApp(
    const std::filesystem::path& appPath,
    const std::filesystem::path& outputDir,
    const ProgressCallback& progress = {});
void packApp(
    const std::filesystem::path& manifestPath,
    const std::filesystem::path& outputPath,
    const ProgressCallback& progress = {});
std::string describeApp(const AppImage& image);

std::string resourceKindName(ResourceKind kind);
ResourceKind parseResourceKind(const std::string& value);

} // namespace dingoo
