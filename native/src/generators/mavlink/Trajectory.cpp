#include "Trajectory.h"

#include <cmath>

// Port of Assets/Plugin/Generators/MAVLinkDrone/BlockData/Trajectory.cs.

Trajectory::Trajectory() {
    startTimeMs = 0.0;
    durationMs = 1000.0; // matches TimeSpan.FromSeconds(1)
    endTimeMs = startTimeMs + durationMs;
    xControlPoints_ = {0.0f};
    yControlPoints_ = {0.0f};
    zControlPoints_ = {0.0f};
    yawControlPoints_ = {0.0f};
    lastPosition = Vec3{0.0f, 0.0f, 0.0f};
    lastYaw = 0.0f;
}

Trajectory::Trajectory(ByteReader& reader, Vec3 startPos, float startYaw, uint8_t scale) {
    xControlPoints_.push_back(startPos.x);
    yControlPoints_.push_back(startPos.y);
    zControlPoints_.push_back(startPos.z);
    yawControlPoints_.push_back(startYaw);

    // Axis order byte: 2 bits per axis (X/Y/Z/YAW), matching Trajectory.cs's Axis enum
    // shift amounts (X=0, Y=2, Z=4, YAW=6).
    uint8_t axisByte = reader.ReadByte();
    xOrder_ = DecodeAxisOrder(axisByte, 0);
    yOrder_ = DecodeAxisOrder(axisByte, 2);
    zOrder_ = DecodeAxisOrder(axisByte, 4);
    yawOrder_ = DecodeAxisOrder(axisByte, 6);

    // Duration is read as UInt16 (not Int16) - matches Trajectory.cs's
    // DecodeDuration() comment ("cursed, was getting NEGATIVE durations somehow????").
    durationMs = static_cast<double>(reader.ReadUInt16LE());

    // Control points are stored in this order: X Y Z YAW.
    std::vector<float> xPts = DecodeAxisControlPoints(reader, scale, xOrder_);
    std::vector<float> yPts = DecodeAxisControlPoints(reader, scale, yOrder_);
    std::vector<float> zPts = DecodeAxisControlPoints(reader, scale, zOrder_);
    std::vector<float> yawPts = DecodeAxisControlPoints(reader, scale, yawOrder_);

    xControlPoints_.insert(xControlPoints_.end(), xPts.begin(), xPts.end());
    yControlPoints_.insert(yControlPoints_.end(), yPts.begin(), yPts.end());
    zControlPoints_.insert(zControlPoints_.end(), zPts.begin(), zPts.end());
    yawControlPoints_.insert(yawControlPoints_.end(), yawPts.begin(), yawPts.end());

    lastPosition = Vec3{xControlPoints_.back(), yControlPoints_.back(), zControlPoints_.back()};
    lastYaw = yawControlPoints_.back();
}

Vec3 Trajectory::Evaluate(float t) const {
    float x = BezierEvaluate(xControlPoints_, t);
    float y = BezierEvaluate(yControlPoints_, t);
    float z = BezierEvaluate(zControlPoints_, t);
    return Vec3{-y, x, z}; // "blender coord system go brrrr" - matches Trajectory.cs's evaluate() exactly
}

float Trajectory::BezierEvaluate(const std::vector<float>& controlPoints, float t) {
    switch (controlPoints.size()) {
        case 1:
            return controlPoints[0]; // constant
        case 2: {
            // Unity's Mathf.Lerp (used here) clamps t to [0,1], unlike the Pow-based
            // formulas below (Mathf.Pow never clamps) - faithfully preserving that
            // asymmetry, not applying a uniform clamp everywhere.
            float tc = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
            return controlPoints[0] + (controlPoints[1] - controlPoints[0]) * tc;
        }
        case 4:
            return BezierCubicEvaluate(controlPoints, t);
        case 8:
            return BezierSeventhDegreeEvaluate(controlPoints, t);
        default:
            // C# throws ArgumentException here. Untrusted network-uploaded show data
            // shouldn't be able to crash the app - fall back to the first control
            // point (or 0 if somehow empty) instead. See ByteReader.h's header
            // comment for the same crash-avoidance rationale applied elsewhere in
            // this show-file parser.
            return controlPoints.empty() ? 0.0f : controlPoints[0];
    }
}

float Trajectory::BezierCubicEvaluate(const std::vector<float>& p, float t) {
    return std::pow(1 - t, 3) * p[0] + 3 * std::pow(1 - t, 2) * t * p[1] + 3 * (1 - t) * std::pow(t, 2) * p[2] +
           std::pow(t, 3) * p[3];
}

float Trajectory::BezierSeventhDegreeEvaluate(const std::vector<float>& p, float t) {
    return std::pow(1 - t, 7) * p[0] + 7 * std::pow(1 - t, 6) * t * p[1] +
           21 * std::pow(1 - t, 5) * std::pow(t, 2) * p[2] + 35 * std::pow(1 - t, 4) * std::pow(t, 3) * p[3] +
           35 * std::pow(1 - t, 3) * std::pow(t, 4) * p[4] + 21 * std::pow(1 - t, 2) * std::pow(t, 5) * p[5] +
           7 * (1 - t) * std::pow(t, 6) * p[6] + std::pow(t, 7) * p[7];
}

Trajectory::BezierOrder Trajectory::DecodeAxisOrder(uint8_t data, int shift) {
    uint8_t shifted = static_cast<uint8_t>(data >> shift) & 0x03;
    return static_cast<BezierOrder>(shifted);
}

std::vector<float> Trajectory::DecodeAxisControlPoints(ByteReader& reader, uint8_t scale, BezierOrder order) {
    int pointCount = 0;
    switch (order) {
        case BezierOrder::Constant:
            pointCount = 0;
            break;
        case BezierOrder::StraightLine:
            pointCount = 1;
            break;
        case BezierOrder::Cubic:
            pointCount = 3;
            break;
        case BezierOrder::SeventhDegree:
            pointCount = 7;
            break;
    }

    std::vector<float> points;
    for (int i = 0; i < pointCount; ++i) {
        points.push_back(DecodeSpatialCoordinate(reader, scale));
    }
    return points;
}

Vec3 Trajectory::DecodeStartSpatialCoordinates(ByteReader& reader, uint8_t scale) {
    float x = DecodeSpatialCoordinate(reader, scale);
    float y = DecodeSpatialCoordinate(reader, scale);
    float z = DecodeSpatialCoordinate(reader, scale);
    return Vec3{x, y, z};
}

float Trajectory::DecodeSpatialCoordinate(ByteReader& reader, uint8_t scale) {
    // Coordinates are signed-16-bit millimeters * scale, converted to meters.
    return static_cast<float>(reader.ReadInt16LE()) * static_cast<float>(scale) / 1000.0f;
}

float Trajectory::DecodeAngleCoordinate(ByteReader& reader) {
    // Angles (yaw) are signed-16-bit tenths of a degree.
    return static_cast<float>(reader.ReadInt16LE()) / 10.0f;
}
