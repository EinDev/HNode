#pragma once
// Port of Assets/Plugin/Generators/GeneratorLRC.cs (`class LRC : Text`). Loads a
// lyrics (.lrc) file at Construct() time; three playback modes (see Mode). Animated:
// needs continuous re-evaluation in OnConfigLoad/PlayTrigger modes to detect when the
// active lyric line should advance.
#include "IGenerator.h"
#include "TextGenerator.h"
#include <string>
#include <vector>

class LrcGenerator : public IGenerator {
public:
    const char* Name() const override { return "LRC"; }

    enum class Mode { StrobeTrigger, PlayTrigger, OnConfigLoad };

    std::string filePath;
    Mode mode = Mode::OnConfigLoad;
    int triggerChannel = 0;

    bool IsAnimated() const override { return true; }

    // Parses `filePath` into `events_`. FAITHFUL QUIRK: unlike SrtGenerator, the C#
    // reference's LRC.Construct() does NOT clear `events` first - so calling
    // Construct() again (e.g. editing filePath then reloading) APPENDS the newly
    // parsed events to whatever was already loaded, rather than replacing them. Port
    // this exactly (don't `events_.clear()` here), it's a real quirk of the reference,
    // not something to "fix".
    void Construct() override;

    void GenerateDMX(std::vector<uint8_t>& dmxData) override;

    // The C# reference has NO ConstructUserInterface override at all for LRC (unlike
    // SRT/Time, which override it to disable the text field) - it uses Text's
    // unmodified UI, meaning the raw text field IS shown/editable (even though
    // GenerateDMX overwrites `text` every call, same as the others). Port that
    // omission faithfully: DrawUi() here should behave like TextGenerator's own
    // DrawUi() (text field included), not hide it. filePath/mode/triggerChannel have
    // NO UI at all in the reference either (config-file-only fields) - don't invent
    // controls for them.
    bool DrawUi() override;

    void ReadYaml(const YAML::Node& node) override;
    void WriteYaml(YAML::Emitter& out) const override;

private:
    struct LrcEvent {
        double timestampMs = 0.0;
        std::string text;
    };

    std::vector<LrcEvent> events_;
    size_t currentEvent_ = 0;
    uint8_t lastTriggerValue_ = 0;
    double timeAtLoadMs_ = 0.0;
    TextGenerator inner_;
};
