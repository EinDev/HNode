#include "Drone.h"

#include <winsock2.h>
#include <chrono>
#include <cmath>
#include <cstring>

#include "mavlink_all.h"

// Port of Assets/Plugin/Generators/MAVLinkDrone/Drone.cs.
//
// Named-parameter note: the C# reference builds each outgoing message via a
// positional-argument struct constructor from the prebuilt MAVLink.dll (not checked
// into this repo - see the vendoring readme's sibling investigation). This file
// instead uses the vendored C library's per-message mavlink_msg_X_pack() functions,
// which take NAMED parameters - so each value below is mapped to the wire field it
// actually means (per the standard MAVLink common/standard.xml message definitions),
// not transcribed by argument position. This is both safer (position-order mistakes
// with same-typed fields are easy to make silently) and unblocks messages where the
// C# constructor's exact parameter order can't be verified without MAVLink.dll's
// source. Where the C# reference's intent for a given field is genuinely ambiguous
// (e.g. HEARTBEAT's custom_mode - see SendHeartbeat), a reasonable equivalent value is
// used and called out in a comment; this only affects cosmetic/unused telemetry
// fields, never the framing/CRC (which the vendored library computes correctly
// regardless).

namespace {
constexpr double kFlashDurationMs = 2000.0;
// MSVC's <cmath> only defines M_PI when _USE_MATH_DEFINES is set before the include -
// simpler to just define this locally.
constexpr double kPi = 3.14159265358979323846;
}

Drone::Drone(uint8_t uidIn, std::uintptr_t socketHandle, uint32_t remoteAddrNetworkOrder, uint16_t remotePortHostOrder)
    : uid(uidIn),
      socketHandle_(socketHandle),
      remoteAddrNetworkOrder_(remoteAddrNetworkOrder),
      remotePortHostOrder_(remotePortHostOrder) {
}

Drone::~Drone() = default;

// --- position -------------------------------------------------------------

namespace {
// Port of Drone.cs's private measure() - haversine distance in meters.
double Measure(float lat1, float lon1, float lat2, float lon2) {
    const double R = 6378.137; // Earth radius, km
    double dLat = lat2 * kPi / 180.0 - lat1 * kPi / 180.0;
    double dLon = lon2 * kPi / 180.0 - lon1 * kPi / 180.0;
    double a = std::sin(dLat / 2) * std::sin(dLat / 2) +
               std::cos(lat1 * kPi / 180.0) * std::cos(lat2 * kPi / 180.0) * std::sin(dLon / 2) * std::sin(dLon / 2);
    double c = 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));
    return R * c * 1000.0; // meters
}
} // namespace

void Drone::SetPosition(float lat, float lon, float alt) {
    // If any value is exactly 0, nudge it to a tiny epsilon instead - matches the C#
    // reference (avoids some degenerate case in the haversine math at exactly 0,0).
    const float epsilon = 0.0000001f;
    if (lat == 0.0f) lat = epsilon;
    if (lon == 0.0f) lon = epsilon;
    if (alt == 0.0f) alt = epsilon;
    position_ = Vec3{lat, lon, alt};
}

void Drone::SetShowOrigin(float lat, float lon, float alt) {
    showOrigin_ = Vec3{lat, lon, alt};
}

Vec3 Drone::GetDronePosition() const {
    // Phase 2 (show-file trajectory playback) isn't implemented yet - showFile_ stays
    // null, so this always falls through to the GPS-derived position below.
    if (showFile_ != nullptr) {
        // Placeholder until ShowFile lands - unreachable while showFile_ is always null.
        return Vec3{};
    }

    // Port of Drone.cs's private `Position` getter - see the header's faithful-port
    // note for why the x/y axis usage here doesn't cleanly correspond to true
    // geographic lat/lon despite the parameter names.
    double x = Measure(showOrigin_.x, position_.y, showOrigin_.x, showOrigin_.y);
    double y = Measure(position_.x, showOrigin_.y, showOrigin_.x, showOrigin_.y);
    if (showOrigin_.y - position_.y > 0) x = -x;
    if (showOrigin_.x - position_.x > 0) y = -y;
    return Vec3{static_cast<float>(x), static_cast<float>(y), position_.z};
}

Vec3 Drone::XYZtoLatLonAlt(Vec3 xyz) const {
    const double R = 6378.137; // km
    double xKm = xyz.x / 1000.0;
    double yKm = xyz.y / 1000.0;
    double newLongitude =
        showOrigin_.y + (yKm / R) * (180.0 / kPi) / std::cos(showOrigin_.y * kPi / 180.0);
    double newLatitude = showOrigin_.x + (xKm / R) * (180.0 / kPi);
    return Vec3{static_cast<float>(newLongitude), static_cast<float>(newLatitude), xyz.z};
}

void Drone::SetFlashing() {
    auto now = std::chrono::duration<double, std::milli>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();
    flashStartMs_ = std::fmod(now, 86400000.0); // time-of-day ms, matches DateTime.Now.TimeOfDay
    flashEndMs_ = flashStartMs_ + kFlashDurationMs;
}

RGBA8 Drone::GetDroneColor() const {
    auto now = std::chrono::duration<double, std::milli>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();
    double nowOfDay = std::fmod(now, 86400000.0);

    if (nowOfDay < flashEndMs_ &&
        std::fmod(nowOfDay - flashStartMs_, kFlashIntervalMs) < (kFlashIntervalMs / 2.0)) {
        return RGBA8{255, 255, 255, 255};
    }

    // Phase 2 - show-file color playback not implemented yet (showFile_ always null).
    return RGBA8{0, 0, 0, 255};
}

PyroEvent Drone::GetPyroEvent() const {
    // Phase 3 - show-file pyro playback not implemented yet (showFile_ always null).
    return PyroEvent{};
}

// --- parameters / show file -------------------------------------------------

void Drone::SetParameter(const std::string& name, float value) {
    parameters[name] = value;
    // Phase 2's SHOW_START_TIME handling (resetting show program pointers) will hook
    // in here once ShowFile exists.
}

void Drone::ReloadShow() {
    // Phase 2 - will construct a ShowFile from showFileRaw here. For now, just clear
    // the accumulated buffer (matches the C# reference's behavior after constructing
    // its ShowFile - the raw bytes aren't needed again once parsed).
    showFileRaw.clear();
}

// --- MAVLink sends -------------------------------------------------------

void Drone::Transmit(const mavlink_message_t& msg) const {
    uint8_t buffer[300]; // MAVLINK_MAX_PACKET_LEN
    uint16_t len = mavlink_msg_to_send_buffer(buffer, &msg);

    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_addr.s_addr = remoteAddrNetworkOrder_;
    dest.sin_port = htons(remotePortHostOrder_);

    sendto(static_cast<SOCKET>(socketHandle_), reinterpret_cast<const char*>(buffer), len, 0,
           reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
}

void Drone::SendHeartbeat() const {
    mavlink_message_t msg;
    // custom_mode: the C# reference passes a literal `127` as this message's first
    // constructor argument, which - given MAVLink.dll isn't available to inspect - is
    // ambiguous between custom_mode and other same-message fields. Using 0 here
    // instead: custom_mode is an autopilot-specific field with no meaning this app's
    // Skybrush-server counterpart is documented to depend on, unlike type/autopilot/
    // base_mode/system_status below (all faithfully reproduced).
    mavlink_msg_heartbeat_pack(uid, 1, &msg, MAV_TYPE_GENERIC, MAV_AUTOPILOT_ARDUPILOTMEGA,
                                MAV_MODE_FLAG_AUTO_ENABLED, 0, MAV_STATE_STANDBY);
    Transmit(msg);
}

void Drone::SendAutopilotCapabilities() const {
    // DRONE_SHOW_MODE isn't part of the official MAVLink capability bitmask - matches
    // the C# reference's own comment that this is a Skybrush-specific extension bit.
    constexpr uint64_t kDroneShowMode = 0x4000000ULL;
    uint64_t capabilities = static_cast<uint64_t>(MAV_PROTOCOL_CAPABILITY_PARAM_FLOAT) |
                             static_cast<uint64_t>(MAV_PROTOCOL_CAPABILITY_FTP) |
                             static_cast<uint64_t>(MAV_PROTOCOL_CAPABILITY_SET_POSITION_TARGET_GLOBAL_INT) |
                             static_cast<uint64_t>(MAV_PROTOCOL_CAPABILITY_SET_POSITION_TARGET_LOCAL_NED) |
                             static_cast<uint64_t>(MAV_PROTOCOL_CAPABILITY_MAVLINK2) | kDroneShowMode;

    uint8_t flightCustomVersion[8] = {};
    uint8_t middlewareCustomVersion[8] = {};
    uint8_t osCustomVersion[8] = {};
    uint8_t uid2[18] = {};
    uid2[0] = uid;

    mavlink_message_t msg;
    mavlink_msg_autopilot_version_pack(uid, 1, &msg, capabilities,
                                        static_cast<uint32_t>(FIRMWARE_VERSION_TYPE_BETA),
                                        static_cast<uint32_t>(FIRMWARE_VERSION_TYPE_BETA),
                                        static_cast<uint32_t>(FIRMWARE_VERSION_TYPE_BETA),
                                        /*board_version=*/0, flightCustomVersion, middlewareCustomVersion,
                                        osCustomVersion, /*vendor_id=*/0, /*product_id=*/0,
                                        /*uid=*/uid, uid2);
    Transmit(msg);
}

void Drone::SendCommandACK(uint16_t command, uint8_t result, uint8_t targetSystem, uint8_t targetComponent) const {
    mavlink_message_t msg;
    mavlink_msg_command_ack_pack(uid, 1, &msg, command, result, /*progress=*/255, /*result_param2=*/0,
                                  targetSystem, targetComponent);
    Transmit(msg);
}

void Drone::SendMissionACK(uint8_t result, uint8_t missionType, uint8_t targetSystem, uint8_t targetComponent) const {
    mavlink_message_t msg;
    mavlink_msg_mission_ack_pack(uid, 1, &msg, targetSystem, targetComponent, result, missionType, /*opaque_id=*/0);
    Transmit(msg);
}

void Drone::SendParamValue(const std::string& paramName, float value, const uint8_t paramIdBytes[16]) const {
    mavlink_message_t msg;
    char paramId[16];
    std::memcpy(paramId, paramIdBytes, 16);
    mavlink_msg_param_value_pack(uid, 1, &msg, paramId, value, MAV_PARAM_TYPE_REAL32,
                                  /*param_count=*/0, /*param_index=*/0);
    (void)paramName; // kept for signature symmetry with the C# reference; not itself sent
    Transmit(msg);
}

void Drone::SendFTPMessage(const uint8_t ftpPayload[251], uint8_t targetSystem, uint8_t targetComponent) const {
    mavlink_message_t msg;
    mavlink_msg_file_transfer_protocol_pack(uid, 1, &msg, /*target_network=*/0, targetSystem, targetComponent,
                                             ftpPayload);
    Transmit(msg);
}

void Drone::SendGPSRaw() const {
    Vec3 pos = GetDronePosition();
    Vec3 latLonAlt = XYZtoLatLonAlt(pos);

    mavlink_message_t msg;
    mavlink_msg_gps_raw_int_pack(
        uid, 1, &msg,
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                   std::chrono::system_clock::now().time_since_epoch())
                                   .count()),
        GPS_FIX_TYPE_3D_FIX, static_cast<int32_t>(latLonAlt.x * 10000000), static_cast<int32_t>(latLonAlt.y * 10000000),
        static_cast<int32_t>(latLonAlt.z * 1000), 65535, 65535, 65535, 65535, /*satellites_visible=*/4,
        /*alt_ellipsoid=*/0, /*h_acc=*/0, /*v_acc=*/0, /*vel_acc=*/0, /*hdg_acc=*/0, /*yaw=*/0);
    Transmit(msg);
}

void Drone::SendGPSFiltered() const {
    Vec3 pos = GetDronePosition();
    Vec3 latLonAlt = XYZtoLatLonAlt(pos);

    mavlink_message_t msg;
    mavlink_msg_global_position_int_pack(
        uid, 1, &msg,
        static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                   std::chrono::system_clock::now().time_since_epoch())
                                   .count()),
        static_cast<int32_t>(latLonAlt.x * 10000000), static_cast<int32_t>(latLonAlt.y * 10000000),
        static_cast<int32_t>(latLonAlt.z * 1000), /*relative_alt=*/0, /*vx=*/0, /*vy=*/0, /*vz=*/0, /*hdg=*/0);
    Transmit(msg);
}

void Drone::SendSYSStatus() const {
    uint32_t flags = MAV_SYS_STATUS_SENSOR_GPS | MAV_SYS_STATUS_SENSOR_MOTOR_OUTPUTS |
                      MAV_SYS_STATUS_SENSOR_RC_RECEIVER | MAV_SYS_STATUS_SENSOR_BATTERY |
                      MAV_SYS_STATUS_SENSOR_SATCOM | MAV_SYS_STATUS_SENSOR_PROPULSION;

    mavlink_message_t msg;
    mavlink_msg_sys_status_pack(uid, 1, &msg, flags, flags, flags, /*load=*/50, /*voltage_battery=*/12000,
                                 /*current_battery=*/-1, /*battery_remaining=*/0, /*drop_rate_comm=*/0,
                                 /*errors_comm=*/0, /*errors_count1=*/0, /*errors_count2=*/0,
                                 /*errors_count3=*/0, /*errors_count4=*/0,
                                 /*onboard_control_sensors_present_extended=*/0,
                                 /*onboard_control_sensors_enabled_extended=*/0,
                                 /*onboard_control_sensors_health_extended=*/0);
    Transmit(msg);
}
