#include "TwitchChatGenerator.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <random>

#include <yaml-cpp/yaml.h>
#include "imgui.h"
#include "imgui_stdlib.h"

// Port of Assets/Plugin/Generators/GeneratorTwitchChat.cs. See the header for the
// overall approach; this file hand-rolls the IRC protocol pieces that used to come
// from the C# reference's Lexone.UnityTwitchChat dependency.

namespace {

constexpr char kIrcHost[] = "irc.chat.twitch.tv";
constexpr char kIrcPort[] = "6667";

std::string ToLowerAscii(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// Mirrors ParseHelper.cs's PRIVMSG parsing: login is the substring between the
// leading ':' and the first '!'; message is everything after the 3rd space
// (typically starting with ':', which is stripped here).
bool ParsePrivMsg(const std::string& line, std::string& login, std::string& message) {
    if (line.empty() || line[0] != ':') return false;
    if (line.find(" PRIVMSG ") == std::string::npos) return false;

    size_t bangPos = line.find('!');
    if (bangPos == std::string::npos || bangPos < 1) return false;
    login = line.substr(1, bangPos - 1);

    size_t searchFrom = 0;
    size_t spacePos = std::string::npos;
    for (int n = 0; n < 3; ++n) {
        spacePos = line.find(' ', searchFrom);
        if (spacePos == std::string::npos) return false;
        searchFrom = spacePos + 1;
    }
    std::string rest = line.substr(searchFrom);
    if (!rest.empty() && rest[0] == ':') rest = rest.substr(1);
    message = rest;
    return true;
}

} // namespace

TwitchChatGenerator::TwitchChatGenerator() = default;

TwitchChatGenerator::~TwitchChatGenerator() {
    Deconstruct();
}

void TwitchChatGenerator::Construct() {
    Deconstruct(); // idempotent restart, matches other Construct()s' defensive pattern
    stopRequested_ = false;
    connectionThread_ = std::thread(&TwitchChatGenerator::ConnectionLoop, this);
}

void TwitchChatGenerator::Deconstruct() {
    stopRequested_ = true;
    {
        // Closing the socket unblocks a recv() currently blocked in RunConnection().
        std::lock_guard<std::mutex> lock(socketMutex_);
        if (socketHandle_ != 0) {
            closesocket(static_cast<SOCKET>(socketHandle_));
            socketHandle_ = 0;
        }
    }
    if (connectionThread_.joinable()) {
        connectionThread_.join();
    }
    connected_ = false;
}

void TwitchChatGenerator::ConnectionLoop() {
    int backoffSeconds = 1;
    while (!stopRequested_) {
        RunConnection(); // blocks until disconnected or stopRequested_
        connected_ = false;
        if (stopRequested_) break;

        for (int i = 0; i < backoffSeconds * 10 && !stopRequested_; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        // Not std::min() - windows.h's min/max macros (pulled in via winsock2.h, no
        // NOMINMAX defined project-wide) shadow it and break the call.
        backoffSeconds = (backoffSeconds * 2 < 16) ? backoffSeconds * 2 : 16;
    }
}

void TwitchChatGenerator::RunConnection() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return;

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* result = nullptr;
    if (getaddrinfo(kIrcHost, kIrcPort, &hints, &result) != 0 || !result) {
        WSACleanup();
        return;
    }

    SOCKET sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (sock == INVALID_SOCKET ||
        connect(sock, result->ai_addr, static_cast<int>(result->ai_addrlen)) == SOCKET_ERROR) {
        if (sock != INVALID_SOCKET) closesocket(sock);
        freeaddrinfo(result);
        WSACleanup();
        return;
    }
    freeaddrinfo(result);

    // Receive timeout so the read loop wakes up periodically to check stopRequested_
    // even with no chat traffic - matches FrameSnapshotExporter's ListenLoop.
    DWORD timeoutMs = 200;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));

    {
        std::lock_guard<std::mutex> lock(socketMutex_);
        socketHandle_ = static_cast<std::uintptr_t>(sock);
    }

    // Anonymous login handshake - matches TwitchConnection.cs's Connect(): a random
    // "justinfanNNNN" nick, an empty oauth token, and the tags/commands capability
    // request. This app is read-only (never sends chat), so anonymous login is enough.
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<int> dist(1000, 9999);
    std::string username = "justinfan" + std::to_string(dist(rng));

    auto sendLine = [&](const std::string& line) {
        std::string withCrlf = line + "\r\n";
        send(sock, withCrlf.c_str(), static_cast<int>(withCrlf.size()), 0);
    };
    sendLine("PASS oauth:");
    sendLine("NICK " + ToLowerAscii(username));
    sendLine("CAP REQ :twitch.tv/tags twitch.tv/commands");

    std::string channelLower = ToLowerAscii(channelName);
    bool joined = false;
    std::string accum;
    char buffer[2048];

    while (!stopRequested_) {
        int received = recv(sock, buffer, static_cast<int>(sizeof(buffer)), 0);
        if (received == 0) break; // orderly remote close
        if (received == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAETIMEDOUT) continue; // just the periodic stop-check wakeup
            break;                              // real error, or our own closesocket() in Deconstruct()
        }

        accum.append(buffer, static_cast<size_t>(received));
        size_t newlinePos;
        while ((newlinePos = accum.find('\n')) != std::string::npos) {
            std::string line = accum.substr(0, newlinePos);
            accum.erase(0, newlinePos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;

            if (line.rfind("PING", 0) == 0) {
                sendLine("PONG :tmi.twitch.tv");
                continue;
            }
            if (!joined && line.find(" 001 ") != std::string::npos && !channelLower.empty()) {
                sendLine("JOIN #" + channelLower);
                joined = true;
                connected_ = true;
                continue;
            }

            HandleLine(line);
        }
    }

    {
        std::lock_guard<std::mutex> lock(socketMutex_);
        if (socketHandle_ == static_cast<std::uintptr_t>(sock)) {
            socketHandle_ = 0;
        }
    }
    closesocket(sock);
    WSACleanup();
    connected_ = false;
}

void TwitchChatGenerator::HandleLine(const std::string& line) {
    std::string login, message;
    if (!ParsePrivMsg(line, login, message)) return;

    std::lock_guard<std::mutex> lock(messagesMutex_);
    messages_.emplace_back(std::move(login), std::move(message));
    const size_t capacity = chatMessages > 0 ? static_cast<size_t>(chatMessages) : 0;
    while (messages_.size() > capacity) {
        messages_.pop_front();
    }
}

void TwitchChatGenerator::GenerateDMX(std::vector<uint8_t>& dmxData) {
    std::string text;
    {
        std::lock_guard<std::mutex> lock(messagesMutex_);
        for (const auto& entry : messages_) {
            text += entry.first;
            text += ": ";
            text += entry.second;
            text += "\n";
        }
    }

    inner_.text = std::move(text);
    inner_.channelStart = channelStart;
    inner_.unicode = unicode;
    inner_.limitLength = limitLength;
    inner_.maxCharacters = maxCharacters;
    inner_.GenerateDMX(dmxData);
}

bool TwitchChatGenerator::DrawUi() {
    bool changed = false;
    if (connected_.load()) {
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "Twitch: connected");
    } else {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Twitch: connecting...");
    }
    changed |= ImGui::InputText("Channel Name", &channelName);
    changed |= ImGui::InputInt("Chat Messages", &chatMessages);
    changed |= ImGui::InputInt("Channel Start", &channelStart);
    changed |= ImGui::Checkbox("Unicode", &unicode);
    changed |= ImGui::Checkbox("Limit Length", &limitLength);
    changed |= ImGui::InputInt("Length Limit", &maxCharacters);
    return changed;
}

void TwitchChatGenerator::ReadYaml(const YAML::Node& node) {
    try {
        if (node["ChannelName"]) channelName = node["ChannelName"].as<std::string>();
        if (node["chatMessages"]) chatMessages = node["chatMessages"].as<int>();
        if (node["channelStart"]) channelStart = node["channelStart"].as<int>();
        if (node["unicode"]) unicode = node["unicode"].as<bool>();
        if (node["limitLength"]) limitLength = node["limitLength"].as<bool>();
        if (node["maxCharacters"]) maxCharacters = node["maxCharacters"].as<int>();
    } catch (const YAML::Exception&) {
    }
}

void TwitchChatGenerator::WriteYaml(YAML::Emitter& out) const {
    out << YAML::Key << "ChannelName" << YAML::Value << channelName;
    out << YAML::Key << "chatMessages" << YAML::Value << chatMessages;
    out << YAML::Key << "channelStart" << YAML::Value << channelStart;
    out << YAML::Key << "unicode" << YAML::Value << unicode;
    out << YAML::Key << "limitLength" << YAML::Value << limitLength;
    out << YAML::Key << "maxCharacters" << YAML::Value << maxCharacters;
}
