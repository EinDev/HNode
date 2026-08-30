#pragma once
// Port of Assets/Plugin/Generators/GeneratorSnapshot.cs - captures a channel range on
// button press, then continuously re-writes that captured range every frame until
// cleared.
#include "IGenerator.h"
#include <vector>

class SnapshotGenerator : public IGenerator {
public:
    const char* Name() const override { return "Snapshot"; }

    int channelStart = 0;
    int channelEnd = 1;

    void GenerateDMX(std::vector<uint8_t>& dmxData) override;

    bool DrawUi() override;
    void ReadYaml(const YAML::Node& node) override;
    void WriteYaml(YAML::Emitter& out) const override;

private:
    std::vector<uint8_t> snapshotData_; // empty == "no snapshot taken" (matches C#'s null)
    bool takeSnapshotNextFrame_ = false;
};
