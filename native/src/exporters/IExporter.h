#pragma once
// Common interface for exporters, matching Assets/Plugin/Exporters/IExporter.cs's
// shape (SerializeChannel/InitFrame/CompleteFrame/FrameRendered). Unlike ISerializer
// (single-selection, persistent registry instances), exporters are a dynamic list -
// zero or more active at once, each its own instance with its own settings,
// reorderable/removable - mirroring ShowConfiguration.cs's `List<IExporter> Exporters`
// and the InterfaceList add/remove/reorder UI. See ExporterRegistry for the factory
// that creates fresh instances (as opposed to SerializerRegistry's persistent ones).
//
// Cadence: exporters tick every main-loop iteration, NOT gated behind the
// render-on-change `dirty` flag the serializer/Spout path uses. Several exporters
// (MIDIDMX's watchdog, TimeCodeExporter's UDP timecode broadcast) need a steady
// heartbeat regardless of whether DMX data actually changed - the C# originals run
// off Unity's unconditional 60Hz Update() and would misbehave (world-side timeouts,
// stale time sync) if starved for long idle stretches. Only the pixel
// serialize/upload/Spout-send path is dirty-gated; exporters are not.
#include <cstdint>
#include <vector>
#include "../render/PixelOps.h"

namespace YAML {
class Emitter;
class Node;
} // namespace YAML

class IExporter {
public:
    virtual ~IExporter() = default;

    // Stable name used as both this exporter's YAML tag (e.g. "!MIDIDMX") and its
    // label in the add-exporter UI - must match the reference C# class name for
    // .shwcfg compatibility.
    virtual const char* Name() const = 0;

    // Lifecycle: Construct() runs once when the exporter is added/loaded (mirrors
    // IExporter/IConstructable's Construct()); Deconstruct() runs once when removed or
    // on shutdown.
    virtual void Construct() {}
    virtual void Deconstruct() {}

    virtual void InitFrame(const std::vector<uint8_t>& channelValues) { (void)channelValues; }
    virtual void SerializeChannel(uint8_t channelValue, int channel) { (void)channelValue; (void)channel; }
    virtual void CompleteFrame(const std::vector<uint8_t>& channelValues) { (void)channelValues; }

    // `pixels` is the CPU-side rendered frame (top-down RGBA8, `width`x`height`) -
    // equivalent to the C#'s `ref Texture2D texture` hook, passed as the CPU buffer
    // FrameRenderer already maintains rather than a GL texture, since exporters that
    // need pixels (FrameSnapshotExporter) want to encode them (e.g. to PNG) on the
    // CPU anyway and this avoids a GPU readback.
    virtual void FrameRendered(const std::vector<RGBA8>& pixels, int width, int height) {
        (void)pixels; (void)width; (void)height;
    }

    // Draws this exporter's own ImGui settings controls (mirrors
    // IExporter::ConstructUserInterface). Returns true if any control was edited.
    virtual bool DrawUi() { return false; }

    // Per-instance YAML persistence - each exporter type owns reading/writing its own
    // fields (there are enough exporter types with meaningfully different fields that
    // centralizing this in ShowConfig.cpp the way VRSL's 3 fields were special-cased
    // would stop scaling). `node` is the exporter's own YAML map (already
    // tag-resolved to this instance's type by ExporterRegistry - implementations
    // should NOT re-check the tag). Default: no fields to persist.
    virtual void ReadYaml(const YAML::Node& node) { (void)node; }
    virtual void WriteYaml(YAML::Emitter& out) const { (void)out; }
};
