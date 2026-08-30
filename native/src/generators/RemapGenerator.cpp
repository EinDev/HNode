#include "RemapGenerator.h"
#include "DmxUtil.h"

#include <yaml-cpp/yaml.h>
#include "imgui.h"

void RemapGenerator::GenerateDMX(std::vector<uint8_t>& dmxData) {
    if (sourceChannelLength <= 0) return;
    EnsureDmxCapacity(dmxData, static_cast<size_t>(targetChannel) + static_cast<size_t>(sourceChannelLength));
    EnsureDmxCapacity(dmxData, static_cast<size_t>(sourceChannelStart) + static_cast<size_t>(sourceChannelLength));
    for (int i = 0; i < sourceChannelLength; ++i) {
        dmxData[static_cast<size_t>(targetChannel + i)] = dmxData[static_cast<size_t>(sourceChannelStart + i)];
    }
}

bool RemapGenerator::DrawUi() {
    bool changed = false;
    changed |= ImGui::InputInt("Source Channel Start", &sourceChannelStart);
    changed |= ImGui::InputInt("Source Channel Length", &sourceChannelLength);
    changed |= ImGui::InputInt("Target Channel", &targetChannel);
    return changed;
}

void RemapGenerator::ReadYaml(const YAML::Node& node) {
    try {
        if (node["sourceChannelStart"]) sourceChannelStart = node["sourceChannelStart"].as<int>();
        if (node["sourceChannelLength"]) sourceChannelLength = node["sourceChannelLength"].as<int>();
        if (node["targetChannel"]) targetChannel = node["targetChannel"].as<int>();
    } catch (const YAML::Exception&) {
    }
}

void RemapGenerator::WriteYaml(YAML::Emitter& out) const {
    out << YAML::Key << "sourceChannelStart" << YAML::Value << sourceChannelStart;
    out << YAML::Key << "sourceChannelLength" << YAML::Value << sourceChannelLength;
    out << YAML::Key << "targetChannel" << YAML::Value << targetChannel;
}
