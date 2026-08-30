#include "ArtNetReceiver.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace {

// ID(8) + OpCode(2) + ProtVer(2) + Sequence(1) + Physical(1) + Universe(2) + Length(2)
constexpr size_t kArtNetHeaderSize = 18;
// ID(8) + OpCode(2) + ProtVer(2) - the header shared by both ArtDMX and ArtPoll, enough
// to identify a packet's opcode before dispatching to type-specific parsing/validation.
constexpr size_t kArtNetHeaderMinSize = 12;
// ArtPoll's own minimum size: shared header(12) + TalkToMe(1) + Priority(1).
constexpr size_t kArtPollMinSize = 14;
constexpr uint16_t kOpCodeDmx = 0x5000;
constexpr uint16_t kOpCodePoll = 0x2000;
constexpr uint16_t kProtVer = 14;
constexpr char kArtNetId[8] = {'A', 'r', 't', '-', 'N', 'e', 't', '\0'};

// Best-effort lookup of one of this machine's IPv4 addresses, in network byte order (i.e.
// the 4 bytes as they'd appear in a dotted-quad address). Used to populate ArtPollReply's
// IP-address/BindIp fields. Returns false (leaving outBytes untouched) if the lookup fails
// for any reason - callers should fall back to 0.0.0.0 rather than treat this as fatal.
bool GetLocalIPv4(uint8_t outBytes[4]) {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        return false;
    }

    hostent* host = gethostbyname(hostname);
    if (host == nullptr || host->h_addrtype != AF_INET || host->h_addr_list == nullptr ||
        host->h_addr_list[0] == nullptr) {
        return false;
    }

    std::memcpy(outBytes, host->h_addr_list[0], 4);
    return true;
}

// Zero-fills `fieldSize` bytes starting at `dest`, then copies in up to `fieldSize - 1`
// bytes of `src`, leaving at least the last byte (and any unused trailing bytes) as the
// zero/null terminator - used for ArtPollReply's ShortName/LongName/NodeReport fields.
void PackString(uint8_t* dest, size_t fieldSize, const std::string& src) {
    std::memset(dest, 0, fieldSize);
    // Parenthesized to dodge the min() macro pulled in by <windows.h> (via winsock2.h).
    size_t copyLen = (std::min)(src.size(), fieldSize - 1);
    std::memcpy(dest, src.data(), copyLen);
}

} // namespace

ArtNetReceiver::ArtNetReceiver() = default;

ArtNetReceiver::~ArtNetReceiver() {
    Stop();
}

void ArtNetReceiver::SetCallback(DmxCallback callback) {
    callback_ = std::move(callback);
}

void ArtNetReceiver::SetNodeInfo(const std::string& shortName, const std::string& longName) {
    std::lock_guard<std::mutex> lock(nodeInfoMutex_);
    shortName_ = shortName;
    longName_ = longName;
}

bool ArtNetReceiver::Start(const std::string& bindAddress, uint16_t port) {
    // If we're already running (e.g. Loader.cs re-applying show config), tear down the
    // previous socket/thread before rebinding to the new address/port.
    Stop();

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return false;
    }

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }

    // Allow quick rebinding to the same port across Start()/Stop() cycles.
    BOOL reuseAddr = TRUE;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuseAddr), sizeof(reuseAddr));

    // Receive timeout so ReceiveLoop wakes up periodically to check stopRequested_ even
    // if no packets arrive.
    DWORD timeoutMs = 200;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    const std::string& addrToParse = bindAddress.empty() ? std::string("0.0.0.0") : bindAddress;
    if (inet_pton(AF_INET, addrToParse.c_str(), &addr.sin_addr) != 1) {
        // Fall back to listening on all interfaces if the address string couldn't be parsed.
        addr.sin_addr.s_addr = INADDR_ANY;
    }

    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock);
        WSACleanup();
        return false;
    }

    socketHandle_ = static_cast<std::uintptr_t>(sock);
    stopRequested_ = false;
    running_ = true;

    thread_ = std::thread(&ArtNetReceiver::ReceiveLoop, this, bindAddress, port);
    return true;
}

void ArtNetReceiver::Stop() {
    stopRequested_ = true;

    if (socketHandle_ != 0) {
        SOCKET sock = static_cast<SOCKET>(socketHandle_);
        // Closing the socket unblocks any recvfrom() currently blocked in ReceiveLoop.
        closesocket(sock);
        socketHandle_ = 0;
        WSACleanup();
    }

    if (thread_.joinable()) {
        thread_.join();
    }

    running_ = false;
    stopRequested_ = false;
}

void ArtNetReceiver::ReceiveLoop(std::string bindAddress, uint16_t port) {
    // bindAddress/port were only needed to set up the socket, which Start() already did
    // before spawning this thread.
    (void)bindAddress;
    (void)port;

    SOCKET sock = static_cast<SOCKET>(socketHandle_);
    uint8_t buffer[2048];

    while (!stopRequested_) {
        sockaddr_in fromAddr{};
        int fromLen = sizeof(fromAddr);
        int received = recvfrom(sock, reinterpret_cast<char*>(buffer), static_cast<int>(sizeof(buffer)), 0,
                                 reinterpret_cast<sockaddr*>(&fromAddr), &fromLen);

        if (received == SOCKET_ERROR) {
            // Expected on timeout (WSAETIMEDOUT); also hit once Stop() closes the socket
            // out from under us (e.g. WSAENOTSOCK). Either way, loop back and re-check
            // stopRequested_.
            continue;
        }

        if (stopRequested_) {
            break;
        }

        if (received < static_cast<int>(kArtNetHeaderMinSize)) {
            continue;
        }

        if (std::memcmp(buffer, kArtNetId, sizeof(kArtNetId)) != 0) {
            continue;
        }

        // OpCode: little-endian.
        uint16_t opCode = static_cast<uint16_t>(buffer[8]) | (static_cast<uint16_t>(buffer[9]) << 8);

        // ProtVer: big-endian (network order) - shared by both ArtDMX and ArtPoll.
        uint16_t protVer = (static_cast<uint16_t>(buffer[10]) << 8) | static_cast<uint16_t>(buffer[11]);
        if (protVer != kProtVer) {
            continue;
        }

        if (opCode == kOpCodePoll) {
            // ArtPoll: header(12) + TalkToMe(1) + Priority(1) = 14 bytes minimum. Both
            // fields are ignored - we always reply once per poll (see ArtNetReceiver.h's
            // header comment for why this differs from the reference Unity implementation).
            if (received < static_cast<int>(kArtPollMinSize)) {
                continue;
            }
            SendPollReply(&fromAddr);
            continue;
        }

        if (opCode != kOpCodeDmx) {
            continue;
        }

        if (received < static_cast<int>(kArtNetHeaderSize)) {
            continue;
        }

        // Universe: little-endian.
        uint16_t universe = static_cast<uint16_t>(buffer[14]) | (static_cast<uint16_t>(buffer[15]) << 8);

        // Length: big-endian (network order), must be 1..512.
        uint16_t length = (static_cast<uint16_t>(buffer[16]) << 8) | static_cast<uint16_t>(buffer[17]);
        if (length < 1 || length > 512) {
            continue;
        }

        // Make sure the datagram actually contains `length` bytes of DMX data.
        size_t totalNeeded = kArtNetHeaderSize + static_cast<size_t>(length);
        if (static_cast<size_t>(received) < totalNeeded) {
            continue;
        }

        if (callback_) {
            callback_(universe, buffer + kArtNetHeaderSize, length);
        }
    }
}

void ArtNetReceiver::SendPollReply(const void* senderAddr) {
    const sockaddr_in* dest = static_cast<const sockaddr_in*>(senderAddr);

    // Fixed-format ~239-byte ArtPollReply packet (Art-Net spec). Zero-initialize first so
    // every field this receiver doesn't meaningfully populate (port declarations, spares,
    // etc.) comes out as a compliant zero rather than uninitialized garbage.
    constexpr size_t kReplySize = 239;
    std::array<uint8_t, kReplySize> packet{};

    // ID.
    std::memcpy(packet.data(), kArtNetId, sizeof(kArtNetId));

    // OpCode 0x2100, little-endian.
    packet[8] = 0x00;
    packet[9] = 0x21;

    // IP address, offset 10-13: 4 bytes, network byte order (dotted-quad order). Falls
    // back to 0.0.0.0 (already zeroed) if the local-address lookup fails.
    uint8_t ipBytes[4] = {0, 0, 0, 0};
    GetLocalIPv4(ipBytes);
    std::memcpy(&packet[10], ipBytes, 4);

    // Port 6454, little-endian.
    packet[14] = 0x36;
    packet[15] = 0x19;

    // VersInfo, little-endian - version "1".
    packet[16] = 0x00;
    packet[17] = 0x01;

    // NetSwitch(18)/SubSwitch(19): 0, HNode doesn't declare per-port universe addressing.

    // Oem, little-endian - 0xFFFF ("Oem Unknown"), the standard placeholder for a
    // non-registered/hobbyist implementation.
    packet[20] = 0xFF;
    packet[21] = 0xFF;

    // UbeaVersion(22): 0, no UBEA.

    // Status1(23): 0x00. Left at the simplest safe minimal value rather than asserting
    // specific Status1 bit semantics (e.g. an "all parameters programmed by network"
    // indicator) this receiver has no real basis to claim.
    packet[23] = 0x00;

    // EstaMan(24-25), little-endian: 0x0000, no ESTA manufacturer code registered.

    // ShortName/LongName (mutex-protected copies - SetNodeInfo may run concurrently on
    // another thread), truncated + null-padded to fit their fixed field widths.
    std::string shortName;
    std::string longName;
    {
        std::lock_guard<std::mutex> lock(nodeInfoMutex_);
        shortName = shortName_;
        longName = longName_;
    }
    PackString(&packet[26], 18, shortName);
    PackString(&packet[44], 64, longName);

    // NodeReport: short human-readable status string.
    PackString(&packet[108], 64, std::string("#0001 [0001] HNode ready"));

    // NumPorts(172-173), PortTypes/GoodInput/GoodOutput/SwIn/SwOut(174-193), SwVideo(194),
    // SwMacro(195), SwRemote(196), Spare(197-199): all 0 - HNode declares zero DMX
    // input/output "ports" in the patchable sense; already zero from zero-initialization.

    // Style(200): 0 (StNode - an Art-Net to DMX512 device); already zero.

    // MAC(201-206): not looked up, left zero-filled (already zero) - GetLocalIPv4's
    // gethostbyname()-based lookup doesn't expose a MAC and a separate lookup isn't
    // worth the added complexity for discovery purposes.

    // BindIp, offset 207-210: same as the IP-address field above.
    std::memcpy(&packet[207], ipBytes, 4);

    // BindIndex(211): 1.
    packet[211] = 1;

    // Status2(212): 0x08 (bit3 set - "Node supports 15 bit Port-Address (Art-Net 3/4)"),
    // a reasonable and truthful minimal claim since this receiver already accepts a full
    // 15-bit universe value.
    packet[212] = 0x08;

    // Remaining bytes (213-238, Status3/Filler/etc.): all zero, already zero from
    // zero-initialization - pads the packet out to exactly 239 bytes total.

    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port = dest->sin_port;
    dst.sin_addr = dest->sin_addr;

    SOCKET sock = static_cast<SOCKET>(socketHandle_);
    // Best-effort UDP send, matching this codebase's general philosophy elsewhere (e.g.
    // MidiDmxExporter's watchdog sends) - silently drop on failure, nothing to log/retry.
    sendto(sock, reinterpret_cast<const char*>(packet.data()), static_cast<int>(packet.size()), 0,
           reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
}
