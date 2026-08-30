#pragma once
// Port of Assets/Plugin/Generators/GeneratorASS.cs (`class ASS : Text`). Loads an
// Advanced SubStation Alpha (.ass) subtitle file at Construct() time - parsing its
// [V4+ Styles] and [Events] sections - and every frame emits a custom encoded text
// blob (active styles, then active dialogue events) through a TextGenerator, the same
// composition pattern as SrtGenerator/LrcGenerator/TimeGenerator. Animated: needs
// continuous re-evaluation to track which events/styles are active at the current time.
//
// IMPORTANT byte-vs-codepoint note (see AssGenerator.cpp): the C# reference builds its
// output by appending raw `(char)value` casts (a C# char is a UTF-16 code unit) for
// style indices and color channel bytes, then runs the WHOLE string through
// `Encoding.UTF8.GetBytes` (via the inherited Text.GenerateDMX). For any value >= 128
// this does NOT produce a single raw byte - it produces the UTF-8 encoding of that
// *code point*, i.e. 2 bytes. This native port must reproduce that exact behavior
// (not "fix" it into a clean 1:1 byte packer) since the whole point is bit-for-bit
// output compatibility. See EncodeByteAsCodepoint() in the .cpp.
#include "IGenerator.h"
#include "TextGenerator.h"
#include <string>
#include <vector>

class AssGenerator : public IGenerator {
public:
    const char* Name() const override { return "ASS"; }

    std::string filePath;

    // Mirror the inner TextGenerator's non-text fields (see SrtGenerator.h's comment
    // for why - the C# base.ConstructUserInterface() call still exposes these).
    int channelStart = 0;
    bool unicode = false;
    bool limitLength = false;
    int maxCharacters = 32;

    bool IsAnimated() const override { return true; }

    void Construct() override;
    void GenerateDMX(std::vector<uint8_t>& dmxData) override;

    bool DrawUi() override;
    void ReadYaml(const YAML::Node& node) override;
    void WriteYaml(YAML::Emitter& out) const override;

private:
    struct Color {
        uint8_t r = 0, g = 0, b = 0, a = 255;
    };

    struct Style {
        int styleIndex = 0;
        std::string name;
        std::string fontname;
        float fontSize = 0.0f;
        Color primaryColour, secondaryColour, outlineColour, backColour;
        bool bold = false, italic = false, underline = false, strikeout = false;
        float scaleX = 0, scaleY = 0, spacing = 0, angle = 0;
        int borderStyle = 0;
        float outline = 0, shadow = 0;
        int alignment = 0;
        float marginL = 0, marginR = 0, marginV = 0;
        int encoding = 0;
    };

    struct AssEvent {
        int layer = 0;
        double startMs = 0.0;
        double endMs = 0.0;
        int styleIndex = -1;
        std::string name;
        float marginL = 0, marginR = 0, marginV = 0;
        std::string effect;
        std::string text; // already color/alpha-code-cleaned, per ASSEvent's C# constructor
    };

    std::string title_, scriptType_, subtitler_;
    std::vector<Style> styles_;
    std::vector<AssEvent> events_;
    double timeAtLoadMs_ = 0.0;
    TextGenerator inner_;
};
