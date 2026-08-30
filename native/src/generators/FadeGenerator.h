#pragma once
// Port of Assets/Plugin/Generators/GeneratorFade.cs - fades a set of channels to a
// target value over a duration, triggered by "Fade In"/"Fade Out" buttons. Animated:
// needs to keep re-evaluating every frame while a fade is in flight (and, faithfully
// matching the C# reference, forever after too - see .cpp for why).
#include "IGenerator.h"
#include <chrono>
#include <vector>

class FadeGenerator : public IGenerator {
public:
    const char* Name() const override { return "Fade"; }

    std::vector<int> channels;
    int valueToFadeTo = 0;
    double fadeDurationSeconds = 5.0;

    bool IsAnimated() const override { return true; }
    void GenerateDMX(std::vector<uint8_t>& dmxData) override;

    bool DrawUi() override;
    void ReadYaml(const YAML::Node& node) override;
    void WriteYaml(YAML::Emitter& out) const override;

private:
    // Time-of-day in milliseconds, matching TimeSpan fadeStart/fadeEnd - see .cpp.
    double fadeStartMs_ = 0.0;
    double fadeEndMs_ = 0.0;
};
