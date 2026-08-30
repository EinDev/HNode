#include "MAVLinkDroneNetworkGenerator.h"
#include "DmxUtil.h"
#include "mavlink/Crc32.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

#include <yaml-cpp/yaml.h>
#include "imgui.h"

// Port of Assets/Plugin/Generators/MAVLinkDrone/GeneratorMAVLinkDroneNetwork.cs. See
// the header for the overall architecture and threading-model notes.

namespace {

// Mirrors Assets/Plugin/Generators/MAVLinkDrone/FTPMessage.cs's [StructLayout(Pack=1,
// Size=251)] struct - the payload MAVLink's FILE_TRANSFER_PROTOCOL message carries.
// x86/x64 is little-endian, matching .NET's default field layout on Windows, so a
// straight memcpy in either direction reproduces the same wire bytes.
#pragma pack(push, 1)
struct FtpMessage {
    uint16_t seqNumber;
    uint8_t session;
    uint8_t opcode;
    uint8_t size;
    uint8_t reqOpcode;
    uint8_t burstComplete;
    uint8_t padding;
    uint32_t offset;
    uint8_t data[239];
};
#pragma pack(pop)
static_assert(sizeof(FtpMessage) == 251, "FTPMessage wire layout must be exactly 251 bytes");

enum FtpOpcode : uint8_t {
    kFtpTerminateSession = 1,
    kFtpResetSessions = 2,
    kFtpCreateFile = 6,
    kFtpWriteFile = 7,
    kFtpCalcFileCRC32 = 14,
    kFtpAck = 128,
};

// Port of GeneratorMAVLinkDroneNetwork.cs's Util.CoarseFineChannelSet: maps a 0..1
// float to a 16-bit value's high/low bytes.
void PackCoarseFine(float value, uint8_t& coarse, uint8_t& fine) {
    float clamped = std::clamp(value, 0.0f, 1.0f);
    uint16_t fullValue = static_cast<uint16_t>(clamped * 65535.0f);
    coarse = static_cast<uint8_t>((fullValue >> 8) & 0xFF);
    fine = static_cast<uint8_t>(fullValue & 0xFF);
}

float InverseLerpClamped(float a, float b, float value) {
    return std::clamp((value - a) / (b - a), 0.0f, 1.0f);
}

} // namespace

MAVLinkDroneNetworkGenerator::MAVLinkDroneNetworkGenerator() {
    layoutProvider = std::make_unique<GridLayout>();
}

MAVLinkDroneNetworkGenerator::~MAVLinkDroneNetworkGenerator() {
    Deconstruct();
}

// --- socket lifecycle -------------------------------------------------------

void MAVLinkDroneNetworkGenerator::OpenSocket() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return;

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        WSACleanup();
        return;
    }

    sockaddr_in selfAddr{};
    selfAddr.sin_family = AF_INET;
    selfAddr.sin_addr.s_addr = INADDR_ANY;
    selfAddr.sin_port = 0; // ephemeral - matches the C# reference's `new UdpClient()` + `Bind(Any, 0)`
    if (bind(sock, reinterpret_cast<sockaddr*>(&selfAddr), sizeof(selfAddr)) == SOCKET_ERROR) {
        closesocket(sock);
        WSACleanup();
        return;
    }

    // Short receive timeout so NetworkLoop wakes up regularly to pace outgoing sends
    // even with no inbound traffic, and to notice stopRequested_ promptly.
    DWORD timeoutMs = 20;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));

    socketHandle_ = static_cast<std::uintptr_t>(sock);
}

void MAVLinkDroneNetworkGenerator::CloseSocket() {
    if (socketHandle_ != 0) {
        closesocket(static_cast<SOCKET>(socketHandle_));
        socketHandle_ = 0;
        WSACleanup();
    }
}

// --- lifecycle -------------------------------------------------------

void MAVLinkDroneNetworkGenerator::Construct() {
    Deconstruct(); // idempotent restart, matches other generators'/exporters' pattern

    OpenSocket();
    if (socketHandle_ == 0) return;

    uint32_t loopbackNetworkOrder = htonl(INADDR_LOOPBACK);
    uint16_t port = static_cast<uint16_t>(networkPort);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        drones_.clear();

        int count = droneCount;
        if (count < 0) count = 0;
        if (count > 255) count = 255; // uid is a byte (1..255), matches the C# reference's range comment

        for (int i = 1; i <= count; ++i) {
            uint8_t uid = static_cast<uint8_t>(i);
            drones_.emplace(std::piecewise_construct, std::forward_as_tuple(uid),
                             std::forward_as_tuple(uid, socketHandle_, loopbackNetworkOrder, port));
        }

        if (!layoutProvider) layoutProvider = std::make_unique<GridLayout>();
        layoutProvider->LayoutDrones(drones_);

        heartbeatCursor_ = drones_.begin();
        dataCursor_ = drones_.begin();
    }

    stopRequested_ = false;
    networkThread_ = std::thread(&MAVLinkDroneNetworkGenerator::NetworkLoop, this);
}

void MAVLinkDroneNetworkGenerator::Deconstruct() {
    stopRequested_ = true;
    if (networkThread_.joinable()) {
        networkThread_.join();
    }
    CloseSocket();

    std::lock_guard<std::mutex> lock(mutex_);
    drones_.clear();
}

// --- network thread -------------------------------------------------------

void MAVLinkDroneNetworkGenerator::NetworkLoop() {
    auto lastDataBatch = std::chrono::steady_clock::now();
    auto lastHeartbeatBatch = lastDataBatch;

    // Batch sizes/intervals mirror the C# reference's SendData/SendHeartbeat loops
    // (perUpdate=10 drones per tick, paced by delays between batches) - just driven
    // from this one thread instead of two separate coroutines. See the header comment
    // for why that's a safe simplification here.
    while (!stopRequested_) {
        DrainReceive();

        auto now = std::chrono::steady_clock::now();
        if (now - lastDataBatch >= std::chrono::milliseconds(20)) {
            SendDataBatch(10);
            lastDataBatch = now;
        }
        if (now - lastHeartbeatBatch >= std::chrono::milliseconds(100)) {
            SendHeartbeatBatch(10);
            lastHeartbeatBatch = now;
        }
    }
}

void MAVLinkDroneNetworkGenerator::DrainReceive() {
    if (socketHandle_ == 0) return;
    SOCKET sock = static_cast<SOCKET>(socketHandle_);

    uint8_t buffer[300];
    mavlink_status_t status{};

    for (;;) {
        int received = recv(sock, reinterpret_cast<char*>(buffer), static_cast<int>(sizeof(buffer)), 0);
        if (received <= 0) {
            break; // timeout (SO_RCVTIMEO), or the socket was closed out from under us on shutdown
        }

        for (int i = 0; i < received; ++i) {
            mavlink_message_t msg;
            // MAVLINK_COMM_1 (not _0) - channel 0's parse/finalize state is used for
            // OUTGOING sends (see Drone.cpp's Transmit()); keeping receive parsing on
            // a separate channel avoids the two interfering with each other's state.
            if (mavlink_parse_char(MAVLINK_COMM_1, buffer[i], &msg, &status)) {
                HandleMessage(msg);
            }
        }
    }
}

void MAVLinkDroneNetworkGenerator::SendHeartbeatBatch(size_t count) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (drones_.empty()) return;
    for (size_t i = 0; i < count; ++i) {
        if (heartbeatCursor_ == drones_.end()) heartbeatCursor_ = drones_.begin();
        heartbeatCursor_->second.SendHeartbeat();
        ++heartbeatCursor_;
    }
}

void MAVLinkDroneNetworkGenerator::SendDataBatch(size_t count) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (drones_.empty()) return;
    for (size_t i = 0; i < count; ++i) {
        if (dataCursor_ == drones_.end()) dataCursor_ = drones_.begin();
        dataCursor_->second.SendGPSRaw();
        dataCursor_->second.SendGPSFiltered();
        dataCursor_->second.SendSYSStatus();
        ++dataCursor_;
    }
}

void MAVLinkDroneNetworkGenerator::HandleMessage(const mavlink_message_t& msg) {
    switch (msg.msgid) {
        case MAVLINK_MSG_ID_COMMAND_LONG: {
            mavlink_command_long_t cmd;
            mavlink_msg_command_long_decode(&msg, &cmd);

            std::lock_guard<std::mutex> lock(mutex_);
            auto it = drones_.find(cmd.target_system);
            if (it == drones_.end()) break;
            Drone& d = it->second;

            bool handled = false;
            switch (cmd.command) {
                case MAV_CMD_REQUEST_AUTOPILOT_CAPABILITIES:
                    d.SendAutopilotCapabilities();
                    handled = true;
                    break;
                case MAV_CMD_SET_MESSAGE_INTERVAL:
                    // Message intervals aren't tracked - just ACK it, matching the C#
                    // reference.
                    handled = true;
                    break;
                case MAV_CMD_USER_1:
                    // Show control (Skybrush-specific): param1==0 reloads the show
                    // from the buffered FTP upload; ==1 (remove show) / ==2 (test
                    // pyro) are no-ops here too, matching the C# reference (both are
                    // commented out there).
                    if (cmd.param1 == 0.0f) {
                        d.ReloadShow();
                    }
                    handled = true;
                    break;
                default:
                    break;
            }
            if (handled) {
                d.SendCommandACK(cmd.command, MAV_RESULT_ACCEPTED, msg.sysid, msg.compid);
            }
            break;
        }
        case MAVLINK_MSG_ID_COMMAND_INT: {
            mavlink_command_int_t cmd;
            mavlink_msg_command_int_decode(&msg, &cmd);

            std::lock_guard<std::mutex> lock(mutex_);
            auto it = drones_.find(cmd.target_system);
            if (it == drones_.end()) break;
            Drone& d = it->second;

            bool handled = false;
            if (cmd.command == MAV_CMD_USER_2) {
                // Show origin (Skybrush-specific): x/y are lat/lon*1e7, z is alt*1000.
                d.SetShowOrigin(cmd.x * 0.0000001f, cmd.y * 0.0000001f, cmd.z * 0.001f);
                handled = true;
            }
            if (handled) {
                d.SendCommandACK(cmd.command, MAV_RESULT_ACCEPTED, msg.sysid, msg.compid);
            }
            break;
        }
        case MAVLINK_MSG_ID_PARAM_REQUEST_READ: {
            mavlink_param_request_read_t req;
            mavlink_msg_param_request_read_decode(&msg, &req);

            std::lock_guard<std::mutex> lock(mutex_);
            auto it = drones_.find(req.target_system);
            if (it == drones_.end()) break;
            Drone& d = it->second;

            std::string paramName(req.param_id, strnlen(req.param_id, sizeof(req.param_id)));
            float value = 0.0f;
            auto paramIt = d.parameters.find(paramName);
            if (paramIt != d.parameters.end()) value = paramIt->second;

            uint8_t paramIdBytes[16] = {};
            std::memcpy(paramIdBytes, req.param_id, sizeof(paramIdBytes));
            d.SendParamValue(paramName, value, paramIdBytes);
            break;
        }
        case MAVLINK_MSG_ID_PARAM_SET: {
            mavlink_param_set_t set;
            mavlink_msg_param_set_decode(&msg, &set);

            std::lock_guard<std::mutex> lock(mutex_);
            auto it = drones_.find(set.target_system);
            if (it == drones_.end()) break;
            Drone& d = it->second;

            std::string paramName(set.param_id, strnlen(set.param_id, sizeof(set.param_id)));
            d.SetParameter(paramName, set.param_value);

            uint8_t paramIdBytes[16] = {};
            std::memcpy(paramIdBytes, set.param_id, sizeof(paramIdBytes));
            d.SendParamValue(paramName, set.param_value, paramIdBytes);
            break;
        }
        case MAVLINK_MSG_ID_MISSION_COUNT: {
            mavlink_mission_count_t mc;
            mavlink_msg_mission_count_decode(&msg, &mc);

            std::lock_guard<std::mutex> lock(mutex_);
            auto it = drones_.find(mc.target_system);
            if (it == drones_.end()) break;
            Drone& d = it->second;

            // Mission contents (including geofencing) aren't inspected - just ACK,
            // matching the C# reference.
            d.SendMissionACK(MAV_MISSION_ACCEPTED, mc.mission_type, msg.sysid, msg.compid);
            break;
        }
        case MAVLINK_MSG_ID_FILE_TRANSFER_PROTOCOL: {
            mavlink_file_transfer_protocol_t ftp;
            mavlink_msg_file_transfer_protocol_decode(&msg, &ftp);

            std::lock_guard<std::mutex> lock(mutex_);
            auto it = drones_.find(ftp.target_system);
            if (it == drones_.end()) break;
            Drone& d = it->second;

            FtpMessage in;
            std::memcpy(&in, ftp.payload, sizeof(FtpMessage));

            FtpMessage out{};
            out.seqNumber = static_cast<uint16_t>(in.seqNumber + 1);
            out.session = in.session;
            out.reqOpcode = in.opcode;
            out.burstComplete = 1;

            bool sendResponse = false;
            switch (in.opcode) {
                case kFtpCreateFile:
                    // Doesn't inspect the filename - matches the C# reference (this
                    // is a single-file, no-directory-structure write target).
                    d.showFileRaw.clear();
                    out.opcode = kFtpAck;
                    sendResponse = true;
                    break;
                case kFtpWriteFile: {
                    size_t requiredSize = static_cast<size_t>(in.offset) + in.size;
                    if (d.showFileRaw.size() < requiredSize) d.showFileRaw.resize(requiredSize, 0);
                    std::memcpy(d.showFileRaw.data() + in.offset, in.data, in.size);
                    out.opcode = kFtpAck;
                    sendResponse = true;
                    break;
                }
                case kFtpTerminateSession:
                    out.opcode = kFtpAck;
                    sendResponse = true;
                    break;
                case kFtpCalcFileCRC32: {
                    uint32_t crc = d.showFileRaw.empty()
                                       ? Crc32Reflected(nullptr, 0)
                                       : Crc32Reflected(d.showFileRaw.data(), d.showFileRaw.size());
                    out.opcode = kFtpAck;
                    out.size = 4;
                    std::memcpy(out.data, &crc, 4); // little-endian native == the C#'s BitConverter.GetBytes on Windows
                    sendResponse = true;
                    break;
                }
                case kFtpResetSessions:
                    out.opcode = kFtpAck;
                    sendResponse = true;
                    break;
                default:
                    break; // unhandled opcode - matches the C# reference's default case (logged, not ACKed)
            }

            if (sendResponse) {
                uint8_t payload[251];
                std::memcpy(payload, &out, sizeof(FtpMessage));
                d.SendFTPMessage(payload, msg.sysid, msg.compid);
            }
            break;
        }
        case MAVLINK_MSG_ID_LED_CONTROL: {
            mavlink_led_control_t led;
            mavlink_msg_led_control_decode(&msg, &led);

            std::lock_guard<std::mutex> lock(mutex_);
            auto it = drones_.find(led.target_system);
            if (it == drones_.end()) break;
            it->second.SetFlashing();
            break;
        }
        default:
            break; // unhandled message type - matches the C# reference's logged default case
    }
}

// --- DMX -------------------------------------------------------

void MAVLinkDroneNetworkGenerator::GenerateDMX(std::vector<uint8_t>& dmxData) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [uid, drone] : drones_) {
        Vec3 pos = drone.GetDronePosition();

        uint8_t xc, xf, yc, yf, zc, zf;
        PackCoarseFine(InverseLerpClamped(-800.0f, 800.0f, pos.x), xc, xf);
        PackCoarseFine(InverseLerpClamped(-800.0f, 800.0f, pos.y), yc, yf);
        PackCoarseFine(InverseLerpClamped(-800.0f, 800.0f, pos.z), zc, zf);

        std::vector<uint8_t> droneValues = {xc, xf, yc, yf, zc, zf};

        if (pyroFeature) {
            PyroEvent pyro = drone.GetPyroEvent();
            // All 3 angles use -180..180, matching the C# reference's actual code
            // (its comment claims pitch is -90..90, but the code uses the same
            // -180..180 InverseLerp call for pitch/yaw/roll alike - the code is
            // authoritative here, not the stale comment).
            droneValues.push_back(static_cast<uint8_t>(InverseLerpClamped(-180.0f, 180.0f, pyro.pitch) * 255.0f));
            droneValues.push_back(static_cast<uint8_t>(InverseLerpClamped(-180.0f, 180.0f, pyro.yaw) * 255.0f));
            droneValues.push_back(static_cast<uint8_t>(InverseLerpClamped(-180.0f, 180.0f, pyro.roll) * 255.0f));
            droneValues.push_back(static_cast<uint8_t>(pyro.pyroIndex));
        } else {
            RGBA8 color = drone.GetDroneColor();
            droneValues.push_back(color.r);
            droneValues.push_back(color.g);
            droneValues.push_back(color.b);
        }

        size_t position = static_cast<size_t>(channelStart) +
                           (static_cast<size_t>(uid - 1) * droneValues.size());
        WriteDmxAtPosition(dmxData, droneValues, position);
    }
}

// --- UI / persistence -------------------------------------------------------

bool MAVLinkDroneNetworkGenerator::DrawUi() {
    bool changed = false;
    changed |= ImGui::InputInt("Drone Count", &droneCount);
    changed |= ImGui::InputInt("Network Port", &networkPort);
    changed |= ImGui::InputInt("Channel Start", &channelStart);
    changed |= ImGui::Checkbox("Pyro Feature", &pyroFeature);

    static const DroneLayoutProviderRegistry kRegistry; // stateless, safe to share
    std::string current = layoutProvider ? layoutProvider->Name() : std::string();
    if (ImGui::BeginCombo("Layout Provider", current.c_str())) {
        for (const std::string& name : kRegistry.Names()) {
            bool selected = (name == current);
            if (ImGui::Selectable(name.c_str(), selected) && name != current) {
                layoutProvider = kRegistry.Create(name);
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    if (layoutProvider) {
        changed |= layoutProvider->DrawUi();
    }

    if (ImGui::Button("Copy Drone Positions To Clipboard")) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string text;
        for (auto& [uid, drone] : drones_) {
            (void)uid;
            Vec3 pos = drone.GetDronePosition();
            text += "float3(" + std::to_string(pos.x) + ", " + std::to_string(pos.y) + ", " +
                    std::to_string(pos.z) + "),\n";
        }
        ImGui::SetClipboardText(text.c_str());
    }

    return changed;
}

void MAVLinkDroneNetworkGenerator::ReadYaml(const YAML::Node& node) {
    try {
        if (node["droneCount"]) droneCount = node["droneCount"].as<int>();
        if (node["networkPort"]) networkPort = node["networkPort"].as<int>();
        if (node["channelStart"]) channelStart = node["channelStart"].as<int>();
        if (node["pyroFeature"]) pyroFeature = node["pyroFeature"].as<bool>();

        YAML::Node layoutNode = node["layoutProvider"];
        if (layoutNode && layoutNode.IsMap()) {
            // Same tag-resolution approach as ShowConfig.cpp's serializer field -
            // untagged/default-tagged maps fall back to the default (GridLayout).
            const std::string& tag = layoutNode.Tag();
            bool isDefaultTag = tag.empty() || tag == "?" || tag == "!" || tag == "tag:yaml.org,2002:map";

            DroneLayoutProviderRegistry registry;
            std::unique_ptr<IDroneLayoutProvider> resolved =
                isDefaultTag ? registry.Create("GridLayout") : registry.Create(tag.substr(tag.find_last_of('!') + 1));
            if (resolved) {
                resolved->ReadYaml(layoutNode);
                layoutProvider = std::move(resolved);
            }
        }
    } catch (const YAML::Exception&) {
    }
}

void MAVLinkDroneNetworkGenerator::WriteYaml(YAML::Emitter& out) const {
    out << YAML::Key << "droneCount" << YAML::Value << droneCount;
    out << YAML::Key << "networkPort" << YAML::Value << networkPort;
    out << YAML::Key << "channelStart" << YAML::Value << channelStart;
    out << YAML::Key << "pyroFeature" << YAML::Value << pyroFeature;

    out << YAML::Key << "layoutProvider" << YAML::Value;
    out << YAML::LocalTag(layoutProvider ? layoutProvider->Name() : "GridLayout") << YAML::BeginMap;
    if (layoutProvider) layoutProvider->WriteYaml(out);
    out << YAML::EndMap;
}
