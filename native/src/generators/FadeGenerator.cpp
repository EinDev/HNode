#include "FadeGenerator.h"
#include "DmxUtil.h"

#include <yaml-cpp/yaml.h>
#include "imgui.h"

#include <algorithm>
#include <chrono>
#include <ctime>

namespace {

// Current local time-of-day, in milliseconds since midnight - equivalent to C#'s
// `DateTime.Now.TimeOfDay.TotalMilliseconds`. Shared by GenerateDMX and DrawUi's
// Fade In/Out buttons so they agree on "now".
double NowTimeOfDayMs() {
    using namespace std::chrono;
    const system_clock::time_point now = system_clock::now();
    const time_t nowSeconds = system_clock::to_time_t(now);

    std::tm localTm{};
#if defined(_WIN32)
    localtime_s(&localTm, &nowSeconds);
#else
    localtime_r(&nowSeconds, &localTm);
#endif

    // Sub-second fraction of `now`, in milliseconds.
    const auto sinceEpochMs = duration_cast<milliseconds>(now.time_since_epoch()).count();
    const auto wholeSeconds = duration_cast<seconds>(now.time_since_epoch()).count();
    const long long subSecondMs = sinceEpochMs - wholeSeconds * 1000;

    const double hoursMs = static_cast<double>(localTm.tm_hour) * 3600000.0;
    const double minutesMs = static_cast<double>(localTm.tm_min) * 60000.0;
    const double secondsMs = static_cast<double>(localTm.tm_sec) * 1000.0;
    return hoursMs + minutesMs + secondsMs + static_cast<double>(subSecondMs);
}

// Mathf.InverseLerp(a, b, value), clamped to [0,1] - including the degenerate a == b
// case (which C#'s float divide would turn into +/-Infinity or NaN; we just pick 0
// defensively rather than propagate a NaN into the Lerp below).
float InverseLerpClamped(double a, double b, double value) {
    if (a == b) return 0.0f;
    double t = (value - a) / (b - a);
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    return static_cast<float>(t);
}

} // namespace

void FadeGenerator::GenerateDMX(std::vector<uint8_t>& dmxData) {
    const double nowMs = NowTimeOfDayMs();
    const float t = InverseLerpClamped(fadeStartMs_, fadeEndMs_, nowMs);

    for (int channel : channels) {
        if (channel < 0) continue;
        EnsureDmxCapacity(dmxData, static_cast<size_t>(channel) + 1);

        const float current = static_cast<float>(dmxData[static_cast<size_t>(channel)]);
        const float target = static_cast<float>(valueToFadeTo);
        float lerped = current + (target - current) * t;
        lerped = std::clamp(lerped, 0.0f, 255.0f);
        dmxData[static_cast<size_t>(channel)] = static_cast<uint8_t>(lerped);
    }
}

bool FadeGenerator::DrawUi() {
    bool changed = false;

    if (ImGui::Button("Fade In")) {
        const double now = NowTimeOfDayMs();
        fadeStartMs_ = now + fadeDurationSeconds * 1000.0;
        fadeEndMs_ = now;
    }
    ImGui::SameLine();
    if (ImGui::Button("Fade Out")) {
        const double now = NowTimeOfDayMs();
        fadeStartMs_ = now;
        fadeEndMs_ = now + fadeDurationSeconds * 1000.0;
    }

    changed |= ImGui::InputDouble("Fade Duration (s)", &fadeDurationSeconds);

    return changed;
}

void FadeGenerator::ReadYaml(const YAML::Node& node) {
    try {
        if (node["channels"] && node["channels"].IsSequence()) {
            channels.clear();
            for (const auto& item : node["channels"]) {
                channels.push_back(item.as<int>());
            }
        }
        if (node["valueToFadeTo"]) valueToFadeTo = node["valueToFadeTo"].as<int>();
        // Deliberate format simplification: the C# reference's `fadeDuration` is a full
        // TimeSpan (e.g. "00:00:05" when serialized by YamlDotNet). We store/parse it as
        // a plain seconds-as-double instead, since no other TimeSpan-format parsing
        // exists elsewhere in this port.
        if (node["fadeDuration"]) fadeDurationSeconds = node["fadeDuration"].as<double>();
    } catch (const YAML::Exception&) {
    }
}

void FadeGenerator::WriteYaml(YAML::Emitter& out) const {
    out << YAML::Key << "channels" << YAML::Value << YAML::BeginSeq;
    for (int channel : channels) {
        out << channel;
    }
    out << YAML::EndSeq;
    out << YAML::Key << "valueToFadeTo" << YAML::Value << valueToFadeTo;
    out << YAML::Key << "fadeDuration" << YAML::Value << fadeDurationSeconds;
}
