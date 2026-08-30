#include "ArtNetReceiver.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstring>

namespace {

// ID(8) + OpCode(2) + ProtVer(2) + Sequence(1) + Physical(1) + Universe(2) + Length(2)
constexpr size_t kArtNetHeaderSize = 18;
constexpr uint16_t kOpCodeDmx = 0x5000;
constexpr uint16_t kProtVer = 14;
constexpr char kArtNetId[8] = {'A', 'r', 't', '-', 'N', 'e', 't', '\0'};

} // namespace

ArtNetReceiver::ArtNetReceiver() = default;

ArtNetReceiver::~ArtNetReceiver() {
    Stop();
}

void ArtNetReceiver::SetCallback(DmxCallback callback) {
    callback_ = std::move(callback);
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

        if (received < static_cast<int>(kArtNetHeaderSize)) {
            continue;
        }

        if (std::memcmp(buffer, kArtNetId, sizeof(kArtNetId)) != 0) {
            continue;
        }

        // OpCode: little-endian.
        uint16_t opCode = static_cast<uint16_t>(buffer[8]) | (static_cast<uint16_t>(buffer[9]) << 8);
        if (opCode != kOpCodeDmx) {
            continue;
        }

        // ProtVer: big-endian (network order).
        uint16_t protVer = (static_cast<uint16_t>(buffer[10]) << 8) | static_cast<uint16_t>(buffer[11]);
        if (protVer != kProtVer) {
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
