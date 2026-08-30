#include "OnTimeGenerator.h"
#include "DmxUtil.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <chrono>
#include <vector>

#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>
#include "imgui.h"

// Port of Assets/Plugin/Generators/OnTime/GeneratorOnTime.cs. See the header for the
// polling-thread rationale.

namespace {

// The C# reference polls once per GenerateDMX() call (effectively ~60Hz in Unity, a
// synchronous blocking HTTP GET every frame). Polling a local HTTP server 10x/second
// from a background thread is smooth enough for a timer display without hammering it
// or ever blocking the render thread - see the header comment.
constexpr int kPollIntervalMs = 100;
constexpr wchar_t kHost[] = L"localhost";
constexpr INTERNET_PORT kPort = 4001;
constexpr wchar_t kPath[] = L"/api/poll";

bool HttpGetLocal(std::string& outBody) {
    HINTERNET hSession =
        WinHttpOpen(L"HNode/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    // Short timeouts (ms): resolve, connect, send, receive - a stalled or absent
    // OnTime server must not block the poll thread for long.
    WinHttpSetTimeouts(hSession, 2000, 2000, 2000, 2000);

    bool ok = false;
    HINTERNET hConnect = WinHttpConnect(hSession, kHost, kPort, 0);
    if (hConnect) {
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", kPath, nullptr, WINHTTP_NO_REFERER,
                                                 WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
        if (hRequest) {
            if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(hRequest, nullptr)) {
                outBody.clear();
                DWORD available = 0;
                while (WinHttpQueryDataAvailable(hRequest, &available) && available > 0) {
                    std::vector<char> buf(available);
                    DWORD read = 0;
                    if (!WinHttpReadData(hRequest, buf.data(), available, &read)) break;
                    outBody.append(buf.data(), read);
                }
                ok = true;
            }
            WinHttpCloseHandle(hRequest);
        }
        WinHttpCloseHandle(hConnect);
    }
    WinHttpCloseHandle(hSession);
    return ok;
}

} // namespace

OnTimeGenerator::OnTimeGenerator() = default;

OnTimeGenerator::~OnTimeGenerator() {
    Deconstruct();
}

void OnTimeGenerator::Construct() {
    Deconstruct(); // idempotent restart
    stopRequested_ = false;
    pollThread_ = std::thread(&OnTimeGenerator::PollLoop, this);
}

void OnTimeGenerator::Deconstruct() {
    stopRequested_ = true;
    if (pollThread_.joinable()) {
        pollThread_.join();
    }
    connected_ = false;
}

void OnTimeGenerator::PollLoop() {
    while (!stopRequested_) {
        std::string body;
        if (HttpGetLocal(body)) {
            nlohmann::json doc = nlohmann::json::parse(body, nullptr, /*allow_exceptions=*/false);
            if (!doc.is_discarded() && doc.is_object()) {
                nlohmann::json payload = doc.value("payload", nlohmann::json::object());
                nlohmann::json timer = payload.value("timer", nlohmann::json::object());
                nlohmann::json auxtimer1 = payload.value("auxtimer1", nlohmann::json::object());
                nlohmann::json message = payload.value("message", nlohmann::json::object());
                nlohmann::json msgTimer = message.value("timer", nlohmann::json::object());

                int timerElapsed = timer.value("elapsed", 0);
                int timerDuration = timer.value("duration", 0);
                float progress = 0.0f;
                if (timerDuration != 0) {
                    // Guard against the C# reference's implicit NaN/Infinity on a
                    // zero duration (float division then clamp) - fall back to 0
                    // instead, since a NaN-derived byte is undefined behavior in C++.
                    progress = static_cast<float>(timerElapsed) / static_cast<float>(timerDuration);
                    progress = std::clamp(progress, 0.0f, 1.0f);
                }

                std::string secondarySource;
                if (msgTimer.contains("secondarySource") && msgTimer["secondarySource"].is_string()) {
                    secondarySource = msgTimer["secondarySource"].get<std::string>();
                }

                State next;
                next.clock = payload.value("clock", 0);
                next.timerCurrent = timer.value("current", 0);
                next.auxTimerCurrent = auxtimer1.value("current", 0);
                next.progress = progress;
                next.timerVisible = msgTimer.value("visible", false);
                next.timerText = msgTimer.value("text", std::string());
                next.timerBlink = msgTimer.value("blink", false);
                next.timerBlackout = msgTimer.value("blackout", false);
                next.secondarySource = secondarySource;
                next.externalMessage = message.value("external", std::string());

                {
                    std::lock_guard<std::mutex> lock(stateMutex_);
                    state_ = std::move(next);
                }
                connected_ = true;
            } else {
                connected_ = false;
            }
        } else {
            connected_ = false;
        }

        for (int i = 0; i < kPollIntervalMs / 20 && !stopRequested_; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
}

void OnTimeGenerator::GenerateDMX(std::vector<uint8_t>& dmxData) {
    State snapshot;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        snapshot = state_;
    }

    // 14-byte payload: int32 clock, int32 timer.current, int32 auxtimer1.current (all
    // little-endian, matching .NET's BitConverter.GetBytes on Windows), 1 progress
    // byte (0..1 mapped to 0..255), 1 flags byte - see the C# reference for the exact
    // bit assignments this reproduces.
    std::vector<uint8_t> payload(14, 0);
    auto writeI32LE = [&](size_t offset, int32_t v) {
        payload[offset + 0] = static_cast<uint8_t>(static_cast<uint32_t>(v) & 0xFF);
        payload[offset + 1] = static_cast<uint8_t>((static_cast<uint32_t>(v) >> 8) & 0xFF);
        payload[offset + 2] = static_cast<uint8_t>((static_cast<uint32_t>(v) >> 16) & 0xFF);
        payload[offset + 3] = static_cast<uint8_t>((static_cast<uint32_t>(v) >> 24) & 0xFF);
    };
    writeI32LE(0, snapshot.clock);
    writeI32LE(4, snapshot.timerCurrent);
    writeI32LE(8, snapshot.auxTimerCurrent);
    payload[12] = static_cast<uint8_t>(snapshot.progress * 255.0f);

    uint8_t flags = 0;
    if (snapshot.timerVisible) flags |= 0x01;
    if (snapshot.secondarySource == "external") flags |= 0x02;
    if (snapshot.timerBlink) flags |= 0x04;
    if (snapshot.timerBlackout) flags |= 0x08;
    if (snapshot.secondarySource == "aux") flags |= 0x10;
    payload[13] = flags;

    WriteDmxAtPosition(dmxData, payload, static_cast<size_t>(dataPayloadChannelStart));

    // Text field priority: the timer's own message wins when visible; otherwise fall
    // back to the external message only if secondarySource=="external" and it's
    // non-empty; otherwise blank. Matches the C# reference exactly.
    std::string text;
    if (snapshot.timerVisible) {
        text = snapshot.timerText;
    } else if (snapshot.secondarySource == "external" && !snapshot.externalMessage.empty()) {
        text = snapshot.externalMessage;
    }

    inner_.text = std::move(text);
    inner_.channelStart = channelStart;
    inner_.unicode = unicode;
    inner_.limitLength = limitLength;
    inner_.maxCharacters = maxCharacters;
    inner_.GenerateDMX(dmxData);
}

bool OnTimeGenerator::DrawUi() {
    bool changed = false;
    if (connected_.load()) {
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "OnTime: connected (localhost:4001)");
    } else {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "OnTime: no response from localhost:4001");
    }
    changed |= ImGui::InputInt("Data Payload Channel Start", &dataPayloadChannelStart);
    changed |= ImGui::InputInt("Channel Start", &channelStart);
    changed |= ImGui::Checkbox("Unicode", &unicode);
    changed |= ImGui::Checkbox("Limit Length", &limitLength);
    changed |= ImGui::InputInt("Length Limit", &maxCharacters);
    return changed;
}

void OnTimeGenerator::ReadYaml(const YAML::Node& node) {
    try {
        if (node["dataPayloadChannelStart"]) dataPayloadChannelStart = node["dataPayloadChannelStart"].as<int>();
        if (node["channelStart"]) channelStart = node["channelStart"].as<int>();
        if (node["unicode"]) unicode = node["unicode"].as<bool>();
        if (node["limitLength"]) limitLength = node["limitLength"].as<bool>();
        if (node["maxCharacters"]) maxCharacters = node["maxCharacters"].as<int>();
    } catch (const YAML::Exception&) {
    }
}

void OnTimeGenerator::WriteYaml(YAML::Emitter& out) const {
    out << YAML::Key << "dataPayloadChannelStart" << YAML::Value << dataPayloadChannelStart;
    out << YAML::Key << "channelStart" << YAML::Value << channelStart;
    out << YAML::Key << "unicode" << YAML::Value << unicode;
    out << YAML::Key << "limitLength" << YAML::Value << limitLength;
    out << YAML::Key << "maxCharacters" << YAML::Value << maxCharacters;
}
