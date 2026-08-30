#pragma once
// Port of Assets/Plugin/Generators/MAVLinkDrone/BlockData/PyroEvent.cs's plain-data
// fields - a single pyro-firing event's parameters, as returned by a drone's active
// show (or a zero-initialized default when there's no show file/pyro event active).
// The decoding logic (varint parsing of an EVENT_LIST block) lives in ShowFile.cpp,
// not here, since it's only ever produced while parsing that one block type.
//
// Split out of Drone.h (where it used to live inline) so ShowFile.h can use it too
// without a Drone.h <-> ShowFile.h circular include.
class PyroEvent {
public:
    double startMs = 0.0;
    double durationMs = 0.0;
    double endMs = 0.0;
    int pyroIndex = 0;
    float pitch = 0.0f;
    float yaw = 0.0f;
    float roll = 0.0f;

    bool InsideEvent(double timeMs) const { return timeMs >= startMs && timeMs < endMs; }
};
