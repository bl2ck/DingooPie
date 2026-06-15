#include "stx_editor.h"

#include "file_util.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace dingoo {
namespace {

constexpr std::uint32_t kMaxEditableRunBytes = 512;

bool isAsciiPrintable(char32_t ch) {
    return ch >= 0x20 && ch <= 0x7e;
}

bool isCjk(char32_t ch) {
    return (ch >= 0x3400 && ch <= 0x4dbf) ||
           (ch >= 0x4e00 && ch <= 0x9fff) ||
           (ch >= 0xf900 && ch <= 0xfaff);
}

bool isAllowedTextChar(char32_t ch) {
    if (isAsciiPrintable(ch) || isCjk(ch)) {
        return true;
    }

    // Common punctuation seen in game UI text and resource labels.
    return ch == U'\u3000' || ch == U'\u3001' || ch == U'\u3002' ||
           ch == U'\uff0c' || ch == U'\uff1a' || ch == U'\uff1b' ||
           ch == U'\uff01' || ch == U'\uff1f' || ch == U'\uff08' ||
           ch == U'\uff09' || ch == U'\u2014' ||
           ch == U'\u2018' || ch == U'\u2019' || ch == U'\u201c' ||
           ch == U'\u201d' || ch == U'\u2026' || ch == U'\u30fb';
}

bool isSuspiciousFiller(char32_t ch) {
    // 0xCCCC is a common debug-fill pattern and appears as false positive text.
    return ch == U'\ucccc' || ch == U'\uffff' || ch == U'\ufefe';
}

std::uint32_t cjkCount(const std::u32string& text) {
    std::uint32_t count = 0;
    for (char32_t ch : text) {
        if (isCjk(ch)) {
            ++count;
        }
    }
    return count;
}

bool looksUsefulText(const std::u32string& text) {
    if (text.size() < 2 || cjkCount(text) < 2) {
        return false;
    }

    std::uint32_t filler = 0;
    for (char32_t ch : text) {
        if (isSuspiciousFiller(ch)) {
            ++filler;
        }
    }
    if (filler != 0 && filler * 2 >= text.size()) {
        return false;
    }

    return true;
}

std::string utf8FromCodepoint(char32_t ch) {
    std::string out;
    if (ch <= 0x7f) {
        out.push_back(static_cast<char>(ch));
    } else if (ch <= 0x7ff) {
        out.push_back(static_cast<char>(0xc0 | ((ch >> 6) & 0x1f)));
        out.push_back(static_cast<char>(0x80 | (ch & 0x3f)));
    } else if (ch <= 0xffff) {
        out.push_back(static_cast<char>(0xe0 | ((ch >> 12) & 0x0f)));
        out.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (ch & 0x3f)));
    } else {
        out.push_back(static_cast<char>(0xf0 | ((ch >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((ch >> 12) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (ch & 0x3f)));
    }
    return out;
}

std::string toUtf8(const std::u32string& text) {
    std::string out;
    for (char32_t ch : text) {
        out += utf8FromCodepoint(ch);
    }
    return out;
}

bool decodeUtf8At(const std::vector<std::uint8_t>& data, std::size_t offset, char32_t& ch, std::size_t& bytes) {
    const std::uint8_t b0 = data[offset];
    if (b0 < 0x80) {
        ch = b0;
        bytes = 1;
        return true;
    }
    if ((b0 & 0xe0) == 0xc0 && offset + 1 < data.size()) {
        const std::uint8_t b1 = data[offset + 1];
        if ((b1 & 0xc0) != 0x80) {
            return false;
        }
        ch = ((b0 & 0x1f) << 6) | (b1 & 0x3f);
        bytes = 2;
        return ch >= 0x80;
    }
    if ((b0 & 0xf0) == 0xe0 && offset + 2 < data.size()) {
        const std::uint8_t b1 = data[offset + 1];
        const std::uint8_t b2 = data[offset + 2];
        if ((b1 & 0xc0) != 0x80 || (b2 & 0xc0) != 0x80) {
            return false;
        }
        ch = ((b0 & 0x0f) << 12) | ((b1 & 0x3f) << 6) | (b2 & 0x3f);
        bytes = 3;
        return ch >= 0x800;
    }
    if ((b0 & 0xf8) == 0xf0 && offset + 3 < data.size()) {
        const std::uint8_t b1 = data[offset + 1];
        const std::uint8_t b2 = data[offset + 2];
        const std::uint8_t b3 = data[offset + 3];
        if ((b1 & 0xc0) != 0x80 || (b2 & 0xc0) != 0x80 || (b3 & 0xc0) != 0x80) {
            return false;
        }
        ch = ((b0 & 0x07) << 18) | ((b1 & 0x3f) << 12) | ((b2 & 0x3f) << 6) | (b3 & 0x3f);
        bytes = 4;
        return ch >= 0x10000 && ch <= 0x10ffff;
    }
    return false;
}

bool readUtf8Codepoint(const std::string& text, std::size_t& offset, char32_t& ch) {
    const auto bytes = std::vector<std::uint8_t>(text.begin(), text.end());
    std::size_t size = 0;
    if (offset >= bytes.size() || !decodeUtf8At(bytes, offset, ch, size)) {
        return false;
    }
    offset += size;
    return true;
}

std::vector<std::uint8_t> encodeUtf16Le(const std::string& utf8) {
    std::vector<std::uint8_t> out;
    for (std::size_t offset = 0; offset < utf8.size();) {
        char32_t ch = 0;
        if (!readUtf8Codepoint(utf8, offset, ch)) {
            throw std::runtime_error("replacement contains invalid UTF-8");
        }
        if (ch <= 0xffff) {
            out.push_back(static_cast<std::uint8_t>(ch & 0xff));
            out.push_back(static_cast<std::uint8_t>((ch >> 8) & 0xff));
        } else {
            ch -= 0x10000;
            const char32_t hi = 0xd800 + ((ch >> 10) & 0x3ff);
            const char32_t lo = 0xdc00 + (ch & 0x3ff);
            out.push_back(static_cast<std::uint8_t>(hi & 0xff));
            out.push_back(static_cast<std::uint8_t>((hi >> 8) & 0xff));
            out.push_back(static_cast<std::uint8_t>(lo & 0xff));
            out.push_back(static_cast<std::uint8_t>((lo >> 8) & 0xff));
        }
    }
    return out;
}

#ifdef _WIN32
std::wstring utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (needed <= 0) {
        throw std::runtime_error("replacement contains invalid UTF-8");
    }
    std::wstring out(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), out.data(), needed);
    return out;
}

bool decodeGbkAt(const std::vector<std::uint8_t>& data, std::size_t offset, char32_t& ch, std::size_t& bytes) {
    bytes = 1;
    const std::uint8_t b0 = data[offset];
    if (b0 < 0x80) {
        ch = b0;
        return true;
    }
    if (offset + 1 >= data.size()) {
        return false;
    }

    char input[2] = {static_cast<char>(data[offset]), static_cast<char>(data[offset + 1])};
    wchar_t wide = 0;
    const int written = MultiByteToWideChar(936, MB_ERR_INVALID_CHARS, input, 2, &wide, 1);
    if (written != 1) {
        return false;
    }
    ch = static_cast<char32_t>(wide);
    bytes = 2;
    return true;
}

std::vector<std::uint8_t> encodeGbk(const std::string& utf8) {
    const std::wstring wide = utf8ToWide(utf8);
    if (wide.empty()) {
        return {};
    }
    const int needed = WideCharToMultiByte(936, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        throw std::runtime_error("replacement cannot be encoded as GBK");
    }
    std::vector<std::uint8_t> out(static_cast<std::size_t>(needed));
    BOOL usedDefaultChar = FALSE;
    WideCharToMultiByte(
        936,
        0,
        wide.data(),
        static_cast<int>(wide.size()),
        reinterpret_cast<char*>(out.data()),
        needed,
        nullptr,
        &usedDefaultChar);
    if (usedDefaultChar) {
        throw std::runtime_error("replacement contains characters that cannot be encoded as GBK");
    }
    return out;
}
#else
bool decodeGbkAt(const std::vector<std::uint8_t>&, std::size_t, char32_t&, std::size_t&) {
    return false;
}

std::vector<std::uint8_t> encodeGbk(const std::string&) {
    throw std::runtime_error("GBK import is only implemented on Windows builds");
}
#endif

std::string hexOffset(std::uint32_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::uppercase << value;
    return out.str();
}

std::string escapeTsv(const std::string& value) {
    std::string out;
    for (char c : value) {
        switch (c) {
        case '\\':
            out += "\\\\";
            break;
        case '\t':
            out += "\\t";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\n':
            out += "\\n";
            break;
        default:
            out.push_back(c);
            break;
        }
    }
    return out;
}

std::string unescapeTsv(const std::string& value) {
    std::string out;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        if (c != '\\' || i + 1 >= value.size()) {
            out.push_back(c);
            continue;
        }
        const char next = value[++i];
        switch (next) {
        case '\\':
            out.push_back('\\');
            break;
        case 't':
            out.push_back('\t');
            break;
        case 'r':
            out.push_back('\r');
            break;
        case 'n':
            out.push_back('\n');
            break;
        default:
            out.push_back(next);
            break;
        }
    }
    return out;
}

std::vector<std::string> splitTsvLine(const std::string& line) {
    std::vector<std::string> columns;
    std::string column;
    for (char c : line) {
        if (c == '\t') {
            columns.push_back(column);
            column.clear();
        } else {
            column.push_back(c);
        }
    }
    columns.push_back(column);
    return columns;
}

std::uint32_t parseInteger(const std::string& text) {
    try {
        std::size_t pos = 0;
        const int base = text.rfind("0x", 0) == 0 || text.rfind("0X", 0) == 0 ? 16 : 10;
        const auto value = std::stoull(text, &pos, base);
        if (pos != text.size() || value > 0xffffffffull) {
            throw std::runtime_error("invalid integer in STX TSV: " + text);
        }
        return static_cast<std::uint32_t>(value);
    } catch (const std::exception&) {
        throw std::runtime_error("invalid integer in STX TSV: " + text);
    }
}

void addEntry(
    std::vector<StxTextEntry>& entries,
    std::set<std::pair<std::uint32_t, std::string>>& seen,
    const std::string& encoding,
    std::uint32_t offset,
    std::uint32_t byteLength,
    const std::u32string& text) {
    if (!looksUsefulText(text)) {
        return;
    }
    if (byteLength == 0 || byteLength > kMaxEditableRunBytes) {
        return;
    }

    const auto key = std::make_pair(offset, encoding);
    if (!seen.insert(key).second) {
        return;
    }

    StxTextEntry entry;
    entry.id = entries.size();
    entry.encoding = encoding;
    entry.offset = offset;
    entry.byteLength = byteLength;
    entry.text = toUtf8(text);
    entries.push_back(entry);
}

std::uint16_t readU16Unchecked(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return static_cast<std::uint16_t>(data[offset]) |
           static_cast<std::uint16_t>(data[offset + 1] << 8);
}

std::uint32_t readU32Unchecked(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

void writeU16Unchecked(std::vector<std::uint8_t>& data, std::size_t offset, std::uint16_t value) {
    data[offset] = static_cast<std::uint8_t>(value & 0xff);
    data[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xff);
}

void writeU32Unchecked(std::vector<std::uint8_t>& data, std::size_t offset, std::uint32_t value) {
    data[offset] = static_cast<std::uint8_t>(value & 0xff);
    data[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xff);
    data[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xff);
    data[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xff);
}

void replaceRange(
    std::vector<std::uint8_t>& data,
    std::size_t offset,
    std::size_t oldLength,
    const std::vector<std::uint8_t>& replacement) {
    if (offset > data.size() || oldLength > data.size() - offset) {
        throw std::runtime_error("STX replacement range is outside file");
    }
    data.erase(data.begin() + offset, data.begin() + offset + oldLength);
    data.insert(data.begin() + offset, replacement.begin(), replacement.end());
}

bool decodeUtf16LeRun(
    const std::vector<std::uint8_t>& data,
    std::size_t offset,
    std::size_t byteLength,
    std::u32string& out) {
    if (offset + byteLength > data.size() || (byteLength % 2) != 0) {
        return false;
    }

    out.clear();
    for (std::size_t cursor = offset; cursor < offset + byteLength;) {
        char32_t ch = readU16Unchecked(data, cursor);
        cursor += 2;
        if (ch == 0) {
            while (cursor < offset + byteLength) {
                if (readU16Unchecked(data, cursor) != 0) {
                    return false;
                }
                cursor += 2;
            }
            break;
        }
        if (ch >= 0xd800 && ch <= 0xdbff) {
            if (cursor + 2 > offset + byteLength) {
                return false;
            }
            const char32_t lo = readU16Unchecked(data, cursor);
            cursor += 2;
            if (lo < 0xdc00 || lo > 0xdfff) {
                return false;
            }
            ch = 0x10000 + (((ch - 0xd800) << 10) | (lo - 0xdc00));
        }
        if (!isAllowedTextChar(ch)) {
            return false;
        }
        out.push_back(ch);
    }
    return true;
}

bool decodeGbkRun(
    const std::vector<std::uint8_t>& data,
    std::size_t offset,
    std::size_t byteLength,
    std::u32string& out) {
    if (offset + byteLength > data.size()) {
        return false;
    }

    out.clear();
    const std::size_t end = offset + byteLength;
    while (offset < end) {
        if (data[offset] == 0) {
            break;
        }
        char32_t ch = 0;
        std::size_t bytes = 0;
        if (!decodeGbkAt(data, offset, ch, bytes) || offset + bytes > end || !isAllowedTextChar(ch)) {
            return false;
        }
        out.push_back(ch);
        offset += bytes;
    }
    return true;
}

void scanStructuredUtf16Le(
    const std::vector<std::uint8_t>& data,
    std::vector<StxTextEntry>& entries,
    std::set<std::pair<std::uint32_t, std::string>>& seen) {
    for (std::size_t record = 0; record + 9 < data.size(); ++record) {
        // Observed STX string field:
        //   86 01 <char_count:u16> 05 10 00 00 00 <UTF-16LE bytes>
        if (data[record] != 0x86 || data[record + 1] != 0x01 ||
            data[record + 4] != 0x05 || data[record + 5] != 0x10 ||
            data[record + 6] != 0x00 || data[record + 7] != 0x00 ||
            data[record + 8] != 0x00) {
            continue;
        }

        const std::uint32_t chars = readU16Unchecked(data, record + 2);
        const std::uint32_t byteLength = chars * 2;
        const std::size_t textOffset = record + 9;
        if (chars == 0 || byteLength > kMaxEditableRunBytes || textOffset + byteLength > data.size()) {
            continue;
        }

        std::u32string text;
        if (decodeUtf16LeRun(data, textOffset, byteLength, text)) {
            addEntry(entries, seen, "utf-16le", static_cast<std::uint32_t>(textOffset), byteLength, text);
        }
    }
}

void scanStructuredGbk(
    const std::vector<std::uint8_t>& data,
    std::vector<StxTextEntry>& entries,
    std::set<std::pair<std::uint32_t, std::string>>& seen) {
    for (std::size_t record = 0; record + 8 < data.size(); ++record) {
        // Some STX files embed OLE/BIFF-like GBK strings as length-prefixed
        // VT_LPSTR values. The length usually includes the trailing NUL byte.
        std::size_t textOffset = 0;
        std::uint32_t byteLength = 0;
        const std::uint32_t type = readU32Unchecked(data, record);
        if (type == 0x0000001e) {
            byteLength = readU32Unchecked(data, record + 4);
            textOffset = record + 8;
        } else if (type == 0x0000101e && record + 12 < data.size()) {
            byteLength = readU32Unchecked(data, record + 8);
            textOffset = record + 12;
        } else {
            continue;
        }

        if (byteLength == 0 || byteLength > kMaxEditableRunBytes || textOffset + byteLength > data.size()) {
            continue;
        }

        std::u32string text;
        if (decodeGbkRun(data, textOffset, byteLength, text)) {
            addEntry(entries, seen, "gbk", static_cast<std::uint32_t>(textOffset), byteLength, text);
        }
    }
}

std::vector<std::uint8_t> encodeReplacement(const std::string& encoding, const std::string& text) {
    if (encoding == "utf-16le") {
        return encodeUtf16Le(text);
    }
    if (encoding == "gbk") {
        return encodeGbk(text);
    }
    if (encoding == "utf-8") {
        for (std::size_t offset = 0; offset < text.size();) {
            char32_t ch = 0;
            if (!readUtf8Codepoint(text, offset, ch)) {
                throw std::runtime_error("replacement contains invalid UTF-8");
            }
        }
        return std::vector<std::uint8_t>(text.begin(), text.end());
    }
    throw std::runtime_error("unsupported STX text encoding: " + encoding);
}

struct StxTextEdit {
    std::size_t lineNumber = 0;
    std::string encoding;
    std::uint32_t offset = 0;
    std::uint32_t byteLength = 0;
    std::vector<std::uint8_t> encoded;
};

} // namespace

std::vector<StxTextEntry> scanStxText(const std::vector<std::uint8_t>& data) {
    std::vector<StxTextEntry> entries;
    std::set<std::pair<std::uint32_t, std::string>> seen;

    scanStructuredUtf16Le(data, entries, seen);
    scanStructuredGbk(data, entries, seen);

    std::sort(entries.begin(), entries.end(), [](const StxTextEntry& a, const StxTextEntry& b) {
        if (a.offset != b.offset) {
            return a.offset < b.offset;
        }
        return a.encoding < b.encoding;
    });

    for (std::size_t i = 0; i < entries.size(); ++i) {
        entries[i].id = i;
    }
    return entries;
}

std::string describeStxText(const std::vector<StxTextEntry>& entries) {
    std::map<std::string, std::size_t> byEncoding;
    for (const auto& entry : entries) {
        ++byEncoding[entry.encoding];
    }

    std::ostringstream out;
    out << "STX text entries: " << entries.size() << "\n";
    for (const auto& [encoding, count] : byEncoding) {
        out << "  " << encoding << ": " << count << "\n";
    }

    const std::size_t previewCount = std::min<std::size_t>(entries.size(), 12);
    for (std::size_t i = 0; i < previewCount; ++i) {
        const auto& e = entries[i];
        out << "  [" << e.id << "] " << e.encoding << " " << hexOffset(e.offset)
            << " len=" << e.byteLength << " text=" << e.text << "\n";
    }
    return out.str();
}

void exportStxText(const std::filesystem::path& stxPath, const std::filesystem::path& tsvPath) {
    const auto entries = scanStxText(readFile(stxPath));
    std::ostringstream out;
    out << "id\tencoding\toffset_hex\tbyte_length\ttext\treplacement\n";
    for (const auto& entry : entries) {
        out << entry.id << '\t'
            << entry.encoding << '\t'
            << hexOffset(entry.offset) << '\t'
            << entry.byteLength << '\t'
            << escapeTsv(entry.text) << "\t\n";
    }
    writeTextFile(tsvPath, out.str());
}

std::size_t importStxText(
    const std::filesystem::path& stxPath,
    const std::filesystem::path& tsvPath,
    const std::filesystem::path& outputPath) {
    auto data = readFile(stxPath);
    const std::string tsv = readTextFile(tsvPath);

    std::vector<StxTextEdit> edits;
    std::istringstream input(tsv);
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (lineNumber == 1 && line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xef &&
            static_cast<unsigned char>(line[1]) == 0xbb &&
            static_cast<unsigned char>(line[2]) == 0xbf) {
            line.erase(0, 3);
        }
        if (line.empty()) {
            continue;
        }
        if (lineNumber == 1 && line.rfind("id\tencoding\t", 0) == 0) {
            continue;
        }

        const auto columns = splitTsvLine(line);
        if (columns.size() < 6) {
            throw std::runtime_error("STX TSV line has fewer than 6 columns: " + std::to_string(lineNumber));
        }

        const std::string encoding = columns[1];
        const std::uint32_t offset = parseInteger(columns[2]);
        const std::uint32_t byteLength = parseInteger(columns[3]);
        const std::string replacement = unescapeTsv(columns[5]);
        if (replacement.empty()) {
            continue;
        }
        if (offset > data.size() || byteLength > data.size() - offset) {
            throw std::runtime_error("STX TSV range is outside file at line: " + std::to_string(lineNumber));
        }

        const auto encoded = encodeReplacement(encoding, replacement);

        StxTextEdit edit;
        edit.lineNumber = lineNumber;
        edit.encoding = encoding;
        edit.offset = offset;
        edit.byteLength = byteLength;
        edit.encoded = encoded;
        edits.push_back(std::move(edit));
    }

    std::sort(edits.begin(), edits.end(), [](const StxTextEdit& a, const StxTextEdit& b) {
        return a.offset > b.offset;
    });

    for (const auto& edit : edits) {
        if (edit.encoding == "utf-16le") {
            if (edit.offset < 9) {
                throw std::runtime_error("UTF-16LE STX row has no record header at line: " + std::to_string(edit.lineNumber));
            }
            const std::size_t record = edit.offset - 9;
            if (record + 9 > data.size() ||
                data[record] != 0x86 || data[record + 1] != 0x01 ||
                data[record + 4] != 0x05 || data[record + 5] != 0x10 ||
                data[record + 6] != 0x00 || data[record + 7] != 0x00 ||
                data[record + 8] != 0x00) {
                throw std::runtime_error("UTF-16LE STX record header changed at line: " + std::to_string(edit.lineNumber));
            }
            if ((edit.encoded.size() % 2) != 0 || edit.encoded.size() / 2 > 0xffff) {
                throw std::runtime_error("UTF-16LE replacement is too large at line: " + std::to_string(edit.lineNumber));
            }
            writeU16Unchecked(data, record + 2, static_cast<std::uint16_t>(edit.encoded.size() / 2));
            replaceRange(data, edit.offset, edit.byteLength, edit.encoded);
            continue;
        }

        if (edit.encoding == "gbk") {
            std::size_t lengthField = 0;
            std::size_t textOffset = 0;
            if (edit.offset >= 8 && readU32Unchecked(data, edit.offset - 8) == 0x0000001e) {
                lengthField = edit.offset - 4;
                textOffset = edit.offset;
            } else if (edit.offset >= 12 && readU32Unchecked(data, edit.offset - 12) == 0x0000101e) {
                lengthField = edit.offset - 4;
                textOffset = edit.offset;
            } else {
                throw std::runtime_error("GBK STX length header changed at line: " + std::to_string(edit.lineNumber));
            }

            std::vector<std::uint8_t> encodedWithTerminator = edit.encoded;
            encodedWithTerminator.push_back(0);
            if (encodedWithTerminator.size() > 0xffffffffull) {
                throw std::runtime_error("GBK replacement is too large at line: " + std::to_string(edit.lineNumber));
            }
            writeU32Unchecked(data, lengthField, static_cast<std::uint32_t>(encodedWithTerminator.size()));
            replaceRange(data, textOffset, edit.byteLength, encodedWithTerminator);
            continue;
        }

        throw std::runtime_error("resizable STX import does not support encoding: " + edit.encoding);
    }

    writeFile(outputPath, data);
    return edits.size();
}

} // namespace dingoo
