#pragma once
// Common interface for DMX generators, matching Assets/Plugin/Generators/IDMXGenerator.cs
// (GenerateDMX(ref List<byte>)). Like exporters (see exporters/IExporter.h), generators
// are a dynamic list - multiple independently-configured instances, add/remove/reorder -
// mirroring ShowConfiguration.cs's `List<IDMXGenerator> Generators`.
//
// IMPORTANT - animation and the render-on-change loop: some generators (Fade, Strobe,
// Time, and in general anything reading the system clock) produce different output on
// every call even with no new ArtNet data, because in Unity they ride the engine's
// unconditional 60Hz Update(). This native port only re-serializes/re-renders when
// `dirty`, so a generator like that needs the main loop to keep marking frames dirty on
// a timer while it's active - otherwise its output would freeze the instant ArtNet goes
// idle. IsAnimated() is how a generator declares this need; main.cpp OR's it across all
// active generators each frame to decide whether to tick continuously (at
// targetFramerate) or fall back to the normal event/ArtNet-driven wakeups. Generators
// that only react to already-changed DMX values (Remap, RemapOnDemand, Snapshot,
// StaticValue once set) return false (the default) and cost nothing extra when idle.
#include <cstdint>
#include <vector>

namespace YAML {
class Emitter;
class Node;
} // namespace YAML

class IGenerator {
public:
    virtual ~IGenerator() = default;

    // Stable name, used as both this generator's YAML tag (e.g. "!Fade") and its label
    // in the add-generator UI - must match the reference C# class name for .shwcfg
    // compatibility.
    virtual const char* Name() const = 0;

    virtual void Construct() {}
    virtual void Deconstruct() {}

    // See the class comment above - true if this generator needs to run every frame
    // on a timer even with no new DMX data, to keep animating.
    virtual bool IsAnimated() const { return false; }

    // Mutates `dmxData` in place - may read AND write channels (e.g. Fade reads a
    // channel's current value to lerp from), and may resize it (mirrors
    // List<byte>.EnsureCapacity/WriteToListAtPosition idioms in the C# generators -
    // see generators/DmxUtil.h for the C++ equivalents).
    virtual void GenerateDMX(std::vector<uint8_t>& dmxData) = 0;

    // Draws this generator's own ImGui settings controls. Returns true if edited.
    virtual bool DrawUi() { return false; }

    virtual void ReadYaml(const YAML::Node& node) { (void)node; }
    virtual void WriteYaml(YAML::Emitter& out) const { (void)out; }
};
