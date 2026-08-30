#pragma once
// Port of Assets/Plugin/Generators/MAVLinkDrone/LayoutProviders/{IDroneLayoutProvider,
// GridLayout,CircularLayout}.cs - lays out a freshly-constructed drone map's initial
// GPS positions. This is a small polymorphic YAML sub-object on
// MAVLinkDroneNetworkGenerator's `layoutProvider` field (tagged `!GridLayout` /
// `!CircularLayout`, same TagMappedAttribute mechanism the C# reference uses for
// serializers/generators/exporters) - unlike those, it's a single nested field, not a
// top-level dynamic list, so it gets its own tiny registry here rather than reusing
// GeneratorRegistry's shape.
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace YAML {
class Emitter;
class Node;
} // namespace YAML

class Drone;

class IDroneLayoutProvider {
public:
    virtual ~IDroneLayoutProvider() = default;

    // Stable name - both this provider's YAML tag (e.g. "!GridLayout") and its label
    // in the UI dropdown - must match the reference C# class name for .shwcfg
    // compatibility.
    virtual const char* Name() const = 0;

    virtual void LayoutDrones(std::map<uint8_t, Drone>& drones) = 0;

    virtual bool DrawUi() { return false; }
    virtual void ReadYaml(const YAML::Node& node) { (void)node; }
    virtual void WriteYaml(YAML::Emitter& out) const { (void)out; }
};

class GridLayout : public IDroneLayoutProvider {
public:
    const char* Name() const override { return "GridLayout"; }

    float gridLon = 0.0f;
    float gridLat = 0.0f;
    int gridLonCount = 1;
    float gridSpacingLon = 0.0001f;
    float gridSpacingLat = 0.0001f;
    float initialAltitude = 0.0f;
    float rotation = 0.0f;

    void LayoutDrones(std::map<uint8_t, Drone>& drones) override;
    bool DrawUi() override;
    void ReadYaml(const YAML::Node& node) override;
    void WriteYaml(YAML::Emitter& out) const override;
};

class CircularLayout : public IDroneLayoutProvider {
public:
    const char* Name() const override { return "CircularLayout"; }

    float lon = 0.0f;
    float lat = 0.0f;
    float radius = 0.0001f;
    float initialAltitude = 0.0f;
    float rotationOffset = 0.0f;

    void LayoutDrones(std::map<uint8_t, Drone>& drones) override;
    bool DrawUi() override;
    void ReadYaml(const YAML::Node& node) override;
    void WriteYaml(YAML::Emitter& out) const override;
};

class DroneLayoutProviderRegistry {
public:
    std::vector<std::string> Names() const;
    std::unique_ptr<IDroneLayoutProvider> Create(const std::string& name) const;
};
