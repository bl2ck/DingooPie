#include "json_manifest.h"

#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace dingoo {
namespace {

std::string escapeJson(const std::string& value) {
    std::ostringstream out;
    for (unsigned char c : value) {
        switch (c) {
        case '\\':
            out << "\\\\";
            break;
        case '"':
            out << "\\\"";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            if (c < 0x20) {
                out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
            } else {
                out << static_cast<char>(c);
            }
            break;
        }
    }
    return out.str();
}

std::string hex32(std::uint32_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::setw(8) << std::setfill('0') << value;
    return out.str();
}

class JsonReader {
public:
    explicit JsonReader(const std::string& text) : text_(text) {}

    void objectBegin() {
        consume('{');
        firstObjectItem_.push_back(true);
    }

    bool objectEnd() {
        skipWs();
        if (peek() == '}') {
            ++pos_;
            if (firstObjectItem_.empty()) {
                throw std::runtime_error("JSON object stack underflow");
            }
            firstObjectItem_.pop_back();
            return true;
        }
        return false;
    }

    void arrayBegin() {
        consume('[');
    }

    bool arrayEnd() {
        skipWs();
        if (peek() == ']') {
            ++pos_;
            return true;
        }
        return false;
    }

    std::string key() {
        if (firstObjectItem_.empty()) {
            throw std::runtime_error("JSON key outside object");
        }
        if (!firstObjectItem_.back()) {
            consume(',');
        }
        firstObjectItem_.back() = false;
        const auto k = string();
        consume(':');
        return k;
    }

    void nextArrayItem(bool& first) {
        if (!first) {
            consume(',');
        }
        first = false;
    }

    std::string string() {
        skipWs();
        consumeRaw('"');
        std::string out;
        while (pos_ < text_.size()) {
            char c = text_[pos_++];
            if (c == '"') {
                return out;
            }
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (pos_ >= text_.size()) {
                throw std::runtime_error("unterminated JSON escape");
            }
            const char e = text_[pos_++];
            switch (e) {
            case '"':
            case '\\':
            case '/':
                out.push_back(e);
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            default:
                throw std::runtime_error("unsupported JSON escape in manifest");
            }
        }
        throw std::runtime_error("unterminated JSON string");
    }

    std::uint32_t u32() {
        skipWs();
        if (pos_ >= text_.size() || !std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
            throw std::runtime_error("expected JSON integer");
        }
        std::uint64_t value = 0;
        while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
            value = value * 10 + static_cast<unsigned>(text_[pos_++] - '0');
            if (value > 0xffffffffull) {
                throw std::runtime_error("integer exceeds uint32");
            }
        }
        return static_cast<std::uint32_t>(value);
    }

    bool boolean() {
        skipWs();
        if (text_.compare(pos_, 4, "true") == 0) {
            pos_ += 4;
            return true;
        }
        if (text_.compare(pos_, 5, "false") == 0) {
            pos_ += 5;
            return false;
        }
        throw std::runtime_error("expected JSON boolean");
    }

    void skipValue() {
        skipWs();
        const char c = peek();
        if (c == '"') {
            (void)string();
        } else if (c == '{') {
            objectBegin();
            while (!objectEnd()) {
                (void)key();
                skipValue();
            }
        } else if (c == '[') {
            arrayBegin();
            bool first = true;
            while (!arrayEnd()) {
                nextArrayItem(first);
                skipValue();
            }
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            (void)u32();
        } else if (c == 't' || c == 'f') {
            (void)boolean();
        } else {
            throw std::runtime_error("unsupported JSON value in manifest");
        }
    }

private:
    void skipWs() {
        while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) {
            ++pos_;
        }
    }

    char peek() {
        skipWs();
        if (pos_ >= text_.size()) {
            return 0;
        }
        return text_[pos_];
    }

    void consume(char expected) {
        skipWs();
        consumeRaw(expected);
    }

    void consumeRaw(char expected) {
        if (pos_ >= text_.size() || text_[pos_] != expected) {
            throw std::runtime_error(std::string("expected JSON character: ") + expected);
        }
        ++pos_;
    }

    const std::string& text_;
    std::size_t pos_ = 0;
    std::vector<bool> firstObjectItem_;
};

void writeSymbolArray(std::ostringstream& out, const char* name, const std::vector<SymbolEntry>& entries) {
    out << "    \"" << name << "\": [\n";
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        out << "      { \"name\": \"" << escapeJson(e.name) << "\", \"offset\": " << e.offset
            << ", \"offset_hex\": \"" << hex32(e.offset) << "\", \"string_offset\": " << e.stringOffset
            << ", \"unknown0\": " << e.unknown0 << ", \"unknown1\": " << e.unknown1 << " }";
        out << (i + 1 == entries.size() ? "\n" : ",\n");
    }
    out << "    ]";
}

SymbolEntry readSymbol(JsonReader& reader) {
    SymbolEntry entry;
    reader.objectBegin();
    while (!reader.objectEnd()) {
        const auto k = reader.key();
        if (k == "name") {
            entry.name = reader.string();
        } else if (k == "offset") {
            entry.offset = reader.u32();
        } else if (k == "string_offset") {
            entry.stringOffset = reader.u32();
        } else if (k == "unknown0") {
            entry.unknown0 = reader.u32();
        } else if (k == "unknown1") {
            entry.unknown1 = reader.u32();
        } else {
            reader.skipValue();
        }
    }
    return entry;
}

std::vector<SymbolEntry> readSymbolArray(JsonReader& reader) {
    std::vector<SymbolEntry> entries;
    reader.arrayBegin();
    bool first = true;
    while (!reader.arrayEnd()) {
        reader.nextArrayItem(first);
        entries.push_back(readSymbol(reader));
    }
    return entries;
}

ResourceEntry readResource(JsonReader& reader) {
    ResourceEntry resource;
    reader.objectBegin();
    while (!reader.objectEnd()) {
        const auto k = reader.key();
        if (k == "kind") {
            resource.kind = parseResourceKind(reader.string());
        } else if (k == "name") {
            resource.name = reader.string();
        } else if (k == "offset") {
            resource.offset = reader.u32();
        } else if (k == "size") {
            resource.size = reader.u32();
        } else if (k == "xor_key") {
            resource.xorKey = static_cast<std::uint8_t>(reader.u32());
        } else if (k == "path") {
            resource.exportPath = reader.string();
        } else {
            reader.skipValue();
        }
    }
    return resource;
}

std::vector<ResourceEntry> readResourceArray(JsonReader& reader) {
    std::vector<ResourceEntry> entries;
    reader.arrayBegin();
    bool first = true;
    while (!reader.arrayEnd()) {
        reader.nextArrayItem(first);
        entries.push_back(readResource(reader));
    }
    return entries;
}

} // namespace

std::string writeManifest(const AppImage& image, const std::string& originalImagePath, const std::string& rawPayloadPath) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"format\": \"dingoo-app-tool-manifest-v1\",\n";
    out << "  \"original_image\": \"" << escapeJson(originalImagePath) << "\",\n";
    out << "  \"raw_payload\": \"" << escapeJson(rawPayloadPath) << "\",\n";
    out << "  \"file_size\": " << image.originalBytes.size() << ",\n";
    out << "  \"chunks\": {\n";
    out << "    \"impt\": { \"offset\": " << image.impt.offset << ", \"size\": " << image.impt.size << " },\n";
    out << "    \"expt\": { \"offset\": " << image.expt.offset << ", \"size\": " << image.expt.size << " },\n";
    out << "    \"rawd\": { \"offset\": " << image.rawd.offset << ", \"offset_hex\": \"" << hex32(image.rawd.offset)
        << "\", \"size\": " << image.rawd.size << ", \"size_hex\": \"" << hex32(image.rawd.size)
        << "\", \"entry\": " << image.rawd.entry << ", \"entry_hex\": \"" << hex32(image.rawd.entry)
        << "\", \"origin\": " << image.rawd.origin << ", \"origin_hex\": \"" << hex32(image.rawd.origin)
        << "\", \"program_size\": " << image.rawd.programSize << " }\n";
    out << "  },\n";
    out << "  \"symbols\": {\n";
    writeSymbolArray(out, "imports", image.imports);
    out << ",\n";
    writeSymbolArray(out, "exports", image.exports);
    out << "\n";
    out << "  },\n";
    out << "  \"unparsed_tail\": { \"path\": \"" << escapeJson(image.unparsedTailPath)
        << "\", \"offset\": " << image.unparsedTailOffset
        << ", \"offset_hex\": \"" << hex32(image.unparsedTailOffset)
        << "\", \"size\": " << image.unparsedTailSize
        << ", \"size_hex\": \"" << hex32(image.unparsedTailSize)
        << "\", \"present\": " << (image.hasUnparsedTail ? "true" : "false") << " },\n";
    out << "  \"resources\": [\n";
    for (std::size_t i = 0; i < image.resources.size(); ++i) {
        const auto& r = image.resources[i];
        out << "    { \"kind\": \"" << resourceKindName(r.kind) << "\", \"name\": \"" << escapeJson(r.name)
            << "\", \"path\": \"" << escapeJson(r.exportPath) << "\", \"offset\": " << r.offset
            << ", \"offset_hex\": \"" << hex32(r.offset) << "\", \"size\": " << r.size
            << ", \"size_hex\": \"" << hex32(r.size) << "\", \"xor_key\": " << static_cast<unsigned>(r.xorKey)
            << " }";
        out << (i + 1 == image.resources.size() ? "\n" : ",\n");
    }
    out << "  ]\n";
    out << "}\n";
    return out.str();
}

Manifest readManifest(const std::string& text) {
    JsonReader reader(text);
    Manifest manifest;
    reader.objectBegin();
    while (!reader.objectEnd()) {
        const auto k = reader.key();
        if (k == "format") {
            const auto format = reader.string();
            if (format != "dingoo-app-tool-manifest-v1") {
                throw std::runtime_error("unsupported manifest format: " + format);
            }
        } else if (k == "original_image") {
            manifest.originalImagePath = reader.string();
        } else if (k == "raw_payload") {
            manifest.rawPayloadPath = reader.string();
        } else if (k == "chunks") {
            reader.objectBegin();
            while (!reader.objectEnd()) {
                const auto ck = reader.key();
                if (ck == "rawd") {
                    reader.objectBegin();
                    while (!reader.objectEnd()) {
                        const auto rk = reader.key();
                        if (rk == "offset") {
                            manifest.image.rawd.offset = reader.u32();
                        } else if (rk == "size") {
                            manifest.image.rawd.size = reader.u32();
                        } else if (rk == "entry") {
                            manifest.image.rawd.entry = reader.u32();
                        } else if (rk == "origin") {
                            manifest.image.rawd.origin = reader.u32();
                        } else if (rk == "program_size") {
                            manifest.image.rawd.programSize = reader.u32();
                        } else {
                            reader.skipValue();
                        }
                    }
                } else {
                    reader.skipValue();
                }
            }
        } else if (k == "symbols") {
            reader.objectBegin();
            while (!reader.objectEnd()) {
                const auto sk = reader.key();
                if (sk == "imports") {
                    manifest.image.imports = readSymbolArray(reader);
                } else if (sk == "exports") {
                    manifest.image.exports = readSymbolArray(reader);
                } else {
                    reader.skipValue();
                }
            }
        } else if (k == "unparsed_tail") {
            reader.objectBegin();
            while (!reader.objectEnd()) {
                const auto tk = reader.key();
                if (tk == "path") {
                    manifest.image.unparsedTailPath = reader.string();
                } else if (tk == "offset") {
                    manifest.image.unparsedTailOffset = reader.u32();
                } else if (tk == "size") {
                    manifest.image.unparsedTailSize = reader.u32();
                } else if (tk == "present") {
                    manifest.image.hasUnparsedTail = reader.boolean();
                } else {
                    reader.skipValue();
                }
            }
        } else if (k == "resources") {
            manifest.image.resources = readResourceArray(reader);
        } else {
            reader.skipValue();
        }
    }

    if (manifest.originalImagePath.empty() || manifest.rawPayloadPath.empty()) {
        throw std::runtime_error("manifest is missing original_image or raw_payload");
    }
    return manifest;
}

} // namespace dingoo
