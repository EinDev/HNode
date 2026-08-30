#include "TimeCodeExporter.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <mmsystem.h>

#include <chrono>
#include <cstring>

#include <yaml-cpp/yaml.h>
#include "imgui.h"
#include "imgui_stdlib.h"

// Port of Assets/Plugin/Exporters/TimeCodeExporter.cs. See the header for the overall
// protocol description and the framerate-is-dead-code note.

TimeCodeExporter::TimeCodeExporter() = default;

TimeCodeExporter::~TimeCodeExporter() {
    Shutdown();
}

void TimeCodeExporter::Construct() {
    Reconnect();
}

void TimeCodeExporter::Deconstruct() {
    Shutdown();
}

void TimeCodeExporter::Shutdown() {
    CloseMidiDevice();
    CloseSockets();
}

void TimeCodeExporter::Reconnect() {
    CloseMidiDevice();
    CloseSockets();
    OpenMidiDevice(midiDeviceName);
    OpenSockets();
}

// --- winmm plumbing -------------------------------------------------------

std::vector<std::string> TimeCodeExporter::ListDevices() {
    std::vector<std::string> names;
    UINT count = midiInGetNumDevs();
    for (UINT i = 0; i < count; ++i) {
        MIDIINCAPSA caps{};
        if (midiInGetDevCapsA(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
            names.push_back(caps.szPname);
        }
    }
    return names;
}

void TimeCodeExporter::OpenMidiDevice(const std::string& name) {
    UINT count = midiInGetNumDevs();
    for (UINT i = 0; i < count; ++i) {
        MIDIINCAPSA caps{};
        if (midiInGetDevCapsA(i, &caps, sizeof(caps)) != MMSYSERR_NOERROR) continue;
        if (name != caps.szPname) continue;

        HMIDIIN handle = nullptr;
        MMRESULT openResult =
            midiInOpen(&handle, i, reinterpret_cast<DWORD_PTR>(&TimeCodeExporter::MidiInProc),
                       reinterpret_cast<DWORD_PTR>(this), CALLBACK_FUNCTION);
        if (openResult != MMSYSERR_NOERROR) {
            return; // matches the C#'s try/catch swallowing MidiConnectDevice() failures
        }

        // One reusable sysex buffer for the full-frame path - re-queued from within the
        // MIM_LONGDATA handler after each message (see MidiInProc). 32 bytes is well
        // over the 10-byte MTC full-frame message this is meant to catch.
        sysexBuffer_.assign(32, 0);
        auto* hdr = new MIDIHDR();
        ZeroMemory(hdr, sizeof(MIDIHDR));
        hdr->lpData = reinterpret_cast<LPSTR>(sysexBuffer_.data());
        hdr->dwBufferLength = static_cast<DWORD>(sysexBuffer_.size());
        if (midiInPrepareHeader(handle, hdr, sizeof(MIDIHDR)) == MMSYSERR_NOERROR &&
            midiInAddBuffer(handle, hdr, sizeof(MIDIHDR)) == MMSYSERR_NOERROR) {
            sysexHeader_ = hdr;
        } else {
            delete hdr;
        }

        stopping_ = false;
        midiInStart(handle);
        midiInHandle_ = reinterpret_cast<std::uintptr_t>(handle);
        return;
    }
    // No matching device name (or open failed) - stay disconnected.
}

void TimeCodeExporter::CloseMidiDevice() {
    if (midiInHandle_ != 0) {
        // midiInReset() below can synchronously deliver MIM_LONGDATA for the buffer
        // we're about to unprepare/free, on the callback thread - this flag tells that
        // handler not to re-queue it. See the member comment in the header.
        stopping_ = true;

        HMIDIIN handle = reinterpret_cast<HMIDIIN>(midiInHandle_);
        midiInStop(handle);
        midiInReset(handle);
        if (sysexHeader_) {
            auto* hdr = reinterpret_cast<LPMIDIHDR>(sysexHeader_);
            midiInUnprepareHeader(handle, hdr, sizeof(MIDIHDR));
            delete hdr;
            sysexHeader_ = nullptr;
        }
        midiInClose(handle);
        midiInHandle_ = 0;
        stopping_ = false;
    }
    sysexBuffer_.clear();
}

void TimeCodeExporter::MidiInProc(void* hMidiIn, unsigned int wMsg, std::uintptr_t dwInstance,
                                   std::uintptr_t dwParam1, std::uintptr_t dwParam2) {
    (void)dwParam2;
    auto* self = reinterpret_cast<TimeCodeExporter*>(dwInstance);
    if (!self) return;

    if (wMsg == MIM_DATA) {
        // Packed short message: status in the low byte, data1/data2 in the next two.
        DWORD packed = static_cast<DWORD>(dwParam1);
        uint8_t status = static_cast<uint8_t>(packed & 0xFF);
        uint8_t data1 = static_cast<uint8_t>((packed >> 8) & 0xFF);
        if (status == 0xF1) { // MIDI Time Code Quarter Frame
            self->OnQuarterFrame(data1);
        }
    } else if (wMsg == MIM_LONGDATA) {
        auto* hdr = reinterpret_cast<LPMIDIHDR>(dwParam1);
        if (hdr && hdr->dwBytesRecorded > 0) {
            self->OnSysEx(reinterpret_cast<const uint8_t*>(hdr->lpData), hdr->dwBytesRecorded);
        }
        // Re-queue for the next sysex message. midiInAddBuffer() is documented as safe
        // to call from within this callback (unlike midiInStart/Stop/Reset/Close).
        // Skipped during shutdown - see stopping_'s comment.
        if (hdr && !self->stopping_.load()) {
            midiInAddBuffer(reinterpret_cast<HMIDIIN>(hMidiIn), hdr, sizeof(MIDIHDR));
        }
    }
}

// --- timecode parsing -------------------------------------------------------

void TimeCodeExporter::OnQuarterFrame(uint8_t dataByte) {
    // Standard MTC quarter-frame message: high nibble (bits 4-6) selects which piece
    // of the timecode this carries (0-7), low nibble is that piece's 4 data bits.
    int piece = (dataByte >> 4) & 0x07;
    uint8_t nibble = dataByte & 0x0F;

    std::lock_guard<std::mutex> lock(mutex_);
    quarterFramePieces_[piece] = nibble;
    if (piece == 7) {
        // Piece 7 completes a cycle (0..7) - reassemble the full timecode. Framerate
        // (bits 1-2 of piece 7) is parsed but intentionally not stored/transmitted;
        // see the header comment for why.
        int frameCount = quarterFramePieces_[0] | ((quarterFramePieces_[1] & 0x01) << 4);
        int sec = quarterFramePieces_[2] | ((quarterFramePieces_[3] & 0x07) << 4);
        int min = quarterFramePieces_[4] | ((quarterFramePieces_[5] & 0x07) << 4);
        int hr = quarterFramePieces_[6] | ((quarterFramePieces_[7] & 0x01) << 4);

        hours_ = hr;
        minutes_ = min;
        seconds_ = sec;
        frames_ = static_cast<uint8_t>(frameCount);
    }
}

void TimeCodeExporter::OnSysEx(const uint8_t* data, size_t length) {
    // Standard MTC Full Frame: F0 7F <deviceID> 01 01 hh mm ss ff F7 (10 bytes).
    // deviceID must be 0x7F (broadcast) per the MMC/MTC spec.
    if (length != 10) return;
    if (data[0] != 0xF0 || data[9] != 0xF7) return;
    if (data[1] != 0x7F || data[2] != 0x7F) return;
    if (data[3] != 0x01 || data[4] != 0x01) return;

    uint8_t hoursByte = data[5];
    uint8_t minutes = data[6];
    uint8_t seconds = data[7];
    uint8_t frames = data[8];

    std::lock_guard<std::mutex> lock(mutex_);
    // Low 5 bits are the hour value; top 3 bits are rate flags - parsed-but-unused,
    // matching the C# original (see header comment).
    hours_ = hoursByte & 0x1F;
    minutes_ = minutes;
    seconds_ = seconds;
    frames_ = frames;
}

// --- UDP broadcast -------------------------------------------------------

void TimeCodeExporter::OpenSockets() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return;
    }

    for (int i = 0; i < 5; ++i) {
        SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock == INVALID_SOCKET) {
            sockets_[i] = 0;
            continue;
        }

        sockaddr_in dest{};
        dest.sin_family = AF_INET;
        dest.sin_port = htons(static_cast<u_short>(kPorts[i]));
        dest.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        // connect() on a UDP socket just fixes the default destination for send() -
        // matches the C#'s `UdpClient` + `Connect(IPAddress.Loopback, port)` pattern
        // (one client per port, reused every frame).
        if (connect(sock, reinterpret_cast<sockaddr*>(&dest), sizeof(dest)) == SOCKET_ERROR) {
            closesocket(sock);
            sockets_[i] = 0;
            continue;
        }
        sockets_[i] = static_cast<std::uintptr_t>(sock);
    }
}

void TimeCodeExporter::CloseSockets() {
    bool anyOpened = false;
    for (int i = 0; i < 5; ++i) {
        if (sockets_[i] != 0) {
            closesocket(static_cast<SOCKET>(sockets_[i]));
            sockets_[i] = 0;
            anyOpened = true;
        }
    }
    if (anyOpened) {
        WSACleanup(); // one matching WSAStartup() call happened in OpenSockets()
    }
}

void TimeCodeExporter::InitFrame(const std::vector<uint8_t>& /*channelValues*/) {
    int hours, minutes, seconds;
    uint8_t frames;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        hours = hours_;
        minutes = minutes_;
        seconds = seconds_;
        frames = frames_;
    }

    // Matches the C#'s `new TimeSpan(0, hours, minutes, seconds).TotalMilliseconds` -
    // no frame-count fraction included, just whole H:M:S in milliseconds.
    int32_t utcMillis = ((hours * 3600) + (minutes * 60) + seconds) * 1000;
    uint64_t currentUtcMillis = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count());

    // 13-byte packet, big-endian throughout: int32 utcMillis, 1 byte frames, uint64
    // currentUtcMillis (real wall clock) - see the header/C# reference for the layout.
    uint8_t packet[13];
    packet[0] = static_cast<uint8_t>((static_cast<uint32_t>(utcMillis) >> 24) & 0xFF);
    packet[1] = static_cast<uint8_t>((static_cast<uint32_t>(utcMillis) >> 16) & 0xFF);
    packet[2] = static_cast<uint8_t>((static_cast<uint32_t>(utcMillis) >> 8) & 0xFF);
    packet[3] = static_cast<uint8_t>(static_cast<uint32_t>(utcMillis) & 0xFF);
    packet[4] = frames;
    for (int i = 0; i < 8; ++i) {
        packet[5 + i] = static_cast<uint8_t>((currentUtcMillis >> (56 - 8 * i)) & 0xFF);
    }

    for (int i = 0; i < 5; ++i) {
        if (sockets_[i] != 0) {
            send(static_cast<SOCKET>(sockets_[i]), reinterpret_cast<const char*>(packet), sizeof(packet), 0);
        }
    }
}

// --- UI / persistence -------------------------------------------------------

bool TimeCodeExporter::DrawUi() {
    bool changed = false;
    if (IsConnected()) {
        int hours, minutes, seconds;
        uint8_t frames;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            hours = hours_;
            minutes = minutes_;
            seconds = seconds_;
            frames = frames_;
        }
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "MTC: connected");
        ImGui::Text("Last timecode: %02d:%02d:%02d.%02d", hours, minutes, seconds, frames);
    } else {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "MTC: disconnected");
    }
    changed |= ImGui::InputText("MIDI Device", &midiDeviceName);
    if (ImGui::Button("Reconnect MIDI Device")) {
        Reconnect();
    }
    return changed;
}

void TimeCodeExporter::ReadYaml(const YAML::Node& node) {
    try {
        if (node["midiDevice"]) midiDeviceName = node["midiDevice"].as<std::string>();
    } catch (const YAML::Exception&) {
    }
}

void TimeCodeExporter::WriteYaml(YAML::Emitter& out) const {
    out << YAML::Key << "midiDevice" << YAML::Value << midiDeviceName;
}
