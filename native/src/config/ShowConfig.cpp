#include "ShowConfig.h"

#include <fstream>

#include <yaml-cpp/yaml.h>

#include "../serializers/SerializerRegistry.h"
#include "../serializers/VrslSerializer.h"
#include "../exporters/ExporterRegistry.h"
#include "../generators/GeneratorRegistry.h"

namespace {

// Exact text Loader.cs's SaveShowConfiguration() prepends to saved YAML.
const char* kHeaderComment =
    "# All channel values can be represented in 2 ways\n"
    "# Either as a global integer, so 0 upwards like an array\n"
    "# As a direct Universe.Channel mapping, so Universe 3 channel 5 is 3.5\n"
    "# Alongside this, Equations are usable. So (3 * 2).(5 * 5) works\n"
    "\n";

// Parses a DMX channel scalar per DmxChannelRange's simplified rules: either a plain
// non-negative integer, or 1-based "universe.channel" (e.g. "3.5"). Unlike DMXChannel.cs
// this does not support arithmetic-equation syntax. Returns false (leaving `out`
// untouched) if the text doesn't match either shape.
bool ParseDmxChannel(const std::string& text, int& out) {
    auto dot = text.find('.');
    if (dot == std::string::npos) {
        if (text.empty()) return false;
        try {
            size_t consumed = 0;
            long value = std::stol(text, &consumed);
            if (consumed != text.size() || value < 0) return false;
            out = static_cast<int>(value);
            return true;
        } catch (...) {
            return false;
        }
    }

    std::string universePart = text.substr(0, dot);
    std::string channelPart = text.substr(dot + 1);
    if (universePart.empty() || channelPart.empty()) return false;

    try {
        size_t consumedUniverse = 0;
        size_t consumedChannel = 0;
        long universe = std::stol(universePart, &consumedUniverse);
        long channelInUniverse = std::stol(channelPart, &consumedChannel);
        if (consumedUniverse != universePart.size() || consumedChannel != channelPart.size()) return false;
        if (universe < 1 || universe > 512 || channelInUniverse < 1 || channelInUniverse > 512) return false;

        out = static_cast<int>((universe - 1) * 512 + (channelInUniverse - 1));
        return true;
    } catch (...) {
        return false;
    }
}

// Formats a global channel index as "universe.channel", matching DMXChannel.cs's
// implicit string conversion.
std::string FormatDmxChannel(int globalChannel) {
    int universe = (globalChannel / 512) + 1;
    int channelInUniverse = globalChannel - (universe - 1) * 512 + 1;
    return std::to_string(universe) + "." + std::to_string(channelInUniverse);
}

bool ParseOutputConfig(const std::string& text, VrslSerializer::OutputConfig& out) {
    if (text == "HorizontalTop") { out = VrslSerializer::OutputConfig::HorizontalTop; return true; }
    if (text == "VerticalLeft") { out = VrslSerializer::OutputConfig::VerticalLeft; return true; }
    if (text == "VerticalRight") { out = VrslSerializer::OutputConfig::VerticalRight; return true; }
    if (text == "HorizontalBottom") { out = VrslSerializer::OutputConfig::HorizontalBottom; return true; }
    return false;
}

template <typename T>
bool TryAs(const YAML::Node& node, T& out) {
    if (!node || node.IsNull() || !node.IsScalar()) return false;
    try {
        out = node.as<T>();
        return true;
    } catch (const YAML::Exception&) {
        return false;
    }
}

// Resolves a tagged serializer node (e.g. "!VRSL") to a registry instance and reads
// its own persisted fields - shared by both `serializer` and `deserializer`, which use
// the exact same tag-resolution/field-persistence shape (see ShowConfig.h's comment).
// Falls back to the registry default on a missing/unrecognized tag, matching
// Loader.cs's default-to-VRSL behavior for both fields.
ISerializer* ReadSerializerNode(const YAML::Node& node, SerializerRegistry& serializerRegistry) {
    if (!node || !node.IsMap()) return serializerRegistry.Default();

    const std::string& tag = node.Tag();
    bool isDefaultTag = tag.empty() || tag == "?" || tag == "!" || tag == "tag:yaml.org,2002:map";

    ISerializer* resolved =
        isDefaultTag ? serializerRegistry.Default() : serializerRegistry.Find(tag.substr(tag.find_last_of('!') + 1));
    if (!resolved) resolved = serializerRegistry.Default();

    // Only VRSL currently has any config-file-backed fields (see VrslSerializer.h) -
    // every other serializer has no persisted settings.
    if (resolved && std::string(resolved->Name()) == "VRSL") {
        auto* vrsl = static_cast<VrslSerializer*>(resolved);
        TryAs(node["gammaCorrection"], vrsl->gammaCorrection);
        TryAs(node["rgbGridMode"], vrsl->rgbGridMode);

        std::string outputConfigText;
        if (TryAs(node["outputConfig"], outputConfigText)) {
            VrslSerializer::OutputConfig parsed;
            if (ParseOutputConfig(outputConfigText, parsed)) vrsl->outputConfig = parsed;
        }
    }

    return resolved;
}

// Mirror of ReadSerializerNode above - writes a serializer's tag + own fields.
void WriteSerializerNode(YAML::Emitter& emitter, ISerializer* serializer) {
    emitter << YAML::LocalTag(serializer ? serializer->Name() : "VRSL") << YAML::BeginMap;
    if (serializer && std::string(serializer->Name()) == "VRSL") {
        auto* vrsl = static_cast<VrslSerializer*>(serializer);
        emitter << YAML::Key << "gammaCorrection" << YAML::Value << vrsl->gammaCorrection;
        emitter << YAML::Key << "rgbGridMode" << YAML::Value << vrsl->rgbGridMode;
        emitter << YAML::Key << "outputConfig" << YAML::Value << VrslSerializer::ToString(vrsl->outputConfig);
    }
    emitter << YAML::EndMap;
}

} // namespace

bool ShowConfig::Load(const std::string& path, ShowConfig& out, SerializerRegistry& serializerRegistry,
                       const ExporterRegistry& exporterRegistry, const GeneratorRegistry& generatorRegistry,
                       std::string& error) {
    out = ShowConfig{};
    out.serializer = serializerRegistry.Default();
    out.deserializer = serializerRegistry.Default();

    YAML::Node root;
    try {
        root = YAML::LoadFile(path);
    } catch (const YAML::Exception& e) {
        error = "Failed to load show config '" + path + "': " + e.what();
        return false;
    } catch (const std::exception& e) {
        error = "Failed to load show config '" + path + "': " + e.what();
        return false;
    }

    if (!root || !root.IsMap()) {
        error = "Show config '" + path + "' does not contain a YAML mapping.";
        return false;
    }

    TryAs(root["serializeUniverseCount"], out.serializeUniverseCount);

    YAML::Node masked = root["maskedChannels"];
    if (masked && masked.IsSequence()) {
        for (const auto& item : masked) {
            if (!item.IsMap()) continue;

            DmxChannelRange range;
            std::string text;
            if (TryAs(item["start"], text)) ParseDmxChannel(text, range.start);
            if (TryAs(item["end"], text)) ParseDmxChannel(text, range.end);
            out.maskedChannels.push_back(range);
        }
    }

    TryAs(root["invertMask"], out.invertMask);
    TryAs(root["autoMaskOnZero"], out.autoMaskOnZero);

    TryAs(root["spoutOutputName"], out.spoutOutputName);
    TryAs(root["artNetPort"], out.artNetPort);
    TryAs(root["artNetAddress"], out.artNetAddress);
    TryAs(root["targetFramerate"], out.targetFramerate);

    YAML::Node resolution = root["outputResolution"];
    if (resolution && resolution.IsMap()) {
        TryAs(resolution["width"], out.outputWidth);
        TryAs(resolution["height"], out.outputHeight);
    }

    // The node's explicit tag (e.g. "!VRSL", "!Binary", "!BinaryStageFlight") names
    // which registered serializer to select - untagged/default-tagged maps fall back
    // to the registry default (VRSL), matching Loader.cs's own default for both
    // Serializer and Deserializer.
    out.serializer = ReadSerializerNode(root["serializer"], serializerRegistry);
    out.deserializer = ReadSerializerNode(root["deserializer"], serializerRegistry);

    TryAs(root["transcode"], out.transcode);
    TryAs(root["mergeTranscode"], out.mergeTranscode);
    TryAs(root["transcodeUniverseCount"], out.transcodeUniverseCount);
    TryAs(root["spoutInputName"], out.spoutInputName);

    // Each exporter is tagged with its type name (e.g. "!MIDIDMX"); unrecognized tags
    // (an exporter type this native port doesn't have yet) are skipped, not errors,
    // so a config with e.g. a FrameSnapshotExporter block still loads its other
    // exporters and every other field.
    YAML::Node exportersNode = root["exporters"];
    if (exportersNode && exportersNode.IsSequence()) {
        for (const auto& item : exportersNode) {
            if (!item.IsMap()) continue;
            std::string tag = item.Tag();
            std::string name = tag.substr(tag.find_last_of('!') + 1);

            auto exporter = exporterRegistry.Create(name);
            if (!exporter) continue;

            exporter->ReadYaml(item);
            out.exporters.push_back(std::move(exporter));
        }
    }

    // Same tag-resolved-by-name, skip-unrecognized approach as exporters above.
    YAML::Node generatorsNode = root["generators"];
    if (generatorsNode && generatorsNode.IsSequence()) {
        for (const auto& item : generatorsNode) {
            if (!item.IsMap()) continue;
            std::string tag = item.Tag();
            std::string name = tag.substr(tag.find_last_of('!') + 1);

            auto generator = generatorRegistry.Create(name);
            if (!generator) continue;

            generator->ReadYaml(item);
            out.generators.push_back(std::move(generator));
        }
    }

    return true;
}

bool ShowConfig::Save(const std::string& path, std::string& error) const {
    YAML::Emitter emitter;
    emitter << YAML::BeginMap;

    emitter << YAML::Key << "serializeUniverseCount" << YAML::Value << serializeUniverseCount;

    emitter << YAML::Key << "maskedChannels" << YAML::Value << YAML::BeginSeq;
    for (const auto& range : maskedChannels) {
        emitter << YAML::BeginMap;
        emitter << YAML::Key << "start" << YAML::Value << FormatDmxChannel(range.start);
        emitter << YAML::Key << "end" << YAML::Value << FormatDmxChannel(range.end);
        emitter << YAML::EndMap;
    }
    emitter << YAML::EndSeq;

    emitter << YAML::Key << "invertMask" << YAML::Value << invertMask;
    emitter << YAML::Key << "autoMaskOnZero" << YAML::Value << autoMaskOnZero;

    emitter << YAML::Key << "spoutOutputName" << YAML::Value << spoutOutputName;
    emitter << YAML::Key << "artNetPort" << YAML::Value << artNetPort;
    emitter << YAML::Key << "artNetAddress" << YAML::Value << artNetAddress;
    emitter << YAML::Key << "targetFramerate" << YAML::Value << targetFramerate;

    emitter << YAML::Key << "outputResolution" << YAML::Value << YAML::BeginMap;
    emitter << YAML::Key << "width" << YAML::Value << outputWidth;
    emitter << YAML::Key << "height" << YAML::Value << outputHeight;
    emitter << YAML::EndMap;

    // Tagged with the selected serializer's own name, matching YamlDotNet's
    // TagMappedAttribute mechanism on the C# side (e.g. "!VRSL", "!Binary").
    emitter << YAML::Key << "serializer" << YAML::Value;
    WriteSerializerNode(emitter, serializer);

    emitter << YAML::Key << "deserializer" << YAML::Value;
    WriteSerializerNode(emitter, deserializer);

    emitter << YAML::Key << "transcode" << YAML::Value << transcode;
    emitter << YAML::Key << "mergeTranscode" << YAML::Value << mergeTranscode;
    emitter << YAML::Key << "transcodeUniverseCount" << YAML::Value << transcodeUniverseCount;
    emitter << YAML::Key << "spoutInputName" << YAML::Value << spoutInputName;

    emitter << YAML::Key << "exporters" << YAML::Value << YAML::BeginSeq;
    for (const auto& exporter : exporters) {
        emitter << YAML::LocalTag(exporter->Name()) << YAML::BeginMap;
        exporter->WriteYaml(emitter);
        emitter << YAML::EndMap;
    }
    emitter << YAML::EndSeq;

    emitter << YAML::Key << "generators" << YAML::Value << YAML::BeginSeq;
    for (const auto& generator : generators) {
        emitter << YAML::LocalTag(generator->Name()) << YAML::BeginMap;
        generator->WriteYaml(emitter);
        emitter << YAML::EndMap;
    }
    emitter << YAML::EndSeq;

    emitter << YAML::EndMap;

    if (!emitter.good()) {
        error = "Failed to build show config YAML for '" + path + "': " + emitter.GetLastError();
        return false;
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        error = "Failed to open '" + path + "' for writing.";
        return false;
    }

    file << kHeaderComment << emitter.c_str() << "\n";
    if (!file.good()) {
        error = "Failed to write show config to '" + path + "'.";
        return false;
    }

    return true;
}
