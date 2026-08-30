#include "LrcGenerator.h"
#include "DmxUtil.h"
#include "TimeOfDay.h"

#include <yaml-cpp/yaml.h>
#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
#include <sstream>

// ---------------------------------------------------------------------------------------
// Port of Assets/Plugin/Generators/GeneratorLRC.cs. Unlike SrtGenerator, a std::regex is
// used here to find the `[MM:SS]`/`[MM:SS.CC]` tags - a line can carry multiple tags before
// its lyric text (e.g. "[00:12.00][00:17.20][00:20.00]Line of lyrics"), each producing a
// separate LrcEvent sharing the same (tag-stripped) text, which maps directly onto
// std::regex_replace (strip all tags -> text) + std::sregex_iterator (enumerate all tags).
namespace {

std::string Trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return s.substr(start, end - start);
}

// Reads the whole file at `path` as text. Returns an empty string (no error) if the file
// doesn't exist or can't be opened - matches the C# reference's
// "log a warning, continue with an empty string" behavior.
std::string ReadWholeFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::string();
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

// Normalizes CRLF/CR line endings to LF and splits into lines (mirrors the C# reference's
// `lyricsraw.Split(new[] { "\r\n", "\r", "\n" }, StringSplitOptions.None)`).
std::vector<std::string> SplitLinesNormalized(const std::string& raw) {
    std::string normalized;
    normalized.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '\r') {
            normalized += '\n';
            if (i + 1 < raw.size() && raw[i + 1] == '\n') ++i;
        } else {
            normalized += raw[i];
        }
    }

    std::vector<std::string> lines;
    size_t start = 0;
    while (true) {
        const size_t pos = normalized.find('\n', start);
        if (pos == std::string::npos) {
            lines.push_back(normalized.substr(start));
            break;
        }
        lines.push_back(normalized.substr(start, pos - start));
        start = pos + 1;
    }
    return lines;
}

const std::regex& TagPattern() {
    static const std::regex pattern(R"(\[(\d{2}):(\d{2})(?:\.(\d{2}))?\])");
    return pattern;
}

std::string ModeToString(LrcGenerator::Mode mode) {
    switch (mode) {
        case LrcGenerator::Mode::StrobeTrigger:
            return "StrobeTrigger";
        case LrcGenerator::Mode::PlayTrigger:
            return "PlayTrigger";
        case LrcGenerator::Mode::OnConfigLoad:
            return "OnConfigLoad";
    }
    return "OnConfigLoad";
}

bool ParseMode(const std::string& text, LrcGenerator::Mode& out) {
    if (text == "StrobeTrigger") {
        out = LrcGenerator::Mode::StrobeTrigger;
        return true;
    }
    if (text == "PlayTrigger") {
        out = LrcGenerator::Mode::PlayTrigger;
        return true;
    }
    if (text == "OnConfigLoad") {
        out = LrcGenerator::Mode::OnConfigLoad;
        return true;
    }
    return false;
}

} // namespace

void LrcGenerator::Construct() {
    // FAITHFUL QUIRK (see header comment): events_ is deliberately NOT cleared here, so
    // repeated Construct() calls append rather than replace.

    const std::string raw = ReadWholeFile(filePath);
    const std::vector<std::string> lines = SplitLinesNormalized(raw);

    const std::regex& tagPattern = TagPattern();
    for (const std::string& line : lines) {
        std::string lyricText = Trim(std::regex_replace(line, tagPattern, ""));

        auto begin = std::sregex_iterator(line.begin(), line.end(), tagPattern);
        auto end = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            const std::smatch& m = *it;
            const int minutes = std::stoi(m[1].str());
            const int seconds = std::stoi(m[2].str());
            const int hundredths = m[3].matched ? std::stoi(m[3].str()) : 0; // hundredths, not ms
            const double timestampMs =
                static_cast<double>((minutes * 60 + seconds) * 1000) + static_cast<double>(hundredths) * 10.0;

            LrcEvent ev;
            ev.timestampMs = timestampMs;
            ev.text = lyricText;
            events_.push_back(std::move(ev));
        }
    }

    // Unlike events_/currentEvent_, timeAtLoad IS reset on every Construct() call in the
    // C# reference.
    timeAtLoadMs_ = NowTimeOfDayMs();
}

void LrcGenerator::GenerateDMX(std::vector<uint8_t>& dmxData) {
    // Note: unlike SrtGenerator, the C# reference does NOT clear `text` at the top of
    // GenerateDMX - it's only assigned inside the `if` below, so when there are no events
    // (or currentEvent has run past the end) `inner_.text` simply keeps whatever value it
    // last held. Faithfully left alone here.
    if (!events_.empty() && currentEvent_ < events_.size()) {
        inner_.text = events_[currentEvent_].text;

        const double nowMs = NowTimeOfDayMs();
        const size_t triggerIndex = static_cast<size_t>(std::max(triggerChannel, 0));

        switch (mode) {
            case Mode::OnConfigLoad: {
                const double elapsedMs = nowMs - timeAtLoadMs_;
                if (elapsedMs > events_[currentEvent_].timestampMs) {
                    ++currentEvent_;
                }
                break;
            }
            case Mode::StrobeTrigger: {
                EnsureDmxCapacity(dmxData, triggerIndex + 1);
                const uint8_t triggerValue = dmxData[triggerIndex];
                if (lastTriggerValue_ != triggerValue) {
                    lastTriggerValue_ = triggerValue;
                    ++currentEvent_;
                }
                break;
            }
            case Mode::PlayTrigger: {
                EnsureDmxCapacity(dmxData, triggerIndex + 1);
                const uint8_t triggerValue = dmxData[triggerIndex];
                if (triggerValue < 127) {
                    // Start playback.
                    timeAtLoadMs_ = nowMs;
                    currentEvent_ = 0;
                } else {
                    const double elapsedMs = nowMs - timeAtLoadMs_;
                    if (elapsedMs > events_[currentEvent_].timestampMs) {
                        ++currentEvent_;
                    }
                }
                break;
            }
        }
        // currentEvent_ may now sit at events_.size() (or, defensively, beyond it) after an
        // increment above - deliberately not clamped further here: the guard at the top of
        // this function (`currentEvent_ < events_.size()`) makes that a no-op on the next
        // call rather than an out-of-bounds read, same as SrtGenerator.
    }

    inner_.GenerateDMX(dmxData);
}

bool LrcGenerator::DrawUi() {
    // The C# reference has no ConstructUserInterface override for LRC at all - it inherits
    // Text's unmodified UI (raw text field included). filePath/mode/triggerChannel have no
    // UI in the reference either, so nothing extra is drawn here.
    return inner_.DrawUi();
}

void LrcGenerator::ReadYaml(const YAML::Node& node) {
    try {
        if (node["filePath"]) filePath = node["filePath"].as<std::string>();
        if (node["mode"]) {
            Mode parsedMode;
            if (ParseMode(node["mode"].as<std::string>(), parsedMode)) mode = parsedMode;
        }
        if (node["triggerChannel"]) triggerChannel = node["triggerChannel"].as<int>();
    } catch (const YAML::Exception&) {
    }

    // Load the file now that filePath is known, so events_ isn't left empty until the
    // user manually reloads.
    Construct();
}

void LrcGenerator::WriteYaml(YAML::Emitter& out) const {
    out << YAML::Key << "filePath" << YAML::Value << filePath;
    out << YAML::Key << "mode" << YAML::Value << ModeToString(mode);
    out << YAML::Key << "triggerChannel" << YAML::Value << triggerChannel;
}
