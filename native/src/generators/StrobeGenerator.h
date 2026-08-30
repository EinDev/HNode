#pragma once
// Port of Assets/Plugin/Generators/GeneratorStrobe.cs - toggles a channel between two
// values at a given frequency. Animated: needs continuous re-evaluation.
#include "IGenerator.h"

class StrobeGenerator : public IGenerator {
public:
    const char* Name() const override { return "Strobe"; }

    int channel = 0;
    int valueOn = 255;
    int valueOff = 0;
    float frequencyHz = 1.0f;

    bool IsAnimated() const override { return true; }
    void GenerateDMX(std::vector<uint8_t>& dmxData) override;

    bool DrawUi() override;
    void ReadYaml(const YAML::Node& node) override;
    void WriteYaml(YAML::Emitter& out) const override;
};
