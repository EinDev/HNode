#include "TextGenerator.h"
#include "DmxUtil.h"

#include <yaml-cpp/yaml.h>
#include "imgui.h"
#include "imgui_stdlib.h"

#include <ctime>
#include <iomanip>
#include <sstream>
#include <unordered_map>

// ---------------------------------------------------------------------------------------
// Shared UTF-8 <-> code point helpers, used by both the CleanupTable pass and the
// unicode/UTF-16LE encode path below.
//
// `text` (an ImGui-edited std::string) is assumed to hold UTF-8. The C# reference works in
// UTF-16 `char`s throughout, so the two encodings need bridging at a couple of points:
//  - CleanupTable matching happens per Unicode code point (not per UTF-8 byte) - every
//    table entry is in the Basic Multilingual Plane (max U+2055), so a code point compare
//    is equivalent to the C# per-`char` compare for every key actually in the table.
//  - The unicode==true path needs real UTF-8 -> UTF-16LE transcoding (.NET's "Unicode"
//    encoding), including surrogate pairs for code points above the BMP.
//
// The decoder below is a simple manual UTF-8 decoder: it does not need to handle malformed
// input gracefully beyond not crashing, so any invalid leading byte or truncated/invalid
// continuation sequence is just skipped (that byte is dropped and decoding resumes at the
// next byte) rather than substituted or reported.
namespace {

std::vector<char32_t> Utf8Decode(const std::string& s) {
    std::vector<char32_t> out;
    out.reserve(s.size());
    size_t i = 0;
    const size_t n = s.size();
    while (i < n) {
        const unsigned char c0 = static_cast<unsigned char>(s[i]);
        char32_t cp = 0;
        int extra = 0;
        if ((c0 & 0x80) == 0x00) {
            cp = c0;
            extra = 0;
        } else if ((c0 & 0xE0) == 0xC0) {
            cp = static_cast<char32_t>(c0 & 0x1F);
            extra = 1;
        } else if ((c0 & 0xF0) == 0xE0) {
            cp = static_cast<char32_t>(c0 & 0x0F);
            extra = 2;
        } else if ((c0 & 0xF8) == 0xF0) {
            cp = static_cast<char32_t>(c0 & 0x07);
            extra = 3;
        } else {
            // Invalid leading byte (stray continuation byte, or an 0xF8-0xFF byte that
            // can't start a valid UTF-8 sequence) - skip it and keep going.
            ++i;
            continue;
        }

        if (i + static_cast<size_t>(extra) >= n) {
            // Not enough bytes left for the sequence this leading byte announced - skip it.
            ++i;
            continue;
        }

        bool valid = true;
        for (int k = 1; k <= extra; ++k) {
            const unsigned char ck = static_cast<unsigned char>(s[i + static_cast<size_t>(k)]);
            if ((ck & 0xC0) != 0x80) {
                valid = false;
                break;
            }
            cp = (cp << 6) | static_cast<char32_t>(ck & 0x3F);
        }

        if (!valid) {
            ++i;
            continue;
        }

        out.push_back(cp);
        i += static_cast<size_t>(extra) + 1;
    }
    return out;
}

void Utf8AppendCodepoint(std::string& out, char32_t cp) {
    if (cp <= 0x7F) {
        out += static_cast<char>(cp);
    } else if (cp <= 0x7FF) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp <= 0xFFFF) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

void AppendUtf16LE(std::vector<uint8_t>& bytes, uint16_t unit) {
    bytes.push_back(static_cast<uint8_t>(unit & 0xFF));
    bytes.push_back(static_cast<uint8_t>((unit >> 8) & 0xFF));
}

std::vector<uint8_t> CodepointsToUtf16LEBytes(const std::vector<char32_t>& codepoints) {
    std::vector<uint8_t> bytes;
    bytes.reserve(codepoints.size() * 2);
    for (char32_t cp : codepoints) {
        if (cp <= 0xFFFF) {
            AppendUtf16LE(bytes, static_cast<uint16_t>(cp));
        } else {
            const char32_t v = cp - 0x10000;
            const uint16_t high = static_cast<uint16_t>(0xD800 + (v >> 10));
            const uint16_t low = static_cast<uint16_t>(0xDC00 + (v & 0x3FF));
            AppendUtf16LE(bytes, high);
            AppendUtf16LE(bytes, low);
        }
    }
    return bytes;
}

// Verbatim port of GeneratorText.cs's static `CleanupTable` (every active entry - the two
// commented-out emoji/surrogate-pair entries in the C# source are not ported, since they're
// dead code there too). Keys are Unicode code points, cross-checked against the C# source
// file's actual UTF-8 bytes (all entries fall in the Basic Multilingual Plane, U+00A1 through
// U+2055), not guessed from how the glyphs render.
const std::unordered_map<char32_t, std::string>& CleanupTable() {
    static const std::unordered_map<char32_t, std::string> table = {
        {0x2018, "\'"},  // LEFT SINGLE QUOTATION MARK
        {0x2019, "\'"},  // RIGHT SINGLE QUOTATION MARK
        {0x201C, "\""},  // LEFT DOUBLE QUOTATION MARK
        {0x201D, "''"},  // RIGHT DOUBLE QUOTATION MARK
        {0x201E, ",,"},  // DOUBLE LOW-9 QUOTATION MARK
        {0x201F, "\""},  // DOUBLE HIGH-REVERSED-9 QUOTATION MARK
        {0x2039, "<"},   // SINGLE LEFT-POINTING ANGLE QUOTATION MARK
        {0x203A, ">"},   // SINGLE RIGHT-POINTING ANGLE QUOTATION MARK
        {0x201A, ","},   // SINGLE LOW-9 QUOTATION MARK
        {0x201B, "'"},   // SINGLE HIGH-REVERSED-9 QUOTATION MARK
        {0x00A1, "!"},   // INVERTED EXCLAMATION MARK
        {0x00BF, "?"},   // INVERTED QUESTION MARK
        {0x203C, "!!"},  // DOUBLE EXCLAMATION MARK
        {0x2047, "??"},  // DOUBLE QUESTION MARK
        {0x2048, "?!"},  // QUESTION EXCLAMATION MARK
        {0x2049, "!?"},  // EXCLAMATION QUESTION MARK
        {0x203D, "?!"},  // INTERROBANG
        {0x2020, "+"},   // DAGGER
        {0x2021, "++"},  // DOUBLE DAGGER
        {0x2024, "."},   // ONE DOT LEADER
        {0x2025, ".."},  // TWO DOT LEADER
        {0x2026, "..."}, // HORIZONTAL ELLIPSIS
        {0x2030, "%"},   // PER MILLE SIGN
        {0x2031, "%%"},  // PER TEN THOUSAND SIGN
        {0x2032, "'"},   // PRIME
        {0x2033, "\""},  // DOUBLE PRIME
        {0x2034, "\""},  // TRIPLE PRIME
        {0x2035, "`"},   // REVERSED PRIME
        {0x2036, "\""},  // REVERSED DOUBLE PRIME
        {0x2037, "\""},  // REVERSED TRIPLE PRIME
        {0x2038, "^"},   // CARET
        {0x203B, "*"},   // REFERENCE MARK
        {0x2042, "***"}, // ASTERISM
        {0x2044, "/"},   // FRACTION SLASH
        {0x204E, "*"},   // LOW ASTERISK
        {0x204F, ";"},   // REVERSED SEMICOLON
        {0x2052, "%"},   // COMMERCIAL MINUS SIGN
        {0x2053, "~"},   // SWUNG DASH
        {0x2055, "*"},   // FLOWER PUNCTUATION MARK
        {0x2013, "-"},   // EN DASH
        {0x2014, "-"},   // EM DASH
        {0x200B, " "},   // ZERO WIDTH SPACE
        {0x00A0, " "},   // NO-BREAK SPACE
    };
    return table;
}

} // namespace

void TextGenerator::GenerateDMX(std::vector<uint8_t>& dmxData) {
    if (!unicode) {
        // Down-convert "smart" punctuation via CleanupTable, decoding `text` into Unicode
        // code points and rebuilding it. This mutates the stored `text` field itself
        // (matching the C# reference, which reassigns its `text` field rather than working
        // on a local copy) - after the first GenerateDMX() call with unicode==false, the
        // field is permanently down-converted.
        const std::vector<char32_t> codepoints = Utf8Decode(text);
        const auto& cleanupTable = CleanupTable();
        std::string cleaned;
        cleaned.reserve(text.size());
        for (char32_t cp : codepoints) {
            auto it = cleanupTable.find(cp);
            if (it != cleanupTable.end()) {
                cleaned += it->second;
            } else {
                Utf8AppendCodepoint(cleaned, cp);
            }
        }
        text = cleaned;
    }

    // limitLength/maxCharacters: the C# original truncates by UTF-16 `char` count
    // (text.Substring(0, maxCharacters)). `text` here is UTF-8, so truncating by raw byte
    // count could cut a multi-byte sequence in half and produce invalid UTF-8. Truncate by
    // Unicode code point count instead - a minor simplification vs. exact C# parity on this
    // cosmetic limit, but it guarantees `text` stays valid UTF-8. This also mutates the
    // stored field itself, matching the C# original.
    if (limitLength) {
        const std::vector<char32_t> codepoints = Utf8Decode(text);
        if (static_cast<long long>(codepoints.size()) > static_cast<long long>(maxCharacters)) {
            const size_t limit = maxCharacters > 0 ? static_cast<size_t>(maxCharacters) : 0;
            std::string truncated;
            truncated.reserve(text.size());
            for (size_t i = 0; i < limit && i < codepoints.size(); ++i) {
                Utf8AppendCodepoint(truncated, codepoints[i]);
            }
            text = truncated;
        }
    }

    std::vector<uint8_t> textBytes;
    if (unicode) {
        // .NET's Encoding.Unicode is UTF-16LE - actually transcode.
        textBytes = CodepointsToUtf16LEBytes(Utf8Decode(text));
    } else {
        // `text` is already UTF-8, so Encoding.UTF8.GetBytes(text) is effectively a no-op
        // here - just copy the bytes directly.
        textBytes.assign(text.begin(), text.end());
    }

    WriteDmxAtPosition(dmxData, textBytes, static_cast<size_t>(channelStart));
}

bool TextGenerator::DrawUi() {
    bool changed = false;
    changed |= ImGui::InputText("Text", &text);
    changed |= ImGui::InputInt("Channel Start", &channelStart);
    changed |= ImGui::Checkbox("Unicode", &unicode);
    changed |= ImGui::Checkbox("Limit Length", &limitLength);
    changed |= ImGui::InputInt("Length Limit", &maxCharacters);
    return changed;
}

void TextGenerator::ReadYaml(const YAML::Node& node) {
    try {
        if (node["text"]) text = node["text"].as<std::string>();
        if (node["channelStart"]) channelStart = node["channelStart"].as<int>();
        if (node["unicode"]) unicode = node["unicode"].as<bool>();
        if (node["limitLength"]) limitLength = node["limitLength"].as<bool>();
        if (node["maxCharacters"]) maxCharacters = node["maxCharacters"].as<int>();
    } catch (const YAML::Exception&) {
    }
}

void TextGenerator::WriteYaml(YAML::Emitter& out) const {
    out << YAML::Key << "text" << YAML::Value << text;
    out << YAML::Key << "channelStart" << YAML::Value << channelStart;
    out << YAML::Key << "unicode" << YAML::Value << unicode;
    out << YAML::Key << "limitLength" << YAML::Value << limitLength;
    out << YAML::Key << "maxCharacters" << YAML::Value << maxCharacters;
}

// ---------------------------------------------------------------------------------------
// TimeGenerator (port of Assets/Plugin/Generators/GeneratorTime.cs's `Time : Text`).
//
// .NET custom date/time format string translation below is a pragmatic subset, NOT a full
// implementation of .NET's DateTime.ToString(format) grammar. It handles runs of a repeated
// recognized letter (H/h/m/s/t/y/M/d) as a single token - e.g. "HH" is one 2-char run
// mapped to a zero-padded 24-hour hour - which is enough for the default "HH:mm:ss" and
// other simple combinations of these tokens. Anything not in that letter set (literal
// punctuation like ':', single letters used for other purposes, month/day *name* tokens
// like "MMM"/"dddd", .NET's single-quote literal-escaping syntax, culture-specific
// formatting, etc.) is not specially interpreted - non-recognized characters pass through
// unchanged, and a recognized-letter run just always maps through the same width-based
// number/text rule regardless of run length beyond 2 (so "MMM"/"MMMM" still produce a
// zero-padded numeric month rather than a month name).
namespace {

std::string PadNumber(int value, int width) {
    std::ostringstream oss;
    oss << std::setw(width) << std::setfill('0') << value;
    return oss.str();
}

bool IsTimeFormatToken(char c) {
    return c == 'H' || c == 'h' || c == 'm' || c == 's' || c == 't' || c == 'y' || c == 'M' || c == 'd';
}

std::string FormatTimeToken(char token, int runLength, const std::tm& tm) {
    switch (token) {
        case 'H': {
            const int hour24 = tm.tm_hour;
            return runLength >= 2 ? PadNumber(hour24, 2) : std::to_string(hour24);
        }
        case 'h': {
            int hour12 = tm.tm_hour % 12;
            if (hour12 == 0) hour12 = 12;
            return runLength >= 2 ? PadNumber(hour12, 2) : std::to_string(hour12);
        }
        case 'm':
            return runLength >= 2 ? PadNumber(tm.tm_min, 2) : std::to_string(tm.tm_min);
        case 's':
            return runLength >= 2 ? PadNumber(tm.tm_sec, 2) : std::to_string(tm.tm_sec);
        case 't': {
            const bool pm = tm.tm_hour >= 12;
            if (runLength >= 2) return pm ? "PM" : "AM";
            return pm ? "P" : "A";
        }
        case 'y': {
            const int year = tm.tm_year + 1900;
            if (runLength >= 4) return PadNumber(year, 4);
            const int year2 = year % 100;
            return runLength >= 2 ? PadNumber(year2, 2) : std::to_string(year2);
        }
        case 'M': {
            const int month = tm.tm_mon + 1;
            return runLength >= 2 ? PadNumber(month, 2) : std::to_string(month);
        }
        case 'd': {
            const int day = tm.tm_mday;
            return runLength >= 2 ? PadNumber(day, 2) : std::to_string(day);
        }
        default:
            return std::string(static_cast<size_t>(runLength), token);
    }
}

std::string FormatTime(const std::string& format, const std::tm& tm) {
    std::string result;
    result.reserve(format.size());
    size_t i = 0;
    while (i < format.size()) {
        const char c = format[i];
        if (IsTimeFormatToken(c)) {
            size_t j = i;
            while (j < format.size() && format[j] == c) ++j;
            result += FormatTimeToken(c, static_cast<int>(j - i), tm);
            i = j;
        } else {
            result += c;
            ++i;
        }
    }
    return result;
}

} // namespace

void TimeGenerator::GenerateDMX(std::vector<uint8_t>& dmxData) {
    const std::time_t now = std::time(nullptr);
    std::tm localTm{};
#if defined(_WIN32)
    localtime_s(&localTm, &now);
#else
    localtime_r(&now, &localTm);
#endif

    // Construct the text from the current local time, then defer to TextGenerator's own
    // GenerateDMX - mirrors `text = DateTime.Now.ToString(format); base.GenerateDMX(...)`.
    inner_.text = FormatTime(format, localTm);
    inner_.channelStart = channelStart;
    inner_.unicode = unicode;
    inner_.GenerateDMX(dmxData);
}

bool TimeGenerator::DrawUi() {
    // The C# original shows the inherited `text` field too, but disabled (it's overwritten
    // every frame). There's no exposed `text` field on TimeGenerator itself to show, so it's
    // simply omitted here rather than shown read-only.
    bool changed = false;
    changed |= ImGui::InputText("Format", &format);
    changed |= ImGui::InputInt("Channel Start", &channelStart);
    changed |= ImGui::Checkbox("Unicode", &unicode);
    return changed;
}

void TimeGenerator::ReadYaml(const YAML::Node& node) {
    try {
        if (node["format"]) format = node["format"].as<std::string>();
        if (node["channelStart"]) channelStart = node["channelStart"].as<int>();
        if (node["unicode"]) unicode = node["unicode"].as<bool>();
    } catch (const YAML::Exception&) {
    }
}

void TimeGenerator::WriteYaml(YAML::Emitter& out) const {
    out << YAML::Key << "format" << YAML::Value << format;
    out << YAML::Key << "channelStart" << YAML::Value << channelStart;
    out << YAML::Key << "unicode" << YAML::Value << unicode;
}
