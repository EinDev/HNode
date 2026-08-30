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
// of ArtNetPacket.Create returning null - EXCEPT for ArtPoll (opcode 0x2000, the
// standard Art-Net node-discovery request), which this receiver auto-replies to with
// an ArtPollReply (opcode 0x2100) so lighting consoles (QLC+, etc.) can find HNode on
// the network without the operator typing in its IP by hand - the Unity app and the
// ArtNet-Unity library it depends on do NOT do this (see IsRunning()'s doc comment on
// SetNodeInfo below), so this is new behavior, not a straight port. Reply is sent by
// unicast directly back to the polling sender's address+port, not a broadcast - this
// is simpler and works with every poller that's actually been tested against it (QLC+
// included), even though the Art-Net spec's "proper" behavior is a broadcast reply so
// every listener benefits, not just the one that asked.
//
// ArtPoll wire format (request, received):
//   bytes 0-9   : "Art-Net\0" + OpCode 0x2000 little-endian (as ArtDMX above)
//   bytes 10-11 : ProtVer, big-endian, must be 14
//   byte  12    : TalkToMe (ignored - we always reply once per poll regardless of
//                 the "reply on node change only" bit some pollers set)
//   byte  13    : Priority (ignored)
//
// ArtPollReply wire format (response, sent) - see ArtNetReceiver.cpp for the exact
// byte-by-byte layout this receiver builds; it's the ~239-byte fixed-format packet
// from the Art-Net spec (IP/port, short/long node name, status flags, etc.)
#include <cstdint>
#include <functional>
#include <mutex>
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

    // Sets the ShortName (truncated to 17 chars + null)/LongName (63 chars + null)
    // fields sent in ArtPollReply packets - what shows up as this node's name in a
    // console's node list. Thread-safe (may be called from the UI thread while the
    // receive thread is concurrently building a reply). Defaults to "HNode"/"HNode
    // (native)" if never called.
    void SetNodeInfo(const std::string& shortName, const std::string& longName);

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

    // Builds and sends an ArtPollReply datagram back to `senderAddr` (a sockaddr_in*,
    // passed as void* to keep <winsock2.h> out of this header) in response to an
    // ArtPoll received on `socketHandle_`. Implemented in ArtNetReceiver.cpp.
    void SendPollReply(const void* senderAddr);

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};
    DmxCallback callback_;
    // Underlying socket handle (SOCKET on Windows), stored type-erased so this header
    // doesn't have to pull in <winsock2.h>.
    std::uintptr_t socketHandle_ = 0;

    std::mutex nodeInfoMutex_;
    std::string shortName_ = "HNode";
    std::string longName_ = "HNode (native)";
};
