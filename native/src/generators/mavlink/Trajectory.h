#pragma once
// Port of Assets/Plugin/Generators/MAVLinkDrone/BlockData/Trajectory.cs - one segment
// of a Bitcraze/Skybrush "compressed trajectory":
// https://www.bitcraze.io/documentation/repository/crazyflie-firmware/master/functional-areas/trajectory_formats/#compressed-representation
#include <cstdint>
#include <vector>

#include "ByteReader.h"
#include "Vec3.h"

class Trajectory {
public:
    enum class BezierOrder : uint8_t {
        Constant = 0,
        StraightLine = 1,
        Cubic = 2,
        SeventhDegree = 3,
    };

    // Blank/default trajectory (1 second, constant at the origin) - matches
    // Trajectory.cs's parameterless constructor, used for an empty TRAJECTORY block.
    Trajectory();

    // Decodes one segment from `reader`, continuing from `startPos`/`startYaw` with
    // the block-wide `scale` factor. Matches Trajectory.cs's Queue<byte>-consuming
    // constructor.
    Trajectory(ByteReader& reader, Vec3 startPos, float startYaw, uint8_t scale);

    bool InsideEvent(double timeMs) const { return timeMs >= startTimeMs && timeMs < endTimeMs; }

    // Evaluates the Bezier curve at t in [0,1] and returns the position with the
    // reference's "blender coord system" axis swap: (-y, x, z).
    Vec3 Evaluate(float t) const;

    // Public, externally-mutated by ShowFile's cumulative start/end-time pass after
    // all segments in a block are decoded - matches Trajectory.cs's public settable
    // startTime/endTime/duration fields.
    Vec3 lastPosition;
    float lastYaw = 0.0f;
    double durationMs = 0.0;
    double startTimeMs = 0.0;
    double endTimeMs = 0.0;

    static float DecodeSpatialCoordinate(ByteReader& reader, uint8_t scale);
    static float DecodeAngleCoordinate(ByteReader& reader);
    static Vec3 DecodeStartSpatialCoordinates(ByteReader& reader, uint8_t scale);

private:
    std::vector<float> xControlPoints_;
    std::vector<float> yControlPoints_;
    std::vector<float> zControlPoints_;
    std::vector<float> yawControlPoints_;
    BezierOrder xOrder_ = BezierOrder::Constant;
    BezierOrder yOrder_ = BezierOrder::Constant;
    BezierOrder zOrder_ = BezierOrder::Constant;
    BezierOrder yawOrder_ = BezierOrder::Constant;

    static BezierOrder DecodeAxisOrder(uint8_t data, int shift);
    static std::vector<float> DecodeAxisControlPoints(ByteReader& reader, uint8_t scale, BezierOrder order);
    static float BezierEvaluate(const std::vector<float>& controlPoints, float t);
    static float BezierCubicEvaluate(const std::vector<float>& controlPoints, float t);
    static float BezierSeventhDegreeEvaluate(const std::vector<float>& controlPoints, float t);
};
