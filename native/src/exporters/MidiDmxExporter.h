#pragma once
// Port of Assets/Plugin/Exporters/MIDIDMX.cs - sends DMX channel data out as MIDI
// events for the VRC-MIDIDMX world-side receiver (https://github.com/micksam7/VRC-MIDIDMX).
// Talks to a MIDI output device (typically a virtual port like loopMIDI) via the
// Windows winmm MIDI API instead of DryWetMidi (no extra dependency needed - winmm is
// part of the OS). Protocol/encoding must match the C# reference exactly, including
// its known quirks, since the VRChat-side world script is a fixed, external contract.
#include <cstdint>
#include <string>
#include <vector>
#include "IExporter.h"

class MidiDmxExporter : public IExporter {
public:
    MidiDmxExporter();
    ~MidiDmxExporter() override;

    MidiDmxExporter(const MidiDmxExporter&) = delete;
    MidiDmxExporter& operator=(const MidiDmxExporter&) = delete;

    const char* Name() const override { return "MIDIDMX"; }

    // User-configurable fields (mirror MIDIDMX.cs's public fields of the same role).
    std::string midiDeviceName = "loopMIDI Port";
    int channelsPerUpdate = 100;   // keep low: VRChat's receive buffer is small
    int idleScanChannels = 10;
    bool useEditorLog = false;     // native port has no Unity Editor.log; kept for config-field parity, always uses the VRChat player log

    // (Re)opens `midiDeviceName` and resets protocol state (bank, watchdog, log
    // tailing). Construct()/DrawUi()'s "Reconnect MIDI Device" button both call this.
    void Reconnect();

    void Construct() override { Reconnect(); }
    void Deconstruct() override { Shutdown(); }
    void Shutdown();

    // Equivalent to MIDIDMX.cs's CompleteFrame(ref channelValues). Note: per
    // IExporter.h, this (like all exporters) ticks every main-loop iteration, not
    // just on rendered/dirty frames - MIDIDMX's watchdog needs a steady heartbeat.
    void CompleteFrame(const std::vector<uint8_t>& channelValues) override;

    bool DrawUi() override;
    void ReadYaml(const YAML::Node& node) override;
    void WriteYaml(YAML::Emitter& out) const override;

    bool IsConnected() const { return midiOutHandle_ != 0; }

    // Mirrors MIDIDMX.cs's MidiStatus(): Disconnected (no MIDI device open at all) vs.
    // ConnectedWait (device open, but no VRChat-side world has ack'd the watchdog via
    // the "MIDIREADY" log line within the last second - i.e. nothing is actually
    // listening yet) vs. ConnectedSendingData (device open AND the world acked
    // recently, so data is genuinely flowing). IsConnected() above only reflects the
    // first of these - it was misleadingly labeled "connected" in the UI even with no
    // VRChat world present; DrawUi() now shows this 3-state version instead.
    enum class MidiStatus { Disconnected, ConnectedWait, ConnectedSendingData };
    MidiStatus Status() const;

    // Enumerates available MIDI output device names (winmm midiOutGetDevCaps).
    static std::vector<std::string> ListDevices();

private:
    void OpenDevice(const std::string& name);
    void CloseDevice();

    void SendShortMessage(uint8_t status, uint8_t data1, uint8_t data2);
    void SendNoteOn(int midiChannel4bit, int note7bit, int velocity7bit);
    void SendNoteOff(int midiChannel4bit, int note7bit, int velocity7bit);
    void SendControlChange(int midiChannel4bit, int controller7bit, int value7bit);

    // MIDIDMX.cs's Send18BitMessage<T>: packs a 10-bit channel index and 8-bit DMX
    // value across the MIDI channel nibble + note number + velocity fields of a
    // single Note On/Off event.
    void SendChannelValue(int channelWithinBank /*0..2047*/, uint8_t value);

    void ChangeBank(int bank);
    void SendWatchdog();
    void SendKnock();
    void ResetProtocolState();

    // Tails the current VRChat (or Editor, if useEditorLog) log for a "MIDIREADY"
    // watchdog-ack string appended since the last check. Faithfully replicates
    // MIDIDMX.cs's isMidiReady(), INCLUDING its behavior of not resetting the partial
    // match index on a mismatched byte - see MidiDmxExporter.cpp for detail; VRChat
    // world scripts co-designed against this exact watchdog behavior may depend on it.
    bool IsMidiReady();
    void FindLatestLog();

    static constexpr int kMaxChannels = 16384;

    std::uintptr_t midiOutHandle_ = 0; // HMIDIOUT, type-erased to keep <mmsystem.h> out of this header

    std::vector<int> midiData_;    // last value sent per channel (0-initialized, matching `new int[maxChannels]`)
    int bankStatus_ = 0;
    int midiUpdatesThisFrame_ = 0;
    int midiCatchup_ = 0;
    int midiScanPosition_ = 0;
    long long midiLastUpdateTicks_ = 0; // matches Stopwatch.GetTimestamp()/Frequency-based 1s timeout logic

    struct LogState;
    LogState* logState_ = nullptr;
};
