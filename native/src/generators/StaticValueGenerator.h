#pragma once
// Port of Assets/Plugin/Generators/GeneratorStaticValue.cs - forces a channel range to
// a constant value every frame.
#include "IGenerator.h"

class StaticValueGenerator : public IGenerator {
public:
    const char* Name() const override { return "StaticValue"; }

    int channelStart = 0;
    int channelEnd = 1;
    int value = 0; // byte value (0-255), stored as int for easy ImGui editing like the C#'s EquationNumber field

    void GenerateDMX(std::vector<uint8_t>& dmxData) override;

    bool DrawUi() override;
    void ReadYaml(const YAML::Node& node) override;
    void WriteYaml(YAML::Emitter& out) const override;
};
