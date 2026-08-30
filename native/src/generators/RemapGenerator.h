#pragma once
// Port of Assets/Plugin/Generators/GeneratorRemap.cs - copies a channel range to
// another location every frame.
#include "IGenerator.h"

class RemapGenerator : public IGenerator {
public:
    const char* Name() const override { return "Remap"; }

    int sourceChannelStart = 0;
    int sourceChannelLength = 0;
    int targetChannel = 0;

    void GenerateDMX(std::vector<uint8_t>& dmxData) override;

    bool DrawUi() override;
    void ReadYaml(const YAML::Node& node) override;
    void WriteYaml(YAML::Emitter& out) const override;
};
