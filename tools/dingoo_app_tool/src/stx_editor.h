#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace dingoo {

struct StxTextEntry {
    std::size_t id = 0;
    std::string encoding;
    std::uint32_t offset = 0;
    std::uint32_t byteLength = 0;
    std::string text;
};

std::vector<StxTextEntry> scanStxText(const std::vector<std::uint8_t>& data);
std::string describeStxText(const std::vector<StxTextEntry>& entries);
void exportStxText(const std::filesystem::path& stxPath, const std::filesystem::path& tsvPath);
std::size_t importStxText(
    const std::filesystem::path& stxPath,
    const std::filesystem::path& tsvPath,
    const std::filesystem::path& outputPath);

} // namespace dingoo
