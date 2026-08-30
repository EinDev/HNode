#include "RemapOnDemandGenerator.h"
#include "DmxUtil.h"

#include <yaml-cpp/yaml.h>
#include "imgui.h"

void RemapOnDemandGenerator::GenerateDMX(std::vector<uint8_t>& dmxData) {
    if (remapChannelLength <= 0) return;

    EnsureDmxCapacity(dmxData, static_cast<size_t>(remapToChannelStart) + static_cast<size_t>(remapChannelLength));
    EnsureDmxCapacity(dmxData, static_cast<size_t>(remapFromChannelStart) + static_cast<size_t>(remapChannelLength));
    EnsureDmxCapacity(dmxData, static_cast<size_t>(toggleChannel) + 1);

    if (dmxData[static_cast<size_t>(toggleChannel)] > activationThreshold) {
        std::vector<uint8_t> copy(dmxData.begin() + static_cast<size_t>(remapFromChannelStart),
                                   dmxData.begin() + static_cast<size_t>(remapFromChannelStart) + static_cast<size_t>(remapChannelLength));
        WriteDmxAtPosition(dmxData, copy, static_cast<size_t>(remapToChannelStart));
    }
}

bool RemapOnDemandGenerator::DrawUi() {
    bool changed = false;
    changed |= ImGui::InputInt("Toggle Channel", &toggleChannel);
    changed |= ImGui::InputInt("Remap From Channel Start", &remapFromChannelStart);
    changed |= ImGui::InputInt("Remap To Channel Start", &remapToChannelStart);
    changed |= ImGui::InputInt("Remap Channel Length", &remapChannelLength);
    changed |= ImGui::InputInt("Activation Threshold", &activationThreshold);
    return changed;
}

void RemapOnDemandGenerator::ReadYaml(const YAML::Node& node) {
    try {
        if (node["toggleChannel"]) toggleChannel = node["toggleChannel"].as<int>();
        if (node["remapFromChannelStart"]) remapFromChannelStart = node["remapFromChannelStart"].as<int>();
        if (node["remapToChannelStart"]) remapToChannelStart = node["remapToChannelStart"].as<int>();
        if (node["remapChannelLength"]) remapChannelLength = node["remapChannelLength"].as<int>();
        if (node["activationThreshold"]) activationThreshold = node["activationThreshold"].as<int>();
    } catch (const YAML::Exception&) {
    }
}

void RemapOnDemandGenerator::WriteYaml(YAML::Emitter& out) const {
    out << YAML::Key << "toggleChannel" << YAML::Value << toggleChannel;
    out << YAML::Key << "remapFromChannelStart" << YAML::Value << remapFromChannelStart;
    out << YAML::Key << "remapToChannelStart" << YAML::Value << remapToChannelStart;
    out << YAML::Key << "remapChannelLength" << YAML::Value << remapChannelLength;
    out << YAML::Key << "activationThreshold" << YAML::Value << activationThreshold;
}
