#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace dingoo {

std::vector<std::uint8_t> readFile(const std::filesystem::path& path);
void writeFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& data);
void writeTextFile(const std::filesystem::path& path, const std::string& text);
std::string readTextFile(const std::filesystem::path& path);
void ensureDirectory(const std::filesystem::path& path);
std::string pathToUtf8(const std::filesystem::path& path);
std::filesystem::path sanitizeRelativePath(const std::string& name);

} // namespace dingoo
