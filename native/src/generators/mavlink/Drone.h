#pragma once
// Port of Assets/Plugin/Generators/MAVLinkDrone/Drone.cs - one simulated drone's
// state (position, LED color, pyro, MAVLink parameters, show-file playback) plus the
// MAVLink messages it sends. Owned by MAVLinkDroneNetworkGenerator, which supplies the
// shared UDP socket every drone sends through (matches the C# reference: one
// UdpClient shared by every Drone instance).
//
// Faithful-port note: the C# reference's `Position` getter, `measure()`, and
// `XYZtoLatLonAlt()` have an apparent latitude/longitude axis inconsistency (compare
// `SetPosition(lat, lon, alt)`'s parameter names against how GridLayout/CircularLayout
// actually call it, and against which axis ends up in the MAVLink GPS_RAW_INT
// message's `lat` vs `lon` field). This is preserved exactly as-is rather than
// "corrected" - it's internally self-consistent (every caller uses the same x/y
// convention), and since this app's counterpart is a real Skybrush-server-driven show
// system, changing the axis mapping here could silently break interop with however
// that system already compensates (if it does). See Drone.cpp for the exact
// positional transcription from the C# source.
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "Vec3.h"
#include "../../render/PixelOps.h"
#include "mavlink_all.h"

class ShowFile; // Phase 2 - forward-declared, showFile_ stays null until implemented

// Port of Assets/Plugin/Generators/MAVLinkDrone/BlockData/PyroEvent.cs - a single
// pyro-firing event's parameters, as returned by a drone's active show (or a
// zero-initialized default when there's no show file/pyro event active).
struct PyroEvent {
    double startMs = 0.0;
    double durationMs = 0.0;
    int pyroIndex = 0;
    float pitch = 0.0f;
    float yaw = 0.0f;
    float roll = 0.0f;
};

class Drone {
public:
    Drone(uint8_t uid, std::uintptr_t socketHandle, uint32_t remoteAddrNetworkOrder, uint16_t remotePortHostOrder);
    ~Drone();

    Drone(const Drone&) = delete;
    Drone& operator=(const Drone&) = delete;
    Drone(Drone&&) = default;
    Drone& operator=(Drone&&) = default;

    uint8_t uid;

    // --- position / layout -------------------------------------------------
    // SetPosition/SetShowOrigin's parameter names mirror the C# reference's (lat, lon,
    // alt) even though, per the header's faithful-port note, the actual axis usage
    // throughout this file doesn't cleanly correspond to true geographic lat/lon.
    void SetPosition(float lat, float lon, float alt);
    void SetShowOrigin(float lat, float lon, float alt);
    Vec3 GetDronePosition() const; // show-file trajectory position if loaded, else GPS-derived Position
    Vec3 XYZtoLatLonAlt(Vec3 xyz) const;

    void SetFlashing();
    RGBA8 GetDroneColor() const;
    PyroEvent GetPyroEvent() const;

    // --- parameters / show file ---------------------------------------------
    std::map<std::string, float> parameters;
    void SetParameter(const std::string& name, float value);

    std::vector<uint8_t> showFileRaw; // accumulated via FTP WriteFile, consumed by ReloadShow()
    void ReloadShow();

    // --- MAVLink sends -------------------------------------------------------
    // Serializes and sends an already-packed message through the shared socket -
    // equivalent to Drone.cs's generic Transmit<T>(), but taking an already-packed
    // mavlink_message_t instead of a raw struct + msgid, since the vendored C library
    // (unlike the C# reference's MAVLink.dll) builds the message via per-type
    // mavlink_msg_X_pack() functions rather than generic struct serialization.
    void Transmit(const mavlink_message_t& msg) const;

    void SendHeartbeat() const;
    void SendGPSRaw() const;
    void SendGPSFiltered() const;
    void SendSYSStatus() const;
    void SendAutopilotCapabilities() const;
    void SendCommandACK(uint16_t command, uint8_t result, uint8_t targetSystem, uint8_t targetComponent) const;
    void SendMissionACK(uint8_t result, uint8_t missionType, uint8_t targetSystem, uint8_t targetComponent) const;
    void SendParamValue(const std::string& paramName, float value, const uint8_t paramIdBytes[16]) const;
    void SendFTPMessage(const uint8_t ftpPayload[251], uint8_t targetSystem, uint8_t targetComponent) const;

private:
    Vec3 position_;   // C#'s `latlongaltposition`
    Vec3 showOrigin_;
    double flashStartMs_ = 0.0;
    double flashEndMs_ = 0.0;
    static constexpr double kFlashIntervalMs = 500.0;

    std::uintptr_t socketHandle_;      // SOCKET, type-erased to keep <winsock2.h> out of this header
    uint32_t remoteAddrNetworkOrder_;  // already in network byte order
    uint16_t remotePortHostOrder_;

    ShowFile* showFile_ = nullptr; // Phase 2 - owned, null until a show is loaded
};
