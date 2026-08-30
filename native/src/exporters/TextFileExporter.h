#pragma once
// Port of Assets/Plugin/Exporters/TextFileExporter.cs - on demand (via its own UI
// button, not automatically every frame), writes the most recent frame's full DMX
// channel list to a text file as "channel: value" lines.
#include "IExporter.h"

class TextFileExporter : public IExporter {
public:
    const char* Name() const override { return "TextFileExporter"; }

    bool onlyNonZeroChannels = false;

    void CompleteFrame(const std::vector<uint8_t>& channelValues) override { data_ = channelValues; }

    bool DrawUi() override;

    void ReadYaml(const YAML::Node& node) override;
    void WriteYaml(YAML::Emitter& out) const override;

private:
    std::vector<uint8_t> data_;
};
