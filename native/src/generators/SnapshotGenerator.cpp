#include "SnapshotGenerator.h"
#include "DmxUtil.h"

#include <yaml-cpp/yaml.h>
#include "imgui.h"

void SnapshotGenerator::GenerateDMX(std::vector<uint8_t>& dmxData) {
    if (takeSnapshotNextFrame_) {
        takeSnapshotNextFrame_ = false;
        int count = channelEnd - channelStart + 1;
        if (count < 0) count = 0;
        snapshotData_.assign(static_cast<size_t>(count), 0);
        for (int i = 0; i < count; ++i) {
            size_t channel = static_cast<size_t>(channelStart + i);
            snapshotData_[static_cast<size_t>(i)] = (channel < dmxData.size()) ? dmxData[channel] : 0;
        }
    }

    if (snapshotData_.empty()) return;

    for (size_t i = 0; i < snapshotData_.size(); ++i) {
        size_t channel = static_cast<size_t>(channelStart) + i;
        if (channel < dmxData.size()) dmxData[channel] = snapshotData_[i];
    }
}

bool SnapshotGenerator::DrawUi() {
    bool changed = false;
    changed |= ImGui::InputInt("Channel Start", &channelStart);
    changed |= ImGui::InputInt("Channel End", &channelEnd);
    if (ImGui::Button("Take Snapshot")) {
        takeSnapshotNextFrame_ = true;
        changed = true;
    }
    if (ImGui::Button("Clear Snapshot")) {
        snapshotData_.clear();
        changed = true;
    }
    return changed;
}

void SnapshotGenerator::ReadYaml(const YAML::Node& node) {
    try {
        if (node["channelStart"]) channelStart = node["channelStart"].as<int>();
        if (node["channelEnd"]) channelEnd = node["channelEnd"].as<int>();
    } catch (const YAML::Exception&) {
    }
}

void SnapshotGenerator::WriteYaml(YAML::Emitter& out) const {
    out << YAML::Key << "channelStart" << YAML::Value << channelStart;
    out << YAML::Key << "channelEnd" << YAML::Value << channelEnd;
}
