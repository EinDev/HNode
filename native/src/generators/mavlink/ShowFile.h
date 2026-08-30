#pragma once
// Port of Assets/Plugin/Generators/MAVLinkDrone/ShowFile.cs - parses a drone's
// uploaded show-file (received via MAVLink FTP WriteFile calls, see
// Drone::ReloadShow()) into trajectory/light-program/pyro-event timelines, and scrubs
// them by wall-clock time relative to `showStartTime` (set via the SHOW_START_TIME
// MAVLink parameter - see Drone::SetParameter()).
#include <chrono>
#include <cstdint>
#include <vector>

#include "../../render/PixelOps.h"
#include "ByteReader.h"
#include "LightEvent.h"
#include "PyroEvent.h"
#include "Trajectory.h"
#include "Vec3.h"

class ShowFile {
public:
    explicit ShowFile(const std::vector<uint8_t>& rawData);

    Vec3 GetPositionAtRealTime(std::chrono::system_clock::time_point now);
    RGBA8 GetColorAtRealTime(std::chrono::system_clock::time_point now);
    PyroEvent GetPyroAtRealTime(std::chrono::system_clock::time_point now);

    // Public/settable, matching ShowFile.cs's public showStartTime/LightProgramPointer/
    // TrajectoryProgramPointer fields - Drone::SetParameter() resets the two pointers
    // when SHOW_START_TIME changes.
    std::chrono::system_clock::time_point showStartTime;
    int lightProgramPointer = 0;
    int trajectoryProgramPointer = 0;

private:
    void ExtractBlock(ByteReader& reader);

    Vec3 GetPositionAtTime(double timeMs);
    RGBA8 GetColorAtTime(double timeMs);
    PyroEvent GetPyroAtTime(double timeMs);

    std::vector<LightEvent> lightProgram_;
    std::vector<Trajectory> trajectoryProgram_;
    std::vector<PyroEvent> pyroProgram_;

    static constexpr int kPointerLookahead = 5;
};
