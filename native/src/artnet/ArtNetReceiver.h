#pragma once
// Minimal ArtNet (Art-Net) DMX-over-UDP receiver. Reimplements just enough of the
// protocol to parse OpDmx packets - reference implementation ported from
// Packages/net.naoyakohda.artnet-unity@.../Runtime/Scripts/{ArtNetReceiver.cs,UdpReceiver.cs,
// Core/Packets/{ArtNetPacket.cs,DmxPacket.cs}}.
//
// Wire format of an ArtDMX packet (all that this receiver needs to understand):
//   bytes 0-7   : ASCII "Art-Net\0"                (8 bytes)
//   bytes 8-9   : OpCode, little-endian             (0x5000 = ArtDMX -> bytes {0x00, 0x50})
//   bytes 10-11 : ProtVer, big-endian (network order), must be 14 (0x00, 0x0E)
//   byte  12    : Sequence (ignored)
//   byte  13    : Physical (ignored)
//   bytes 14-15 : Universe, little-endian uint16
//   bytes 16-17 : Length,   big-endian (network order) uint16, 1..512
//   bytes 18..  : DMX data, `Length` bytes
//
// Any packet that doesn't match this shape (bad ID, bad opcode, bad protocol version,
// truncated, length out of range) is silently dropped, matching the reference behavior
// of ArtNetPacket.Create returning null.
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <atomic>

class ArtNetReceiver {
public:
    // Called on the receive thread whenever a valid ArtDMX packet arrives.
    // `data` points to exactly `length` (1..512) bytes and is only valid for the
    // duration of the callback.
    using DmxCallback = std::function<void(uint16_t universe, const uint8_t* data, size_t length)>;

    ArtNetReceiver();
    ~ArtNetReceiver();

    ArtNetReceiver(const ArtNetReceiver&) = delete;
    ArtNetReceiver& operator=(const ArtNetReceiver&) = delete;

    void SetCallback(DmxCallback callback);

    // Starts (or restarts, if already running) the receive thread bound to
    // `bindAddress` (dotted-quad string, "0.0.0.0" to listen on all interfaces)
    // and `port` (default 6454). Safe to call again with new values to rebind,
    // matching Loader.cs calling artNetReceiver.ChangePort/ChangeIPAddress on
    // every show-config (re)load.
    bool Start(const std::string& bindAddress, uint16_t port);

    void Stop();

    bool IsRunning() const { return running_; }

private:
    void ReceiveLoop(std::string bindAddress, uint16_t port);

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};
    DmxCallback callback_;
    // Underlying socket handle (SOCKET on Windows), stored type-erased so this header
    // doesn't have to pull in <winsock2.h>.
    std::uintptr_t socketHandle_ = 0;
};
