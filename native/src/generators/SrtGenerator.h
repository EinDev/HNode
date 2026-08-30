#pragma once
// Port of Assets/Plugin/Generators/GeneratorSRT.cs (`class SRT : Text`). Loads a
// subtitle (.srt) file at Construct() time and plays it back against wall-clock time
// relative to when it was loaded, writing the active subtitle's text through a
// TextGenerator the same way TimeGenerator does. Animated: needs continuous
// re-evaluation to detect when the active subtitle event should change.
#include "IGenerator.h"
#include "TextGenerator.h"
#include <string>
#include <vector>

class SrtGenerator : public IGenerator {
public:
    const char* Name() const override { return "SRT"; }

    std::string filePath;
    bool generateSubtitlePercentage = false;
    int subtitlePercentChannel = 0;

    // Mirror the inner TextGenerator's non-text fields, which the C# reference still
    // exposes via its base.ConstructUserInterface() call (only the `text` field itself
    // is disabled in the UI, since GenerateDMX overwrites it every call).
    int channelStart = 0;
    bool unicode = false;
    bool limitLength = false;
    int maxCharacters = 32;

    bool IsAnimated() const override { return true; }

    // Parses `filePath` into `events_` (regex-equivalent block parse - see .cpp).
    // IMPORTANT (faithful quirk): the C# reference does NOT reset `currentEvent` here,
    // only `events`/`timeAtLoad` - so reloading mid-playback can leave `currentEvent_`
    // pointing past the end of a shorter new file (GenerateDMX's bounds check on
    // `currentEvent_ < events_.size()` makes that a silent no-op, not a crash).
    void Construct() override;

    void GenerateDMX(std::vector<uint8_t>& dmxData) override;

    bool DrawUi() override;
    void ReadYaml(const YAML::Node& node) override;
    void WriteYaml(YAML::Emitter& out) const override;

private:
    struct SrtEvent {
        double startMs = 0.0;
        double endMs = 0.0;
        std::string text;
    };

    std::vector<SrtEvent> events_;
    size_t currentEvent_ = 0;
    double timeAtLoadMs_ = 0.0; // time-of-day (ms since midnight) captured at Construct()
    TextGenerator inner_;
};
