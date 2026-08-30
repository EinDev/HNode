#pragma once
// Show configuration: struct + .shwcfg YAML load/save, field-compatible with
// Assets/ShowConfiguration.cs (only the fields Phase 1 uses - see native/README.md
// for what's intentionally not read/written).
//
// The Unity app serializes with YamlDotNet's CamelCaseNamingConvention, so field
// names on disk are camelCase (e.g. "artNetPort", "spoutOutputName"). The polymorphic
// `serializer` node is tagged with the selected serializer's name (e.g. `!VRSL`,
// YamlDotNet's TagMappedAttribute mechanism); serializer-specific sub-fields (like
// VRSL's gammaCorrection) are only read/written for the serializers that have any -
// see ShowConfig.cpp.
#include <cstdint>
#include <string>
#include <vector>
#include "../serializers/ISerializer.h"

class SerializerRegistry;

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

    // Parses `path` into `out`, leaving fields at their defaults when absent from the
    // file (so a config missing a field just falls back rather than failing to load).
    // Returns false and fills `error` on a hard parse failure (bad YAML, unreadable file).
    static bool Load(const std::string& path, ShowConfig& out, SerializerRegistry& registry, std::string& error);

    // Writes `this` to `path` as .shwcfg YAML, preceded by the same explanatory header
    // comment Loader.cs prepends (channel format / equation note) for familiarity.
    bool Save(const std::string& path, std::string& error) const;
};
