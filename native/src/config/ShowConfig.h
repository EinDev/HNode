#pragma once
// Show configuration: struct + .shwcfg YAML load/save, field-compatible with
// Assets/ShowConfiguration.cs (only the fields Phase 1 uses - see native/README.md
// for what's intentionally not read/written).
//
// The Unity app serializes with YamlDotNet's CamelCaseNamingConvention, so field
// names on disk are camelCase (e.g. "artNetPort", "spoutOutputName"). The polymorphic
// `serializer` node (and `deserializer`, the Spout-input mirror) is tagged with the
// selected serializer's name (e.g. `!VRSL`, YamlDotNet's TagMappedAttribute
// mechanism); serializer-specific sub-fields (like VRSL's gammaCorrection) are only
// read/written for the serializers that have any - see ShowConfig.cpp.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "../serializers/ISerializer.h"
#include "../exporters/IExporter.h"
#include "../generators/IGenerator.h"

class SerializerRegistry;
class ExporterRegistry;
class GeneratorRegistry;

struct DmxChannelRange {
    // Global (flat) channel indices, inclusive on both ends - simplified from
    // DMXChannel.cs, which also accepts "universe.channel" text and arithmetic
    // equations. Phase 1 only parses/emits plain non-negative integers or
    // "universe.channel" (e.g. "3.5"); equation syntax is not supported.
    int start = 0;
    int end = 0;

    bool Contains(int channel) const { return channel >= start && channel <= end; }
};

struct ShowConfig {
    // Non-owning: points into a SerializerRegistry that outlives this ShowConfig
    // (main.cpp constructs the registry before any ShowConfig). Never null once the
    // config has been through its normal setup path (main.cpp assigns
    // registry.Default() right after construction; Load() always resolves to at
    // least the registry's default if the file's tag is missing/unrecognized).
    ISerializer* serializer = nullptr;

    // Independent second serializer instance used for Spout INPUT (matches
    // ShowConfiguration.cs's separate `Deserializer` field - the input format doesn't
    // have to match the output `serializer`'s format). Same non-null/registry-owned
    // contract as `serializer` above.
    ISerializer* deserializer = nullptr;

    // Owning, unlike `serializer`: each exporter is its own independently-configured
    // instance (dynamic list, mirrors ShowConfiguration.cs's `List<IExporter> Exporters`
    // and the add/remove/reorder InterfaceList UI) rather than a persistent
    // single-selection registry entry. This makes ShowConfig move-only.
    std::vector<std::unique_ptr<IExporter>> exporters;

    // Owning, dynamic list - same rationale as `exporters` above, mirrors
    // ShowConfiguration.cs's `List<IDMXGenerator> Generators`. Run BEFORE the
    // serializer in the per-frame pipeline (matches TextureWriter.cs's ordering:
    // merge DMX -> generators -> serializer).
    std::vector<std::unique_ptr<IGenerator>> generators;

    int serializeUniverseCount = INT32_MAX;
    std::vector<DmxChannelRange> maskedChannels;
    bool invertMask = false;
    bool autoMaskOnZero = false;

    std::string spoutOutputName = "HNode Output";
    int artNetPort = 6454;
    std::string artNetAddress = "0.0.0.0"; // "0.0.0.0" == IPAddress.Any: listen on all interfaces
    int targetFramerate = 60;

    int outputWidth = 1920;
    int outputHeight = 1080;

    // Spout input / "Transcode" (Assets/TextureReader.cs + the Transcode-specific
    // ~40 lines of Assets/TextureWriter.cs) - reads an external Spout source, runs it
    // through `deserializer`, and merges the result into the ArtNet-merged DMX buffer
    // before generators run. `transcode` off (the default) means none of this runs at
    // all, matching TextureReader.Update()'s early-out.
    bool transcode = false;
    // false: transcoded DMX fully REPLACES the ArtNet merge. true: channel-wise max()
    // of the two - see FrameRenderer::Render() for the exact merge logic ported from
    // TextureWriter.cs.
    bool mergeTranscode = false;
    int transcodeUniverseCount = 9;
    std::string spoutInputName = "HNode Input";

    // Parses `path` into `out`, leaving fields at their defaults when absent from the
    // file (so a config missing a field just falls back rather than failing to load).
    // Returns false and fills `error` on a hard parse failure (bad YAML, unreadable file).
    static bool Load(const std::string& path, ShowConfig& out, SerializerRegistry& serializerRegistry,
                      const ExporterRegistry& exporterRegistry, const GeneratorRegistry& generatorRegistry,
                      std::string& error);

    // Writes `this` to `path` as .shwcfg YAML, preceded by the same explanatory header
    // comment Loader.cs prepends (channel format / equation note) for familiarity.
    bool Save(const std::string& path, std::string& error) const;
};
