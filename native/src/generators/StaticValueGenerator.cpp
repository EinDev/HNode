#include "StaticValueGenerator.h"
#include "DmxUtil.h"

#include <algorithm>
#include <yaml-cpp/yaml.h>
#include "imgui.h"

void StaticValueGenerator::GenerateDMX(std::vector<uint8_t>& dmxData) {
    if (channelEnd < channelStart) return;
    EnsureDmxCapacity(dmxData, static_cast<size_t>(channelEnd) + 1);
    uint8_t v = static_cast<uint8_t>(std::clamp(value, 0, 255));
    for (int i = channelStart; i <= channelEnd; ++i) {
        dmxData[static_cast<size_t>(i)] = v;
    }
}

bool StaticValueGenerator::DrawUi() {
    bool changed = false;
    changed |= ImGui::InputInt("Channel Start", &channelStart);
    changed |= ImGui::InputInt("Channel End", &channelEnd);
    changed |= ImGui::InputInt("Value", &value);
    return changed;
}

void StaticValueGenerator::ReadYaml(const YAML::Node& node) {
    try {
        if (node["channelStart"]) channelStart = node["channelStart"].as<int>();
        if (node["channelEnd"]) channelEnd = node["channelEnd"].as<int>();
        if (node["value"]) value = node["value"].as<int>();
    } catch (const YAML::Exception&) {
    }
}

void StaticValueGenerator::WriteYaml(YAML::Emitter& out) const {
    out << YAML::Key << "channelStart" << YAML::Value << channelStart;
    out << YAML::Key << "channelEnd" << YAML::Value << channelEnd;
    out << YAML::Key << "value" << YAML::Value << value;
}
