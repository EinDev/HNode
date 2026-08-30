#pragma once
// Port of Assets/Plugin/Generators/GeneratorText.cs - writes a text string's raw bytes
// (UTF-8 or UTF-16) directly into a DMX channel range. Not animated by itself (the text
// only changes when edited) - TimeGenerator (below) is the animated subclass-equivalent
// that overwrites `text` from the system clock every frame.
#include "IGenerator.h"
#include <string>

class TextGenerator : public IGenerator {
public:
    const char* Name() const override { return "Text"; }

    std::string text;
    int channelStart = 0;
    bool unicode = false;
    bool limitLength = false;
    int maxCharacters = 32;

    void GenerateDMX(std::vector<uint8_t>& dmxData) override;

    bool DrawUi() override;
    void ReadYaml(const YAML::Node& node) override;
    void WriteYaml(YAML::Emitter& out) const override;
};

// Port of Assets/Plugin/Generators/GeneratorTime.cs (`class Time : Text`). Composition
// instead of inheritance (a TextGenerator member, not a base class) since C++ doesn't
// need the C# inheritance here - same net effect: format the current time into `text`,
// then run TextGenerator's own GenerateDMX.
class TimeGenerator : public IGenerator {
public:
    const char* Name() const override { return "Time"; }

    // strftime-style format string (C#'s DateTime.ToString custom format strings and
    // strftime specifiers don't match 1:1 - see TimeGenerator.cpp for the mapping used).
    std::string format = "HH:mm:ss";
    int channelStart = 0;
    bool unicode = false;

    bool IsAnimated() const override { return true; }
    void GenerateDMX(std::vector<uint8_t>& dmxData) override;

    bool DrawUi() override;
    void ReadYaml(const YAML::Node& node) override;
    void WriteYaml(YAML::Emitter& out) const override;

private:
    TextGenerator inner_;
};
