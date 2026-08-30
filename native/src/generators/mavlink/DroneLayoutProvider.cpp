#include "DroneLayoutProvider.h"
#include "Drone.h"

#include <cmath>

#include <yaml-cpp/yaml.h>
#include "imgui.h"

// Port of Assets/Plugin/Generators/MAVLinkDrone/LayoutProviders/{GridLayout,
// CircularLayout}.cs.

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg2Rad = kPi / 180.0;

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

// Port of GridLayout.cs's private RotateAbout().
Vec2 RotateAbout(Vec2 pos, Vec2 center, float rotationDegrees) {
    double radians = rotationDegrees * kDeg2Rad;
    double cosA = std::cos(radians);
    double sinA = std::sin(radians);
    Vec2 translated{pos.x - center.x, pos.y - center.y};
    Vec2 rotated{static_cast<float>(translated.x * cosA - translated.y * sinA),
                 static_cast<float>(translated.x * sinA + translated.y * cosA)};
    return Vec2{rotated.x + center.x, rotated.y + center.y};
}
} // namespace

void GridLayout::LayoutDrones(std::map<uint8_t, Drone>& drones) {
    // Faithful-port note: the C# reference uses gridSpacingLat for the X-axis step and
    // gridSpacingLon for the Y-axis step (the opposite of what the field names
    // suggest) - preserved exactly as-is, matching the axis-naming quirk already
    // documented in Drone.h.
    int dronesLeft = static_cast<int>(drones.size());
    while (dronesLeft > 0) {
        for (int j = 0; j < gridLonCount; ++j) {
            auto it = drones.find(static_cast<uint8_t>(dronesLeft));
            if (it == drones.end()) {
                --dronesLeft;
                if (dronesLeft <= 0) break;
                continue;
            }

            Vec2 pos{gridLon + (static_cast<float>(j) * gridSpacingLat),
                     gridLat + (static_cast<float>((dronesLeft - 1) / gridLonCount) * gridSpacingLon)};
            pos = RotateAbout(pos, Vec2{gridLon, gridLat}, rotation);

            it->second.SetPosition(pos.x, pos.y, initialAltitude);

            --dronesLeft;
            if (dronesLeft <= 0) break;
        }
    }
}

bool GridLayout::DrawUi() {
    bool changed = false;
    changed |= ImGui::InputFloat("Grid Longitude (Bottom-Left)", &gridLon, 0.0f, 0.0f, "%.7f");
    changed |= ImGui::InputFloat("Grid Latitude (Bottom-Left)", &gridLat, 0.0f, 0.0f, "%.7f");
    changed |= ImGui::InputInt("Drones Per Row", &gridLonCount);
    changed |= ImGui::InputFloat("Spacing (Longitude axis)", &gridSpacingLon, 0.0f, 0.0f, "%.7f");
    changed |= ImGui::InputFloat("Spacing (Latitude axis)", &gridSpacingLat, 0.0f, 0.0f, "%.7f");
    changed |= ImGui::InputFloat("Initial Altitude (m)", &initialAltitude);
    changed |= ImGui::InputFloat("Rotation (deg)", &rotation);
    return changed;
}

void GridLayout::ReadYaml(const YAML::Node& node) {
    try {
        if (node["gridLon"]) gridLon = node["gridLon"].as<float>();
        if (node["gridLat"]) gridLat = node["gridLat"].as<float>();
        if (node["gridLonCount"]) gridLonCount = node["gridLonCount"].as<int>();
        if (node["gridSpacingLon"]) gridSpacingLon = node["gridSpacingLon"].as<float>();
        if (node["gridSpacingLat"]) gridSpacingLat = node["gridSpacingLat"].as<float>();
        if (node["initialAltitude"]) initialAltitude = node["initialAltitude"].as<float>();
        if (node["rotation"]) rotation = node["rotation"].as<float>();
    } catch (const YAML::Exception&) {
    }
}

void GridLayout::WriteYaml(YAML::Emitter& out) const {
    out << YAML::Key << "gridLon" << YAML::Value << gridLon;
    out << YAML::Key << "gridLat" << YAML::Value << gridLat;
    out << YAML::Key << "gridLonCount" << YAML::Value << gridLonCount;
    out << YAML::Key << "gridSpacingLon" << YAML::Value << gridSpacingLon;
    out << YAML::Key << "gridSpacingLat" << YAML::Value << gridSpacingLat;
    out << YAML::Key << "initialAltitude" << YAML::Value << initialAltitude;
    out << YAML::Key << "rotation" << YAML::Value << rotation;
}

void CircularLayout::LayoutDrones(std::map<uint8_t, Drone>& drones) {
    if (drones.empty()) return;
    float angleStep = 360.0f / static_cast<float>(drones.size());

    // Matches the C# reference's `drones[(byte)(j + 1)]` indexing (drone uids are
    // always a contiguous 1..droneCount range from Construct(), so this is equivalent
    // to iterating uids 1..drones.size() directly rather than map iteration order).
    for (size_t i = 0; i < drones.size(); ++i) {
        auto it = drones.find(static_cast<uint8_t>(i + 1));
        if (it == drones.end()) continue;

        float angle = angleStep * static_cast<float>(i);
        angle += rotationOffset * angleStep;
        double angleRad = angle * kDeg2Rad;
        Vec2 dir{static_cast<float>(std::cos(angleRad)), static_cast<float>(std::sin(angleRad))};
        Vec2 pos{dir.x * radius, dir.y * radius};

        it->second.SetPosition(lon + pos.x, lat + pos.y, initialAltitude);
    }
}

bool CircularLayout::DrawUi() {
    bool changed = false;
    changed |= ImGui::InputFloat("Center Longitude", &lon, 0.0f, 0.0f, "%.7f");
    changed |= ImGui::InputFloat("Center Latitude", &lat, 0.0f, 0.0f, "%.7f");
    changed |= ImGui::InputFloat("Radius", &radius, 0.0f, 0.0f, "%.7f");
    changed |= ImGui::InputFloat("Initial Altitude (m)", &initialAltitude);
    changed |= ImGui::InputFloat("Rotation Offset (0-1)", &rotationOffset);
    return changed;
}

void CircularLayout::ReadYaml(const YAML::Node& node) {
    try {
        if (node["lon"]) lon = node["lon"].as<float>();
        if (node["lat"]) lat = node["lat"].as<float>();
        if (node["radius"]) radius = node["radius"].as<float>();
        if (node["initialAltitude"]) initialAltitude = node["initialAltitude"].as<float>();
        if (node["rotationOffset"]) rotationOffset = node["rotationOffset"].as<float>();
    } catch (const YAML::Exception&) {
    }
}

void CircularLayout::WriteYaml(YAML::Emitter& out) const {
    out << YAML::Key << "lon" << YAML::Value << lon;
    out << YAML::Key << "lat" << YAML::Value << lat;
    out << YAML::Key << "radius" << YAML::Value << radius;
    out << YAML::Key << "initialAltitude" << YAML::Value << initialAltitude;
    out << YAML::Key << "rotationOffset" << YAML::Value << rotationOffset;
}

std::vector<std::string> DroneLayoutProviderRegistry::Names() const {
    return {"GridLayout", "CircularLayout"};
}

std::unique_ptr<IDroneLayoutProvider> DroneLayoutProviderRegistry::Create(const std::string& name) const {
    if (name == "GridLayout") return std::make_unique<GridLayout>();
    if (name == "CircularLayout") return std::make_unique<CircularLayout>();
    return nullptr;
}
