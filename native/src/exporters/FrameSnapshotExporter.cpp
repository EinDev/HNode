#include "FrameSnapshotExporter.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <nlohmann/json.hpp>

#include <cstring>
#include <filesystem>
#include <mutex>
#include <stdexcept>

// Port of Assets/Plugin/Exporters/FrameSnapshotExporter.cs. See the header for the
// overall protocol description.

FrameSnapshotExporter::FrameSnapshotExporter() = default;

FrameSnapshotExporter::~FrameSnapshotExporter() {
    Deconstruct();
}

void FrameSnapshotExporter::Construct() {
    // If we're already running, tear down first (mirrors ArtNetReceiver::Start's
    // defensive Stop()-before-start, in case Construct() is ever called twice).
    Deconstruct();

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return;
    }

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        WSACleanup();
        return;
    }

    BOOL reuseAddr = TRUE;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuseAddr), sizeof(reuseAddr));

    // Receive timeout so ListenLoop wakes up periodically to check stopRequested_ even
    // if no packets arrive.
    DWORD timeoutMs = 200;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9123);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock);
        WSACleanup();
        return;
    }

    socketHandle_ = static_cast<std::uintptr_t>(sock);
    stopRequested_ = false;

    thread_ = std::thread(&FrameSnapshotExporter::ListenLoop, this);
}

void FrameSnapshotExporter::Deconstruct() {
    stopRequested_ = true;

    if (socketHandle_ != 0) {
        SOCKET sock = static_cast<SOCKET>(socketHandle_);
        // Closing the socket unblocks any recvfrom() currently blocked in ListenLoop.
        closesocket(sock);
        socketHandle_ = 0;
        WSACleanup();
    }

    if (thread_.joinable()) {
        thread_.join();
    }

    stopRequested_ = false;
}

void FrameSnapshotExporter::ListenLoop() {
    SOCKET sock = static_cast<SOCKET>(socketHandle_);
    char buffer[4096];

    while (!stopRequested_) {
        sockaddr_in fromAddr{};
        int fromLen = sizeof(fromAddr);
        int received = recvfrom(sock, buffer, static_cast<int>(sizeof(buffer)), 0,
                                 reinterpret_cast<sockaddr*>(&fromAddr), &fromLen);

        if (received == SOCKET_ERROR) {
            // Expected on timeout (WSAETIMEDOUT); also hit once Deconstruct() closes the
            // socket out from under us (e.g. WSAENOTSOCK). Either way, loop back and
            // re-check stopRequested_.
            continue;
        }

        if (stopRequested_) {
            break;
        }

        if (received <= 0) {
            continue;
        }

        // The received bytes already ARE UTF-8 text - no separate decode step needed.
        std::string message(buffer, static_cast<size_t>(received));

        nlohmann::json packet = nlohmann::json::parse(message, nullptr, /*allow_exceptions=*/false);
        if (packet.is_discarded() || !packet.is_object()) {
            continue;
        }

        std::string command = packet.value("command", std::string());
        if (command != "save_frame") {
            continue;
        }

        int frameNumber = packet.value("frame_number", 0);
        std::string filePath = packet.value("file_path", std::string());
        int responsePort = packet.value("response_port", 0);

        char addrStr[INET_ADDRSTRLEN] = {};
        const char* addrResult = inet_ntop(AF_INET, &fromAddr.sin_addr, addrStr, sizeof(addrStr));
        std::string responseAddress = addrResult ? std::string(addrResult) : std::string();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            saveQueued_ = true;
            pendingFrameNumber_ = frameNumber;
            pendingFilePath_ = filePath;
            pendingResponsePort_ = responsePort;
            pendingResponseAddress_ = responseAddress;
        }

        // Wake the render loop and force a fresh frame so FrameRendered() gets called
        // with up-to-date pixels rather than stale ones.
        if (requestDirty_) {
            requestDirty_();
        }
    }
}

void FrameSnapshotExporter::FrameRendered(const std::vector<RGBA8>& pixels, int width, int height) {
    bool shouldSave = false;
    std::string filePath;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        shouldSave = saveQueued_;
        // Reset immediately (matches the C#'s `finally { imageSaveQueued = false; }` -
        // a failed save must not leave this stuck queued forever), before attempting
        // the save below.
        saveQueued_ = false;
        if (shouldSave) {
            filePath = pendingFilePath_;
        }
    }

    if (!shouldSave) {
        return; // No save requested.
    }

    try {
        if (pixels.empty() || width <= 0 || height <= 0) {
            throw std::runtime_error("No pixels to save.");
        }

        std::filesystem::path outPath(filePath);
        std::filesystem::path directory = outPath.parent_path();
        if (!directory.empty() && !std::filesystem::exists(directory)) {
            std::filesystem::create_directories(directory);
        }

        const unsigned char* rgba = reinterpret_cast<const unsigned char*>(pixels.data());
        int stride = width * 4;
        int ok = stbi_write_png(filePath.c_str(), width, height, 4, rgba, stride);

        if (ok != 0) {
            SendResponse("saved", "");
        } else {
            SendResponse("failed", "stbi_write_png failed");
        }
    } catch (const std::filesystem::filesystem_error& e) {
        SendResponse("failed", e.what());
    } catch (const std::exception& e) {
        SendResponse("failed", e.what());
    }
}

void FrameSnapshotExporter::SendResponse(const std::string& status, const std::string& error) {
    int responsePort;
    std::string responseAddress;
    int frameNumber;
    std::string filePath;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        responsePort = pendingResponsePort_;
        responseAddress = pendingResponseAddress_;
        frameNumber = pendingFrameNumber_;
        filePath = pendingFilePath_;
    }

    if (responsePort <= 0 || responseAddress.empty()) {
        return;
    }

    nlohmann::json response;
    response["frame_number"] = frameNumber;
    response["file_path"] = filePath;
    response["status"] = status;
    // Chose to always include "error" (empty string in the success case) rather than
    // omitting the key, for consistent response shape regardless of status - the C#'s
    // `null` default for the success case doesn't have a direct JSON equivalent that's
    // clearly better than an empty string here.
    response["error"] = error;

    std::string payload = response.dump();

    // Best-effort send via a fresh socket, matching the C#'s `using (UdpClient
    // responseClient = new UdpClient())` - never reuse the listening socket, and never
    // let a send failure propagate.
    SOCKET sendSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sendSock == INVALID_SOCKET) {
        return;
    }

    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(static_cast<u_short>(responsePort));
    if (inet_pton(AF_INET, responseAddress.c_str(), &dest.sin_addr) == 1) {
        sendto(sendSock, payload.c_str(), static_cast<int>(payload.size()), 0,
               reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
    }

    closesocket(sendSock);
}
