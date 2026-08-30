#include "TextFileExporter.h"

#include <fstream>
#include <yaml-cpp/yaml.h>

#include "../ui/FileDialog.h"

#include "imgui.h"

namespace {
// Matches DMXChannel.cs's implicit string conversion, also used by ShowConfig.cpp.
std::string FormatDmxChannel(int globalChannel) {
    int universe = (globalChannel / 512) + 1;
    int channelInUniverse = globalChannel - (universe - 1) * 512 + 1;
    return std::to_string(universe) + "." + std::to_string(channelInUniverse);
}
} // namespace

bool TextFileExporter::DrawUi() {
    bool changed = ImGui::Checkbox("Only export non-zero channels", &onlyNonZeroChannels);

    if (ImGui::Button("Export channels to text file")) {
        std::wstring path;
        if (ShowSaveFileDialog(GetActiveWindow(), "Channel Information", "chinfo", "channelinfo.chinfo", path)) {
            std::ofstream file(WideToNarrow(path), std::ios::binary | std::ios::trunc);
            for (size_t i = 0; i < data_.size(); ++i) {
                if (onlyNonZeroChannels && data_[i] == 0) continue;
                file << FormatDmxChannel(static_cast<int>(i)) << ": " << static_cast<int>(data_[i]) << "\n";
            }
        }
    }

    return changed;
}

void TextFileExporter::ReadYaml(const YAML::Node& node) {
    if (node["onlyNonZeroChannels"]) {
        try {
            onlyNonZeroChannels = node["onlyNonZeroChannels"].as<bool>();
        } catch (const YAML::Exception&) {
        }
    }
}

void TextFileExporter::WriteYaml(YAML::Emitter& out) const {
    out << YAML::Key << "onlyNonZeroChannels" << YAML::Value << onlyNonZeroChannels;
}
