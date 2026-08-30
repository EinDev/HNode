#pragma once
// Port of Assets/Plugin/Generators/GeneratorRemapOnDemand.cs - like RemapGenerator, but
// only copies when a "toggle" channel's value exceeds a threshold.
#include "IGenerator.h"

class RemapOnDemandGenerator : public IGenerator {
public:
    const char* Name() const override { return "RemapOnDemand"; }

    int toggleChannel = 0;
    int remapFromChannelStart = 0;
    int remapToChannelStart = 0;
    int remapChannelLength = 0;
    int activationThreshold = 127;

    void GenerateDMX(std::vector<uint8_t>& dmxData) override;

    bool DrawUi() override;
    void ReadYaml(const YAML::Node& node) override;
    void WriteYaml(YAML::Emitter& out) const override;
};
