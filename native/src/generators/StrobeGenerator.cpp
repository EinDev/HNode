#include "StrobeGenerator.h"
#include "DmxUtil.h"

#include <yaml-cpp/yaml.h>
#include "imgui.h"

#include <algorithm>
#include <chrono>
#include <cmath>

void StrobeGenerator::GenerateDMX(std::vector<uint8_t>& dmxData) {
    if (channel < 0) return;
    if (frequencyHz <= 0.0f) return; // guard against divide-by-zero; C# UI validates this on input

    EnsureDmxCapacity(dmxData, static_cast<size_t>(channel) + 1);

    // Sub-second millisecond component (0-999) of the current time - matches C#'s
    // `DateTime.Now.Millisecond`. This wraps every second, which is a real quirk/
    // limitation of the C# reference (not a monotonic/elapsed time base) - ported
    // faithfully rather than "fixed".
    using namespace std::chrono;
    const auto nowMs = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    const long long time = nowMs % 1000;

    const float period = 1000.0f / frequencyHz; // ms
    const float phase = std::fmod(static_cast<float>(time), period);

    const uint8_t on = static_cast<uint8_t>(std::clamp(valueOn, 0, 255));
    const uint8_t off = static_cast<uint8_t>(std::clamp(valueOff, 0, 255));

    dmxData[static_cast<size_t>(channel)] = (phase < period / 2.0f) ? on : off;
}

bool StrobeGenerator::DrawUi() {
    bool changed = false;
    changed |= ImGui::InputInt("Channel", &channel);
    changed |= ImGui::InputInt("Value On", &valueOn);
    changed |= ImGui::InputInt("Value Off", &valueOff);
    changed |= ImGui::InputFloat("Frequency (Hz)", &frequencyHz);
    return changed;
}

void StrobeGenerator::ReadYaml(const YAML::Node& node) {
    try {
        if (node["channel"]) channel = node["channel"].as<int>();
        if (node["valueOn"]) valueOn = node["valueOn"].as<int>();
        if (node["valueOff"]) valueOff = node["valueOff"].as<int>();
        // YAML key stays "frequency" (not "frequencyHz") to match the C# field name
        // exactly, for compatibility with Unity-saved configs.
        if (node["frequency"]) frequencyHz = node["frequency"].as<float>();
    } catch (const YAML::Exception&) {
    }
}

void StrobeGenerator::WriteYaml(YAML::Emitter& out) const {
    out << YAML::Key << "channel" << YAML::Value << channel;
    out << YAML::Key << "valueOn" << YAML::Value << valueOn;
    out << YAML::Key << "valueOff" << YAML::Value << valueOff;
    out << YAML::Key << "frequency" << YAML::Value << frequencyHz;
}
