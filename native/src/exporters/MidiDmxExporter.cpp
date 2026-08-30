#include "MidiDmxExporter.h"

#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <yaml-cpp/yaml.h>
#include "imgui.h"
#include "imgui_stdlib.h"

namespace {

// MIDIDMX.cs's ControlCode enum (lines ~38-53). Always sent on MIDI channel 15 with
// controller number 127 - see SendMidiControl()'s hardcoded "constant magic number".
constexpr int kControlKnockStart = 101;
constexpr int kControlKnockMiddle = 120;
constexpr int kControlKnockFinish = 107;
constexpr int kControlWatchdog = 127;
// kControlClear (100) and kControlChangeToBank0..7 (0..7) exist in the C# enum but are
// only ever used indirectly (bank numbers 0-7 are sent as raw ints by ChangeBank()).

// Literal ASCII search word scanned for byte-by-byte in the VRC log, matching
// isMidiReady()'s `searchWord` (line ~330).
constexpr char kSearchWord[] = {'M', 'I', 'D', 'I', 'R', 'E', 'A', 'D', 'Y'};
constexpr int kSearchWordLen = 9;

} // namespace

// Holds the tailed log file's state across IsMidiReady()/FindLatestLog() calls. Uses
// std::filesystem::file_size() to check the current on-disk size without disturbing the
// ifstream's read position (matching C#'s FileStream.Length, which doesn't move
// FileStream.Position either).
struct MidiDmxExporter::LogState {
    std::ifstream stream;
    std::filesystem::path path;
    bool open = false;
};

MidiDmxExporter::MidiDmxExporter() {
    midiData_.assign(kMaxChannels, 0);
    logState_ = new LogState();
}

MidiDmxExporter::~MidiDmxExporter() {
    Shutdown();
    delete logState_;
}

// --- winmm plumbing -------------------------------------------------------

std::vector<std::string> MidiDmxExporter::ListDevices() {
    std::vector<std::string> names;
    UINT count = midiOutGetNumDevs();
    for (UINT i = 0; i < count; ++i) {
        MIDIOUTCAPSA caps{};
        if (midiOutGetDevCapsA(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
            names.push_back(caps.szPname);
        }
    }
    return names;
}

void MidiDmxExporter::OpenDevice(const std::string& name) {
    UINT count = midiOutGetNumDevs();
    for (UINT i = 0; i < count; ++i) {
        MIDIOUTCAPSA caps{};
        if (midiOutGetDevCapsA(i, &caps, sizeof(caps)) != MMSYSERR_NOERROR) continue;
        if (name != caps.szPname) continue;

        HMIDIOUT handle = nullptr;
        if (midiOutOpen(&handle, i, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR) {
            midiOutHandle_ = reinterpret_cast<std::uintptr_t>(handle);
        }
        return;
    }
    // No matching device name (or midiOutOpen failed) - stay disconnected, matching the
    // C# MidiConnectDevice()'s try/catch that swallows any failure.
}

void MidiDmxExporter::CloseDevice() {
    if (midiOutHandle_ != 0) {
        midiOutClose(reinterpret_cast<HMIDIOUT>(midiOutHandle_));
        midiOutHandle_ = 0;
    }
}

void MidiDmxExporter::SendShortMessage(uint8_t status, uint8_t data1, uint8_t data2) {
    if (midiOutHandle_ == 0) return;
    DWORD msg = static_cast<DWORD>(status)
              | (static_cast<DWORD>(data1) << 8)
              | (static_cast<DWORD>(data2) << 16);
    midiOutShortMsg(reinterpret_cast<HMIDIOUT>(midiOutHandle_), msg);
}

void MidiDmxExporter::SendNoteOn(int midiChannel4bit, int note7bit, int velocity7bit) {
    SendShortMessage(static_cast<uint8_t>(0x90 | (midiChannel4bit & 0xF)),
                      static_cast<uint8_t>(note7bit),
                      static_cast<uint8_t>(velocity7bit));
}

void MidiDmxExporter::SendNoteOff(int midiChannel4bit, int note7bit, int velocity7bit) {
    SendShortMessage(static_cast<uint8_t>(0x80 | (midiChannel4bit & 0xF)),
                      static_cast<uint8_t>(note7bit),
                      static_cast<uint8_t>(velocity7bit));
}

void MidiDmxExporter::SendControlChange(int midiChannel4bit, int controller7bit, int value7bit) {
    SendShortMessage(static_cast<uint8_t>(0xB0 | (midiChannel4bit & 0xF)),
                      static_cast<uint8_t>(controller7bit),
                      static_cast<uint8_t>(value7bit));
}

// --- Protocol -------------------------------------------------------------

// MIDIDMX.cs's Send18BitMessage<T> (lines ~264-274), folded together with SendMidi's
// (lines ~241-262) NoteOn/NoteOff dispatch: channelWithinBank < 1024 means Note On with
// channeloffset 0, otherwise Note Off with channeloffset 1024.
void MidiDmxExporter::SendChannelValue(int channelWithinBank, uint8_t value) {
    bool noteOn = channelWithinBank < 1024;
    int channeloffset = noteOn ? 0 : 1024;

    int t = channelWithinBank - channeloffset; // 0..1023
    int midiChannelNibble = (t >> 6) & 0xF;
    int noteNumber = ((t << 1) & 0x7F) + ((value >> 7) & 0x1);
    int velocity = value & 0x7F;

    if (noteOn) {
        SendNoteOn(midiChannelNibble, noteNumber, velocity);
    } else {
        SendNoteOff(midiChannelNibble, noteNumber, velocity);
    }
}

// MIDIDMX.cs's ChangeBanks (lines ~297-305).
void MidiDmxExporter::ChangeBank(int bank) {
    bankStatus_ = bank;

    // C# calls Mathf.Clamp(bank, ChangeToBank0, ChangeToBank7) here but discards its
    // return value without reassigning `bank` - a no-op in the original, so the sent
    // bank value is never actually clamped. Replicated faithfully by not clamping.

    SendControlChange(15, 127, bank);
    midiUpdatesThisFrame_++;
}

void MidiDmxExporter::SendWatchdog() {
    SendControlChange(15, 127, kControlWatchdog);
}

void MidiDmxExporter::SendKnock() {
    SendControlChange(15, 127, kControlKnockStart);
    SendControlChange(15, 127, kControlKnockMiddle);
    SendControlChange(15, 127, kControlKnockFinish);
}

// MIDIDMX.cs's Reset (lines ~221-234).
void MidiDmxExporter::ResetProtocolState() {
    midiData_.assign(kMaxChannels, 0);
    midiUpdatesThisFrame_ = 0;
    midiScanPosition_ = 0;
    midiCatchup_ = 0;

    FindLatestLog();

    ChangeBank(0);
    SendKnock();
    SendWatchdog();
}

// MIDIDMX.cs's CompleteFrame (lines ~149-194). The per-channel send (MIDIDMX.cs's
// SendMidi, lines ~241-262) is inlined here rather than as a separate member function,
// since the header only exposes SendChannelValue (its Send18BitMessage<T> equivalent) -
// SendMidi's job is just the bank bookkeeping (channel/2048, ChangeBank on bank change,
// reducing channel to 0..2047) around that call.
void MidiDmxExporter::CompleteFrame(const std::vector<uint8_t>& channelValues) {
    if (IsMidiReady()) {
        midiUpdatesThisFrame_ = 0;

        size_t count = channelValues.size();
        if (count > static_cast<size_t>(kMaxChannels)) count = static_cast<size_t>(kMaxChannels); // guard midiData_ bounds
        for (size_t i = static_cast<size_t>(midiCatchup_); i < count; ++i) {
            bool valueChanged = static_cast<int>(channelValues[i]) != midiData_[i];
            bool inIdleScanWindow = static_cast<int>(i) >= midiScanPosition_
                                  && static_cast<int>(i) < midiScanPosition_ + idleScanChannels;
            if (valueChanged || inIdleScanWindow) {
                if (midiUpdatesThisFrame_ >= channelsPerUpdate) {
                    midiCatchup_ = static_cast<int>(i);
                    break;
                }
                midiData_[i] = channelValues[i];

                // MIDIDMX.cs's SendMidi (lines ~241-262).
                if (midiOutHandle_ != 0) {
                    int channelInBank = static_cast<int>(i);
                    int bank = channelInBank / 2048;
                    if (bankStatus_ != bank) {
                        ChangeBank(bank);
                    }
                    channelInBank -= bank * 2048; // now 0..2047
                    SendChannelValue(channelInBank, channelValues[i]);
                    midiUpdatesThisFrame_++;
                }
            }
        }

        if (midiUpdatesThisFrame_ < channelsPerUpdate) {
            midiCatchup_ = 0;
        }

        midiScanPosition_ += idleScanChannels;
        if (midiScanPosition_ > static_cast<int>(channelValues.size())) {
            midiScanPosition_ = 0;
        }

        SendWatchdog();
        midiLastUpdateTicks_ = std::chrono::steady_clock::now().time_since_epoch().count();
    } else {
        long long now = std::chrono::steady_clock::now().time_since_epoch().count();
        double elapsedSeconds = std::chrono::duration<double>(
            std::chrono::steady_clock::duration(now - midiLastUpdateTicks_)).count();
        if (elapsedSeconds > 1.0) {
            midiLastUpdateTicks_ = now;
            ResetProtocolState();
        }
    }
}

// MIDIDMX.cs's isMidiReady (lines ~313-356).
bool MidiDmxExporter::IsMidiReady() {
    if (!logState_->open) {
        FindLatestLog();
        return false;
    }

    if (midiOutHandle_ == 0) {
        return false;
    }

    std::error_code ec;
    uintmax_t fileSize = std::filesystem::file_size(logState_->path, ec);
    if (ec) {
        return false;
    }

    std::streamoff pos = logState_->stream.tellg();
    if (pos < 0) {
        return false;
    }

    long long length = static_cast<long long>(fileSize) - static_cast<long long>(pos);

    if (length > 1) {
        // Single persistent match index - intentionally NOT reset on a mismatched byte,
        // matching isMidiReady()'s missing `else { i = 0; }` (a bug in the C# original
        // that a co-designed VRChat world script may rely on). The read position
        // advances byte-by-byte even if no full match is ever found this call.
        int i = 0;
        char c;
        while (logState_->stream.get(c)) {
            if (static_cast<unsigned char>(c) == static_cast<unsigned char>(kSearchWord[i])) {
                i++;
                if (i >= kSearchWordLen) {
                    std::error_code ec2;
                    uintmax_t currentSize = std::filesystem::file_size(logState_->path, ec2);
                    if (!ec2 && currentSize > 0) {
                        logState_->stream.clear();
                        logState_->stream.seekg(static_cast<std::streamoff>(currentSize - 1));
                    }
                    return true;
                }
            }
        }
        // Exhausted available bytes without a full match. get() hitting EOF sets both
        // eofbit and failbit, and good() (required by tellg()/get() to do anything)
        // stays false until every bit is cleared - so clear all state, not just
        // failbit, leaving the position exactly where the scan stopped so future calls
        // can keep reading as the file grows (matching FileStream.ReadByte(), which has
        // no sticky EOF state).
        logState_->stream.clear();
    } else {
        return false;
    }

    return false;
}

// MIDIDMX.cs's findVRCLog (lines ~381-408).
void MidiDmxExporter::FindLatestLog() {
    if (logState_->stream.is_open()) {
        logState_->stream.close();
    }
    logState_->stream.clear();
    logState_->open = false;
    logState_->path.clear();

    const char* appData = std::getenv("APPDATA");
    if (!appData) return;
    std::filesystem::path roaming(appData);

    std::filesystem::path chosen;

    if (useEditorLog) {
        // No Unity Editor in the native port - this path simply won't exist, so the
        // stream fails to open and IsMidiReady() keeps returning false, as intended.
        chosen = roaming / ".." / "Local" / "Unity" / "Editor" / "Editor.log";
    } else {
        std::filesystem::path logDir = roaming / ".." / "LocalLow" / "VRChat" / "VRChat";

        std::vector<std::string> candidates;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(logDir, ec)) {
            if (ec) break;
            std::error_code fileEc;
            if (!entry.is_regular_file(fileEc) || fileEc) continue;
            std::string filename = entry.path().filename().string();
            const std::string prefix = "output_log_";
            const std::string suffix = ".txt";
            if (filename.size() < prefix.size() + suffix.size()) continue;
            if (filename.compare(0, prefix.size(), prefix) != 0) continue;
            if (filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) != 0) continue;
            candidates.push_back(filename);
        }

        if (candidates.empty()) return; // matches C#'s `if (logs.Length == 0) return;`

        std::sort(candidates.begin(), candidates.end());
        chosen = logDir / candidates.back();
    }

    std::ifstream stream(chosen, std::ios::binary);
    if (!stream.is_open()) return;

    std::error_code ec;
    uintmax_t size = std::filesystem::file_size(chosen, ec);
    if (!ec && size != 0) {
        stream.seekg(static_cast<std::streamoff>(size - 1));
    }

    logState_->stream = std::move(stream);
    logState_->path = chosen;
    logState_->open = true;
}

// --- Lifecycle --------------------------------------------------------------

void MidiDmxExporter::Reconnect() {
    CloseDevice();
    OpenDevice(midiDeviceName);
    midiLastUpdateTicks_ = std::chrono::steady_clock::now().time_since_epoch().count();
    ResetProtocolState();
}

void MidiDmxExporter::Shutdown() {
    CloseDevice();
}

// --- UI / persistence --------------------------------------------------------

MidiDmxExporter::MidiStatus MidiDmxExporter::Status() const {
    if (midiOutHandle_ == 0) return MidiStatus::Disconnected;

    long long now = std::chrono::steady_clock::now().time_since_epoch().count();
    double elapsedSeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::duration(now - midiLastUpdateTicks_)).count();
    return elapsedSeconds > 1.0 ? MidiStatus::ConnectedWait : MidiStatus::ConnectedSendingData;
}

bool MidiDmxExporter::DrawUi() {
    bool changed = false;
    switch (Status()) {
        case MidiStatus::Disconnected:
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "MIDI: disconnected");
            break;
        case MidiStatus::ConnectedWait:
            // Device port is open, but no VRChat world has ack'd the watchdog yet -
            // this is the state the UI used to mislabel as "connected".
            ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.1f, 1.0f), "MIDI: waiting for VRChat (no MIDIREADY yet)");
            break;
        case MidiStatus::ConnectedSendingData:
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "MIDI: connected, sending data");
            break;
    }
    changed |= ImGui::InputText("MIDI Device", &midiDeviceName);
    changed |= ImGui::InputInt("Channels Per Update", &channelsPerUpdate);
    changed |= ImGui::InputInt("Idle Scan Channels", &idleScanChannels);
    if (ImGui::Button("Reconnect MIDI Device")) {
        Reconnect();
    }
    return changed;
}

void MidiDmxExporter::ReadYaml(const YAML::Node& node) {
    try {
        if (node["midiDevice"]) midiDeviceName = node["midiDevice"].as<std::string>();
        if (node["channelsPerUpdate"]) channelsPerUpdate = node["channelsPerUpdate"].as<int>();
        if (node["idleScanChannels"]) idleScanChannels = node["idleScanChannels"].as<int>();
        if (node["useEditorLog"]) useEditorLog = node["useEditorLog"].as<bool>();
    } catch (const YAML::Exception&) {
    }
}

void MidiDmxExporter::WriteYaml(YAML::Emitter& out) const {
    out << YAML::Key << "midiDevice" << YAML::Value << midiDeviceName;
    out << YAML::Key << "channelsPerUpdate" << YAML::Value << channelsPerUpdate;
    out << YAML::Key << "idleScanChannels" << YAML::Value << idleScanChannels;
    out << YAML::Key << "useEditorLog" << YAML::Value << useEditorLog;
}
