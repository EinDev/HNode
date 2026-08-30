#pragma once
// Port of Assets/Plugin/Exporters/TimeCodeExporter.cs - reads MIDI Time Code (MTC)
// from a MIDI input device and rebroadcasts it as a small UDP packet on a fixed set
// of loopback ports, for other local tools (e.g. a VRChat world driver) to consume
// with lower latency than reading MIDI themselves.
//
// The C# original delegated quarter-frame assembly to the DryWetMidi library and only
// hand-rolled the full-frame SysEx path; there's no equivalent library dependency
// here, so both the standard MTC quarter-frame (0xF1 status) and full-frame SysEx
// (F0 7F 7F 01 01 hh mm ss ff F7) paths are parsed directly from raw winmm MIDI input
// callbacks. See TimeCodeExporter.cpp for the exact wire format this reproduces -
// framerate (24/25/29.97/30) is parsed by both paths but deliberately never
// transmitted, matching the original (framerate is dead code there too - only raw
// H:M:S + frame count go out over the wire, and downstream consumers were built
// against that contract).
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
#include "IExporter.h"

class TimeCodeExporter : public IExporter {
public:
    TimeCodeExporter();
    ~TimeCodeExporter() override;

    TimeCodeExporter(const TimeCodeExporter&) = delete;
    TimeCodeExporter& operator=(const TimeCodeExporter&) = delete;

    const char* Name() const override { return "TimeCodeExporter"; }

    // User-configurable field (mirrors TimeCodeExporter.cs's public `midiDevice`).
    std::string midiDeviceName = "loopMIDI Port";

    void Construct() override;
    void Deconstruct() override;
    void Shutdown();

    // Opens `midiDeviceName` for MIDI input and (re)connects the UDP sockets. Both
    // Construct() and DrawUi()'s "Reconnect" button call this.
    void Reconnect();

    // Sends the current timecode packet - mirrors the C#'s InitFrame() (not
    // CompleteFrame()), deliberately called as early as possible each tick to
    // minimize latency between a fresh MIDI event and the outgoing UDP packet. Like
    // all exporters this runs every main-loop iteration regardless of the render-on-
    // change `dirty` flag - see IExporter.h's cadence note.
    void InitFrame(const std::vector<uint8_t>& channelValues) override;

    bool DrawUi() override;
    void ReadYaml(const YAML::Node& node) override;
    void WriteYaml(YAML::Emitter& out) const override;

    bool IsConnected() const { return midiInHandle_ != 0; }

    static std::vector<std::string> ListDevices();

private:
    void OpenMidiDevice(const std::string& name);
    void CloseMidiDevice();
    void OpenSockets();
    void CloseSockets();

    // Called from the winmm callback thread (NOT the main thread) - guards the shared
    // timecode/frame fields with mutex_.
    void OnQuarterFrame(uint8_t dataByte);
    void OnSysEx(const uint8_t* data, size_t length);

    // Calling convention is irrelevant on x64 (this project only targets x64-windows -
    // see native/build.bat), so this is declared as a plain function rather than
    // pulling in <windows.h> here just for the CALLBACK/__stdcall macro.
    static void MidiInProc(void* hMidiIn, unsigned int wMsg, std::uintptr_t dwInstance,
                            std::uintptr_t dwParam1, std::uintptr_t dwParam2);

    std::uintptr_t midiInHandle_ = 0; // HMIDIIN, type-erased to keep <mmsystem.h> out of this header
    void* sysexHeader_ = nullptr;     // LPMIDIHDR, owns sysexBuffer_ below while open
    std::vector<uint8_t> sysexBuffer_;
    // Set before midiInReset()/midiInClose() in CloseMidiDevice() so the MIM_LONGDATA
    // handler (which may still fire synchronously during midiInReset(), on the winmm
    // callback thread) knows not to re-queue the sysex buffer we're about to unprepare
    // and free - otherwise it would be a use-after-free.
    std::atomic<bool> stopping_{false};

    std::mutex mutex_;
    uint8_t quarterFramePieces_[8] = {};
    int hours_ = 0;
    int minutes_ = 0;
    int seconds_ = 0;
    uint8_t frames_ = 0;

    // 5 loopback UDP sockets, one per fixed destination port - matches the C#'s
    // `ports = {7001,7002,7003,7004,7005}` / one UdpClient per port. Not user-
    // configurable in the original, so not exposed in the UI here either.
    static constexpr int kPorts[5] = {7001, 7002, 7003, 7004, 7005};
    std::uintptr_t sockets_[5] = {};
};
