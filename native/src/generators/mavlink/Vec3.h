#pragma once
// Minimal float 3-vector, standing in for UnityEngine.Vector3 in the MAVLinkDrone
// generator port (the only generator that needs 3D vector math).
struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};
