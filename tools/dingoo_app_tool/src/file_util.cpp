#include "file_util.h"

#include <fstream>
#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace dingoo {

std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("failed to open file for reading: " + pathToUtf8(path));
    }

    file.seekg(0, std::ios::end);
    const auto size = file.tellg();
    if (size < 0) {
        throw std::runtime_error("failed to get file size: " + pathToUtf8(path));
    }
    file.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    if (!data.empty()) {
        file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!file) {
            throw std::runtime_error("failed to read complete file: " + pathToUtf8(path));
        }
    }
    return data;
}

void writeFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& data) {
    if (path.has_parent_path()) {
        ensureDirectory(path.parent_path());
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        throw std::runtime_error("failed to open file for writing: " + pathToUtf8(path));
    }
    if (!data.empty()) {
        file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!file) {
            throw std::runtime_error("failed to write complete file: " + pathToUtf8(path));
        }
    }
}

void writeTextFile(const std::filesystem::path& path, const std::string& text) {
    if (path.has_parent_path()) {
        ensureDirectory(path.parent_path());
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        throw std::runtime_error("failed to open text file for writing: " + pathToUtf8(path));
    }
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
}

std::string readTextFile(const std::filesystem::path& path) {
    const auto bytes = readFile(path);
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

void ensureDirectory(const std::filesystem::path& path) {
    if (path.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec) {
        throw std::runtime_error("failed to create directory: " + pathToUtf8(path));
    }
}

std::string pathToUtf8(const std::filesystem::path& path) {
#ifdef _WIN32
    const std::wstring wide = path.wstring();
    if (wide.empty()) {
        return {};
    }
    const int required = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return "<path-conversion-failed>";
    }
    std::string utf8(static_cast<std::size_t>(required - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, utf8.data(), required, nullptr, nullptr);
    return utf8;
#else
    return path.u8string();
#endif
}

std::filesystem::path sanitizeRelativePath(const std::string& name) {
    std::filesystem::path result;
    std::string segment;

    auto flushSegment = [&]() {
        if (segment.empty() || segment == "." || segment == "..") {
            segment.clear();
            return;
        }

        for (char& c : segment) {
            const bool bad = c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|';
            if (bad || static_cast<unsigned char>(c) < 0x20) {
                c = '_';
            }
        }
        result /= segment;
        segment.clear();
    };

    for (char c : name) {
        if (c == '/' || c == '\\') {
            flushSegment();
        } else {
            segment.push_back(c);
        }
    }
    flushSegment();

    if (result.empty()) {
        result = "unnamed.bin";
    }
    return result;
}

} // namespace dingoo
