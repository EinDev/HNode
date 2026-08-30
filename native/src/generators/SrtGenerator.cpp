#include "SrtGenerator.h"
#include "DmxUtil.h"
#include "TimeOfDay.h"

#include <yaml-cpp/yaml.h>
#include "imgui.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

// ---------------------------------------------------------------------------------------
// Port of Assets/Plugin/Generators/GeneratorSRT.cs. The C# reference parses the whole file
// with one big regex (`(?m)^\d+\n*((\d{2}):(\d{2}):(\d{2}),(\d{3})) --> ...`); here we
// hand-parse instead, which is simpler and just as correct for this well-defined format:
// split the file into blank-line-separated blocks, and for each block skip the numeric
// index line, parse the timestamp-range line, and join the remaining lines as the text.
namespace {

bool IsBlankLine(const std::string& s) {
    for (char c : s) {
        if (!std::isspace(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

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

// Normalizes CRLF/CR line endings to LF and splits into lines.
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

} // namespace

void SrtGenerator::Construct() {
    events_.clear();

    const std::string raw = ReadWholeFile(filePath);
    const std::vector<std::string> lines = SplitLinesNormalized(raw);

    // Group lines into blocks separated by one or more blank (whitespace-only) lines.
    std::vector<std::vector<std::string>> blocks;
    std::vector<std::string> current;
    for (const std::string& line : lines) {
        if (IsBlankLine(line)) {
            if (!current.empty()) {
                blocks.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(line);
        }
    }
    if (!current.empty()) blocks.push_back(current);

    for (const auto& block : blocks) {
        // block[0] is the numeric index (unused), block[1] is the timestamp range. Any
        // block that doesn't have at least those two lines, or whose timestamp line
        // doesn't match, is tolerated - skipped rather than treated as an error.
        if (block.size() < 2) continue;

        int h1 = 0, m1 = 0, s1 = 0, ms1 = 0, h2 = 0, m2 = 0, s2 = 0, ms2 = 0;
        const int parsed = std::sscanf(block[1].c_str(), "%d:%d:%d,%d --> %d:%d:%d,%d", &h1, &m1, &s1, &ms1, &h2, &m2,
                                        &s2, &ms2);
        if (parsed != 8) continue;

        std::string text;
        for (size_t i = 2; i < block.size(); ++i) {
            if (i > 2) text += "\n";
            text += block[i];
        }
        text = Trim(text);

        SrtEvent ev;
        ev.startMs = static_cast<double>((h1 * 3600 + m1 * 60 + s1) * 1000 + ms1);
        ev.endMs = static_cast<double>((h2 * 3600 + m2 * 60 + s2) * 1000 + ms2);
        ev.text = text;
        events_.push_back(std::move(ev));
    }

    // Faithful quirk (see header comment): currentEvent_ is deliberately NOT reset here.
    timeAtLoadMs_ = NowTimeOfDayMs();
}

void SrtGenerator::GenerateDMX(std::vector<uint8_t>& dmxData) {
    std::string text; // cleared each call, same as the C# reference's `text = "";`

    if (!events_.empty() && currentEvent_ < events_.size()) {
        const SrtEvent& ev = events_[currentEvent_];
        const double nowMs = NowTimeOfDayMs();
        const double elapsedMs = nowMs - timeAtLoadMs_; // == DateTime.Now.TimeOfDay - timeAtLoad

        float percentage = 0.0f;
        if (ev.endMs != ev.startMs) {
            percentage = static_cast<float>((elapsedMs - ev.startMs) / (ev.endMs - ev.startMs));
        }
        const int percentageValue = std::clamp(static_cast<int>(std::lround(percentage * 255.0f)), 0, 255);

        if (generateSubtitlePercentage && subtitlePercentChannel >= 0) {
            EnsureDmxCapacity(dmxData, static_cast<size_t>(subtitlePercentChannel) + 1);
            dmxData[static_cast<size_t>(subtitlePercentChannel)] = static_cast<uint8_t>(percentageValue);
        }

        // Only mode that exists for SRT: OnConfigLoad.
        if (elapsedMs > ev.startMs && elapsedMs < ev.endMs) {
            text = ev.text;
        }

        if (elapsedMs > ev.endMs) {
            // Keep going till we find a fresh event, since there might be multiple events
            // with the same times.
            while (currentEvent_ < events_.size() - 1 && elapsedMs > events_[currentEvent_ + 1].startMs) {
                ++currentEvent_;
            }
        }
    }

    inner_.text = text;
    inner_.channelStart = channelStart;
    inner_.unicode = unicode;
    inner_.limitLength = limitLength;
    inner_.maxCharacters = maxCharacters;
    inner_.GenerateDMX(dmxData);
}

bool SrtGenerator::DrawUi() {
    bool changed = false;

    // Mirrors base.ConstructUserInterface()'s fields, minus the text field itself (the C#
    // reference disables it, since GenerateDMX overwrites it every call).
    changed |= ImGui::InputInt("Channel Start", &channelStart);
    changed |= ImGui::Checkbox("Unicode", &unicode);
    changed |= ImGui::Checkbox("Limit Length", &limitLength);
    changed |= ImGui::InputInt("Length Limit", &maxCharacters);

    if (ImGui::InputText("File Path", &filePath)) {
        Construct(); // reload the SRT file, matching the C# onEndEdit callback
        changed = true;
    }
    if (ImGui::Button("Reload SRT File")) {
        Construct();
        changed = true;
    }

    changed |= ImGui::Checkbox("Generate Subtitle Percentage Channel", &generateSubtitlePercentage);
    changed |= ImGui::InputInt("Subtitle Percentage Channel", &subtitlePercentChannel);

    return changed;
}

void SrtGenerator::ReadYaml(const YAML::Node& node) {
    try {
        if (node["filePath"]) filePath = node["filePath"].as<std::string>();
        if (node["generateSubtitlePercentage"])
            generateSubtitlePercentage = node["generateSubtitlePercentage"].as<bool>();
        if (node["subtitlePercentChannel"]) subtitlePercentChannel = node["subtitlePercentChannel"].as<int>();
        if (node["channelStart"]) channelStart = node["channelStart"].as<int>();
        if (node["unicode"]) unicode = node["unicode"].as<bool>();
        if (node["limitLength"]) limitLength = node["limitLength"].as<bool>();
        if (node["maxCharacters"]) maxCharacters = node["maxCharacters"].as<int>();
    } catch (const YAML::Exception&) {
    }

    // Load the file now that filePath is known, so events_ isn't left empty until the
    // user manually hits "Reload SRT File".
    Construct();
}

void SrtGenerator::WriteYaml(YAML::Emitter& out) const {
    out << YAML::Key << "filePath" << YAML::Value << filePath;
    out << YAML::Key << "generateSubtitlePercentage" << YAML::Value << generateSubtitlePercentage;
    out << YAML::Key << "subtitlePercentChannel" << YAML::Value << subtitlePercentChannel;
    out << YAML::Key << "channelStart" << YAML::Value << channelStart;
    out << YAML::Key << "unicode" << YAML::Value << unicode;
    out << YAML::Key << "limitLength" << YAML::Value << limitLength;
    out << YAML::Key << "maxCharacters" << YAML::Value << maxCharacters;
}
