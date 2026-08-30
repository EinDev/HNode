#pragma once
// Port of Assets/Plugin/Generators/OnTime/GeneratorOnTime.cs (`class OnTime : Text`).
// Polls the local OnTime (https://on-time.app) show-timer companion API
// (GET http://localhost:4001/api/poll, hardcoded - not user-configurable in the C#
// reference either) and writes a 14-byte binary payload plus a text field derived
// from the response.
//
// The C# reference does a synchronous, blocking HttpClient GET on every single
// GenerateDMX() call (i.e. once per Unity frame, ~60Hz) - that's fine on Unity's own
// thread, but would stall this app's main/render thread if reproduced literally here.
// Instead, a background thread polls on a fixed interval (see kPollIntervalMs in the
// .cpp) via WinHTTP and stores the latest parsed response; GenerateDMX() just reads
// that snapshot under a mutex, matching the non-blocking-main-thread shape the rest of
// this codebase's networked generators/exporters already use.
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include "IGenerator.h"
#include "TextGenerator.h"

class OnTimeGenerator : public IGenerator {
public:
    OnTimeGenerator();
    ~OnTimeGenerator() override;

    OnTimeGenerator(const OnTimeGenerator&) = delete;
    OnTimeGenerator& operator=(const OnTimeGenerator&) = delete;

    const char* Name() const override { return "OnTime"; }

    // Only field the C# reference itself exposes (its base Text fields below are
    // shown too, but with `text` disabled - see SrtGenerator.h for the same pattern).
    int dataPayloadChannelStart = 0;

    int channelStart = 0;
    bool unicode = false;
    bool limitLength = false;
    int maxCharacters = 32;

    bool IsAnimated() const override { return true; }

    void Construct() override;
    void Deconstruct() override;

    void GenerateDMX(std::vector<uint8_t>& dmxData) override;

    bool DrawUi() override;
    void ReadYaml(const YAML::Node& node) override;
    void WriteYaml(YAML::Emitter& out) const override;

    bool IsConnected() const { return connected_.load(); }

private:
    void PollLoop();

    struct State {
        int32_t clock = 0;
        int32_t timerCurrent = 0;
        int32_t auxTimerCurrent = 0;
        float progress = 0.0f; // 0..1, already clamped and duration==0-guarded
        bool timerVisible = false;
        std::string timerText;
        bool timerBlink = false;
        bool timerBlackout = false;
        std::string secondarySource; // "external" / "aux" / anything else
        std::string externalMessage;
    };

    std::thread pollThread_;
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> connected_{false};

    std::mutex stateMutex_;
    State state_;

    TextGenerator inner_;
};
