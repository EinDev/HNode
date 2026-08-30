#pragma once
// Owns one persistent instance of each available ISerializer, mirroring Loader.cs's
// `serializers` list (built once via reflection over all IDMXSerializer
// implementations, then referenced - not recreated - by the dropdown and by
// show-config loading). Keeping instances persistent means a serializer's own
// settings (e.g. VRSL's gammaCorrection) survive switching away and back within a
// session, same as the C# app.
#include <memory>
#include <string>
#include <vector>
#include "ISerializer.h"

class SerializerRegistry {
public:
    SerializerRegistry();

    const std::vector<std::unique_ptr<ISerializer>>& All() const { return serializers_; }

    // Looks up by Name() (exact match), then by a small alias table for renamed
    // classes (currently just "BinaryStageFlight" -> "MDMX", matching MDMX's C#
    // [TagAlias("BinaryStageFlight")]). Returns nullptr if nothing matches.
    ISerializer* Find(const std::string& name) const;

    // The serializer selected when nothing else is specified - matches Loader.cs
    // defaulting showconf.Serializer/Deserializer to `new VRSL()`.
    ISerializer* Default() const { return defaultSerializer_; }

private:
    std::vector<std::unique_ptr<ISerializer>> serializers_;
    ISerializer* defaultSerializer_ = nullptr;
};
