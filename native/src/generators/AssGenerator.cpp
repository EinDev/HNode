#include "AssGenerator.h"
#include "TimeOfDay.h"

#include <yaml-cpp/yaml.h>
#include "imgui.h"
#include "imgui_stdlib.h"

#include <cctype>
#include <cstdint>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------------------
// Port of Assets/Plugin/Generators/GeneratorASS.cs (`class ASS : Text`). See the header
// comment for the byte-vs-codepoint quirk this file has to reproduce.
//
// All free helpers below work with plain primitives (uint8_t/std::string/etc.) rather than
// AssGenerator's private nested Color/Style/AssEvent types, since a free function outside
// the class has no access to those (they're private). The actual construction of
// Color/Style/AssEvent values happens inside AssGenerator's own member functions (and in
// local lambdas defined inside them, which - being lexically part of the member function -
// share its access to the class's private members).
namespace {

// Appends the UTF-8 encoding of the Unicode code point equal to `value` (0-255) - mirrors
// C#'s `(char)value` (a UTF-16 code unit) followed by Encoding.UTF8.GetBytes on it. For
// value < 0x80 this is one raw byte; for value >= 0x80 it's the 2-byte UTF-8 encoding of
// that code point, NOT the raw byte value. See the header comment / .h for why.
void EncodeByteAsCodepoint(std::string& out, uint8_t value) {
    if (value < 0x80) {
        out.push_back(static_cast<char>(value));
    } else {
        out.push_back(static_cast<char>(0xC0 | (value >> 6)));
        out.push_back(static_cast<char>(0x80 | (value & 0x3F)));
    }
}

// Appends a Color's DMX-string encoding (Extensions.cs's Color.ToDMXString(): r,g,b,a in
// that order, each byte codepoint-encoded).
void AppendColorCodepoints(std::string& out, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    EncodeByteAsCodepoint(out, r);
    EncodeByteAsCodepoint(out, g);
    EncodeByteAsCodepoint(out, b);
    EncodeByteAsCodepoint(out, a);
}

std::string Trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return s.substr(start, end - start);
}

// Splits on every occurrence of `delim`, keeping empty fields (matches C#'s
// `string.Split(char)` with no StringSplitOptions - used for both Style/Dialogue lines,
// which rely on empty fields being preserved positionally).
std::vector<std::string> SplitAll(const std::string& s, char delim) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == delim) {
            out.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}

// Reads the whole file at `path` as text. Returns an empty string (no error) if the file
// doesn't exist or can't be opened - matches the C# reference's "log a warning, continue
// with an empty string" behavior.
std::string ReadWholeFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::string();
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

// Splits on '\n' only and drops entries that are exactly empty - mirrors C#'s
// `rawfile.Split(new[] { '\n' }, StringSplitOptions.RemoveEmptyEntries)` verbatim
// (deliberately NOT normalizing/stripping a trailing '\r' the way SrtGenerator/LrcGenerator
// do - the ASS parser's section lookup depends on exact line *indices* lining up with the
// C# reference's split result, including a CRLF file's lines each carrying a trailing '\r'
// that only gets removed later by Trim()).
std::vector<std::string> SplitLinesRemoveEmpty(const std::string& raw) {
    std::vector<std::string> lines;
    std::string current;
    for (char c : raw) {
        if (c == '\n') {
            if (!current.empty()) lines.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) lines.push_back(current);
    return lines;
}

bool ParseHexByte(const std::string& s, uint8_t& out) {
    if (s.size() != 2) return false;
    auto hexVal = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    const int hi = hexVal(s[0]);
    const int lo = hexVal(s[1]);
    if (hi < 0 || lo < 0) return false;
    out = static_cast<uint8_t>((hi << 4) | lo);
    return true;
}

// Port of Extensions.cs's ColorFromHex (as free out-params rather than a private Color
// return, per the note above). Handles &H-prefixed BBGGRR / AABBGGRR (with the source
// format's inverted alpha: 00 = opaque, FF = transparent). On any parse failure
// (wrong length, non-hex digits) this leaves r=g=b=0,a=255 (opaque black) rather than
// throwing - a defensive deviation from the C#'s FormatException, matching the task's
// "don't crash on malformed input" guidance.
void ColorFromHexBytes(std::string hex, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
    r = 0;
    g = 0;
    b = 0;
    a = 255;

    if (hex.rfind("&H", 0) == 0) hex = hex.substr(2);

    if (hex.size() == 6) {
        uint8_t bb = 0, gg = 0, rr = 0;
        if (ParseHexByte(hex.substr(0, 2), bb) && ParseHexByte(hex.substr(2, 2), gg) &&
            ParseHexByte(hex.substr(4, 2), rr)) {
            b = bb;
            g = gg;
            r = rr;
            a = 255;
        }
    } else if (hex.size() == 8) {
        uint8_t aa = 0, bb = 0, gg = 0, rr = 0;
        if (ParseHexByte(hex.substr(0, 2), aa) && ParseHexByte(hex.substr(2, 2), bb) &&
            ParseHexByte(hex.substr(4, 2), gg) && ParseHexByte(hex.substr(6, 2), rr)) {
            a = static_cast<uint8_t>(255 - aa);
            b = bb;
            g = gg;
            r = rr;
        }
    }
    // Any other length: leave the opaque-black default rather than throwing.
}

// Parses ASS's "h:mm:ss.ff" timestamp format (h = hours, ff = HUNDREDTHS of a second, not
// milliseconds - same "hundredths" note as LrcGenerator's tags). Returns false (leaving
// outMs untouched) on any parse failure.
bool ParseAssTime(const std::string& raw, double& outMs) {
    const std::string s = Trim(raw);
    const std::vector<std::string> hms = SplitAll(s, ':');
    if (hms.size() != 3) return false;
    const std::vector<std::string> secFrac = SplitAll(hms[2], '.');
    if (secFrac.size() != 2) return false;

    try {
        const int h = std::stoi(hms[0]);
        const int m = std::stoi(hms[1]);
        const int sec = std::stoi(secFrac[0]);
        const int hundredths = std::stoi(secFrac[1]);
        outMs = (static_cast<double>(h) * 3600.0 + static_cast<double>(m) * 60.0 + static_cast<double>(sec)) *
                    1000.0 +
                static_cast<double>(hundredths) * 10.0;
        return true;
    } catch (...) {
        return false;
    }
}

// Formats a float roughly like C#'s default `float.ToString()` (a compact, trailing-zero-
// free decimal representation) - exact round-trip parity isn't required here (these values
// feed a downstream decoder that presumably re-parses them as plain decimal text), just a
// reasonably clean rendering. Using the default stream formatting (~6 significant digits,
// no forced trailing zeros) gets close enough for the typical small style values (font
// size, scale, spacing, angle, outline, shadow) this is used for.
std::string FormatFloat(float value) {
    std::ostringstream oss;
    oss << value;
    return oss.str();
}

bool MatchLiteral(const std::string& text, size_t pos, const std::string& lit) {
    if (pos > text.size()) return false;
    if (lit.size() > text.size() - pos) return false;
    return text.compare(pos, lit.size(), lit) == 0;
}

// Hand-scanning replacement for ASSEvent's `alphaPattern` regex:
//   {\alpha&H(..)&\c&H(..)(..)(..)&}  ->  { <ToDMXString-equivalent of the color> }
// Captured hex byte pairs are (alpha, b, g, r) in that order; alpha is inverted (source
// format: 00 = opaque, FF = transparent) before being folded into the output color's alpha
// channel. Only a substring that matches the WHOLE literal tag (fixed prefix, two hex
// digits, fixed middle literal, three more hex-digit pairs, fixed suffix) is replaced -
// anything else starting with the same "{\alpha&H" prefix but not completing the pattern
// is left as literal text, mirroring regex non-match-at-this-position behavior.
std::string ReplaceAlphaTags(const std::string& text) {
    static const std::string kPrefix = "{\\alpha&H";
    static const std::string kMid = "&\\c&H";
    static const std::string kSuffix = "&}";

    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    const size_t n = text.size();

    while (i < n) {
        const size_t start = text.find(kPrefix, i);
        if (start == std::string::npos) {
            out.append(text, i, n - i);
            break;
        }
        out.append(text, i, start - i);

        size_t pos = start + kPrefix.size();
        uint8_t a = 0, b = 0, g = 0, r = 0;
        bool ok = pos + 2 <= n && ParseHexByte(text.substr(pos, 2), a);
        if (ok) pos += 2;
        ok = ok && MatchLiteral(text, pos, kMid);
        if (ok) pos += kMid.size();
        ok = ok && pos + 2 <= n && ParseHexByte(text.substr(pos, 2), b);
        if (ok) pos += 2;
        ok = ok && pos + 2 <= n && ParseHexByte(text.substr(pos, 2), g);
        if (ok) pos += 2;
        ok = ok && pos + 2 <= n && ParseHexByte(text.substr(pos, 2), r);
        if (ok) pos += 2;
        ok = ok && MatchLiteral(text, pos, kSuffix);
        if (ok) pos += kSuffix.size();

        if (ok) {
            const uint8_t invA = static_cast<uint8_t>(255 - a);
            out.push_back('{');
            AppendColorCodepoints(out, r, g, b, invA);
            out.push_back('}');
            i = pos;
        } else {
            // Didn't complete the pattern - keep the '{' that started the literal prefix
            // as plain text and resume searching right after it (a later occurrence of the
            // prefix, even one overlapping what we just emitted, can still match).
            out.push_back(text[start]);
            i = start + 1;
        }
    }

    return out;
}

// Hand-scanning replacement for ASSEvent's `colorPattern` regex:
//   {.?\c&H(..)(..)(..)&}  ->  { <ToDMXString-equivalent of the color, alpha = 255> }
// `.?` is an optional single arbitrary character between '{' and the '\c&H' literal (greedy
// - prefers consuming one character over zero when both would otherwise complete the
// pattern, matching regex backtracking order). Captured hex byte pairs are (b, g, r).
std::string ReplaceColorTags(const std::string& text) {
    static const std::string kColorLit = "\\c&H";
    static const std::string kSuffix = "&}";

    auto tryMatch = [&](size_t litPos, uint8_t& b, uint8_t& g, uint8_t& r, size_t& matchEnd) -> bool {
        if (!MatchLiteral(text, litPos, kColorLit)) return false;
        size_t pos = litPos + kColorLit.size();
        const size_t n = text.size();
        uint8_t bb = 0, gg = 0, rr = 0;
        if (!(pos + 2 <= n && ParseHexByte(text.substr(pos, 2), bb))) return false;
        pos += 2;
        if (!(pos + 2 <= n && ParseHexByte(text.substr(pos, 2), gg))) return false;
        pos += 2;
        if (!(pos + 2 <= n && ParseHexByte(text.substr(pos, 2), rr))) return false;
        pos += 2;
        if (!MatchLiteral(text, pos, kSuffix)) return false;
        pos += kSuffix.size();
        b = bb;
        g = gg;
        r = rr;
        matchEnd = pos;
        return true;
    };

    std::string out;
    out.reserve(text.size());
    const size_t n = text.size();
    size_t i = 0;

    while (i < n) {
        if (text[i] != '{') {
            out.push_back(text[i]);
            ++i;
            continue;
        }

        const size_t s = i;
        uint8_t b = 0, g = 0, r = 0;
        size_t matchEnd = 0;
        // Greedy: try consuming one optional character at s+1 first, then fall back to zero.
        const bool matched = tryMatch(s + 2, b, g, r, matchEnd) || tryMatch(s + 1, b, g, r, matchEnd);

        if (matched) {
            out.push_back('{');
            AppendColorCodepoints(out, r, g, b, 255);
            out.push_back('}');
            i = matchEnd;
        } else {
            out.push_back('{');
            ++i;
        }
    }

    return out;
}

} // namespace

void AssGenerator::Construct() {
    title_.clear();
    scriptType_.clear();
    subtitler_.clear();
    styles_.clear();
    events_.clear();

    const std::string raw = ReadWholeFile(filePath);
    const std::vector<std::string> lines = SplitLinesRemoveEmpty(raw);

    // Metadata: scan raw (untrimmed) lines for these prefixes, same as the C# reference.
    for (const std::string& line : lines) {
        if (line.rfind("Title: ", 0) == 0) {
            title_ = Trim(line.substr(7));
        } else if (line.rfind("ScriptType: ", 0) == 0) {
            scriptType_ = Trim(line.substr(12));
        } else if (line.rfind("Original Translation: ", 0) == 0) {
            subtitler_ = Trim(line.substr(22));
        }
    }

    // Local parse helpers - defined as lambdas inside this member function so they share
    // Construct()'s access to the private Style/AssEvent/Color nested types (a free
    // function outside the class can't name those types at all).
    auto parseStyleLine = [](const std::string& rawLine, int index) -> std::optional<Style> {
        const std::vector<std::string> parts = SplitAll(rawLine, ',');
        if (parts.size() < 23) return std::nullopt;

        Style style;
        style.styleIndex = index;
        style.name = Trim(parts[0]);
        style.fontname = Trim(parts[1]);

        try {
            style.fontSize = std::stof(Trim(parts[2]));

            uint8_t r = 0, g = 0, b = 0, a = 255;
            ColorFromHexBytes(Trim(parts[3]), r, g, b, a);
            style.primaryColour = Color{r, g, b, a};
            ColorFromHexBytes(Trim(parts[4]), r, g, b, a);
            style.secondaryColour = Color{r, g, b, a};
            ColorFromHexBytes(Trim(parts[5]), r, g, b, a);
            style.outlineColour = Color{r, g, b, a};
            ColorFromHexBytes(Trim(parts[6]), r, g, b, a);
            style.backColour = Color{r, g, b, a};

            style.bold = Trim(parts[7]) == "-1";
            style.italic = Trim(parts[8]) == "-1";
            style.underline = Trim(parts[9]) == "-1";
            style.strikeout = Trim(parts[10]) == "-1";

            style.scaleX = std::stof(Trim(parts[11]));
            style.scaleY = std::stof(Trim(parts[12]));
            style.spacing = std::stof(Trim(parts[13]));
            style.angle = std::stof(Trim(parts[14]));
            style.borderStyle = std::stoi(Trim(parts[15]));
            style.outline = std::stof(Trim(parts[16]));
            style.shadow = std::stof(Trim(parts[17]));
            style.alignment = std::stoi(Trim(parts[18]));
            style.marginL = std::stof(Trim(parts[19]));
            style.marginR = std::stof(Trim(parts[20]));
            style.marginV = std::stof(Trim(parts[21]));
            style.encoding = std::stoi(Trim(parts[22]));
        } catch (...) {
            return std::nullopt;
        }

        return style;
    };

    auto parseEventLine = [](const std::string& rawLine, const std::vector<Style>& styleList) -> std::optional<AssEvent> {
        const std::vector<std::string> parts = SplitAll(rawLine, ',');
        if (parts.size() < 10) return std::nullopt;

        AssEvent ev;
        try {
            ev.layer = std::stoi(Trim(parts[0]));

            double startMs = 0.0, endMs = 0.0;
            if (!ParseAssTime(parts[1], startMs)) return std::nullopt;
            if (!ParseAssTime(parts[2], endMs)) return std::nullopt;
            ev.startMs = startMs;
            ev.endMs = endMs;

            // Style is a name reference - resolve to an index by linear search. -1 (not
            // found) is left as-is rather than throwing (the C# would IndexOutOfRange on
            // `styles[styleIndex]` here) - downstream code must guard against -1.
            const std::string styleName = Trim(parts[3]);
            ev.styleIndex = -1;
            for (size_t i = 0; i < styleList.size(); ++i) {
                if (styleList[i].name == styleName) {
                    ev.styleIndex = static_cast<int>(i);
                    break;
                }
            }

            ev.name = Trim(parts[4]);
            ev.marginL = std::stof(Trim(parts[5]));
            ev.marginR = std::stof(Trim(parts[6]));
            ev.marginV = std::stof(Trim(parts[7]));
            ev.effect = Trim(parts[8]);

            // Rejoin everything from field index 9 onward with ',' - dialogue text may
            // itself contain literal commas.
            std::string text;
            for (size_t i = 9; i < parts.size(); ++i) {
                if (i > 9) text += ",";
                text += parts[i];
            }
            text = Trim(text);

            if (text.rfind("region=", 0) == 0) {
                const size_t comma = text.find(',');
                if (comma != std::string::npos) {
                    text = Trim(text.substr(comma + 1));
                } else {
                    text = "";
                }
            }

            // Two sequential full passes, alpha-tag pattern first, matching the C#
            // reference's two separate Regex.Replace calls in that order.
            text = ReplaceAlphaTags(text);
            text = ReplaceColorTags(text);

            ev.text = text;
        } catch (...) {
            return std::nullopt;
        }

        return ev;
    };

    // [V4+ Styles] section: style lines start 2 lines after the header (skipping the
    // header itself and the "Format:" line right after it), up to (not including) the
    // line that starts with "[Events]". Defensive: if the header isn't found at all,
    // styles_ is simply left empty rather than indexing with -1+2.
    size_t styleStart = std::string::npos;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (Trim(lines[i]) == "[V4+ Styles]") {
            styleStart = i;
            break;
        }
    }

    if (styleStart != std::string::npos) {
        const size_t begin = styleStart + 2;
        size_t end = lines.size();
        for (size_t i = begin; i < lines.size(); ++i) {
            if (Trim(lines[i]).rfind("[Events]", 0) == 0) {
                end = i;
                break;
            }
        }
        for (size_t i = begin; i < end && i < lines.size(); ++i) {
            const std::string trimmed = Trim(lines[i]);
            if (trimmed.rfind("Style: ", 0) == 0) {
                auto parsed = parseStyleLine(Trim(trimmed.substr(7)), static_cast<int>(styles_.size()));
                if (parsed) styles_.push_back(*parsed);
            }
        }
    }

    // [Events] section: dialogue lines start 2 lines after the header, through end of
    // file. Same defensive "leave empty if header missing" handling as above.
    size_t eventStart = std::string::npos;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (Trim(lines[i]) == "[Events]") {
            eventStart = i;
            break;
        }
    }

    if (eventStart != std::string::npos) {
        const size_t begin = eventStart + 2;
        for (size_t i = begin; i < lines.size(); ++i) {
            const std::string trimmed = Trim(lines[i]);
            if (trimmed.rfind("Dialogue: ", 0) == 0) {
                auto parsed = parseEventLine(Trim(trimmed.substr(10)), styles_);
                if (parsed) events_.push_back(*parsed);
            }
        }
    }

    timeAtLoadMs_ = NowTimeOfDayMs();
}

void AssGenerator::GenerateDMX(std::vector<uint8_t>& dmxData) {
    std::string text; // cleared each call, matches the C# reference's `text = "";`

    const double timeInMs = NowTimeOfDayMs() - timeAtLoadMs_;

    // First pass: which events are currently active (start <= now <= end, inclusive both
    // ends, matching ASSEvent.IsActive).
    std::vector<const AssEvent*> activeEvents;
    for (const auto& e : events_) {
        if (timeInMs >= e.startMs && timeInMs <= e.endMs) activeEvents.push_back(&e);
    }

    if (!activeEvents.empty()) {
        // GetActiveStyles-equivalent: scan ALL events_ in file order (not just the
        // already-collected activeEvents list - matches the C# reference, which iterates
        // `file.events` from scratch here), and for each active event's style, add it to
        // the output list the first time that style is encountered. Distinct styles are
        // tracked by index rather than by full value-equality (as the C# struct's default
        // Equals would do) - since each index maps to exactly one style in this file, this
        // produces the same set and the same first-active-event-in-file-order ordering.
        // Events whose style name didn't resolve (styleIndex == -1) are skipped here,
        // since there's no style to add - a defensive guard the C# reference doesn't need
        // (there, an unresolved style name would already have thrown in the constructor).
        std::vector<size_t> activeStyleIndices;
        std::vector<bool> included(styles_.size(), false);
        for (const auto& e : events_) {
            if (e.styleIndex < 0 || static_cast<size_t>(e.styleIndex) >= styles_.size()) continue;
            if (timeInMs >= e.startMs && timeInMs <= e.endMs) {
                const size_t idx = static_cast<size_t>(e.styleIndex);
                if (!included[idx]) {
                    included[idx] = true;
                    activeStyleIndices.push_back(idx);
                }
            }
        }

        for (size_t idx : activeStyleIndices) {
            const Style& s = styles_[idx];

            uint8_t flags = 0;
            if (s.bold) flags |= 0x01;
            if (s.italic) flags |= 0x02;
            if (s.underline) flags |= 0x04;
            if (s.strikeout) flags |= 0x08;

            EncodeByteAsCodepoint(text, static_cast<uint8_t>(s.styleIndex));
            text += "|";
            text += s.fontname;
            text += "|";
            text += FormatFloat(s.fontSize);
            text += "|";
            AppendColorCodepoints(text, s.primaryColour.r, s.primaryColour.g, s.primaryColour.b, s.primaryColour.a);
            text += "|";
            AppendColorCodepoints(text, s.secondaryColour.r, s.secondaryColour.g, s.secondaryColour.b,
                                   s.secondaryColour.a);
            text += "|";
            AppendColorCodepoints(text, s.outlineColour.r, s.outlineColour.g, s.outlineColour.b, s.outlineColour.a);
            text += "|";
            AppendColorCodepoints(text, s.backColour.r, s.backColour.g, s.backColour.b, s.backColour.a);
            text += "|";
            EncodeByteAsCodepoint(text, flags);
            text += "|";
            text += FormatFloat(s.scaleX);
            text += "|";
            text += FormatFloat(s.scaleY);
            text += "|";
            text += FormatFloat(s.spacing);
            text += "|";
            text += FormatFloat(s.angle);
            text += "|";
            text += std::to_string(s.borderStyle);
            text += "|";
            text += FormatFloat(s.outline);
            text += "|";
            text += FormatFloat(s.shadow);
            text += "|";
            EncodeByteAsCodepoint(text, static_cast<uint8_t>(s.alignment));
            text += "^";
        }

        text += "@";

        for (const AssEvent* e : activeEvents) {
            // styleIndex may be -1 (unresolved style name) - cast to uint8_t wraps to 255,
            // a non-crashing sentinel rather than the C#'s would-be crash on construction.
            EncodeByteAsCodepoint(text, static_cast<uint8_t>(e->styleIndex));
            text += "|";
            text += e->text;
            text += "^";
        }
    }

    // The C# reference's remaining lines (~45-52) build a debug-only "strippedText" via a
    // second regex pass purely for a Debug.Log call - it has no effect on `text`/DMX
    // output, so there's nothing to port.

    inner_.text = text;
    inner_.channelStart = channelStart;
    inner_.unicode = unicode;
    inner_.limitLength = limitLength;
    inner_.maxCharacters = maxCharacters;
    inner_.GenerateDMX(dmxData);
}

bool AssGenerator::DrawUi() {
    bool changed = false;

    // Mirrors base.ConstructUserInterface()'s fields, minus the text field itself (the C#
    // reference disables it - GenerateDMX overwrites it every call).
    changed |= ImGui::InputInt("Channel Start", &channelStart);
    changed |= ImGui::Checkbox("Unicode", &unicode);
    changed |= ImGui::Checkbox("Limit Length", &limitLength);
    changed |= ImGui::InputInt("Length Limit", &maxCharacters);

    if (ImGui::InputText("File Path", &filePath)) {
        Construct(); // reload the ASS file, matching the C# reference's onEndEdit pattern
        changed = true;
    }

    return changed;
}

void AssGenerator::ReadYaml(const YAML::Node& node) {
    try {
        if (node["filePath"]) filePath = node["filePath"].as<std::string>();
        if (node["channelStart"]) channelStart = node["channelStart"].as<int>();
        if (node["unicode"]) unicode = node["unicode"].as<bool>();
        if (node["limitLength"]) limitLength = node["limitLength"].as<bool>();
        if (node["maxCharacters"]) maxCharacters = node["maxCharacters"].as<int>();
    } catch (const YAML::Exception&) {
    }

    // Load the file now that filePath is known, so styles_/events_ isn't left empty until
    // some later manual reload (same pattern as SrtGenerator/LrcGenerator).
    Construct();
}

void AssGenerator::WriteYaml(YAML::Emitter& out) const {
    out << YAML::Key << "filePath" << YAML::Value << filePath;
    out << YAML::Key << "channelStart" << YAML::Value << channelStart;
    out << YAML::Key << "unicode" << YAML::Value << unicode;
    out << YAML::Key << "limitLength" << YAML::Value << limitLength;
    out << YAML::Key << "maxCharacters" << YAML::Value << maxCharacters;
}
