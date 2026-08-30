#include "SerializerRegistry.h"

#include "VrslSerializer.h"
#include "BinarySerializer.h"
#include "ColorBinarySerializer.h"
#include "TernarySerializer.h"
#include "SpiralSerializer.h"
#include "MdmxSerializer.h"
#include "FuralitySomnaSerializer.h"

SerializerRegistry::SerializerRegistry() {
    serializers_.push_back(std::make_unique<VrslSerializer>());
    serializers_.push_back(std::make_unique<BinarySerializer>());
    serializers_.push_back(std::make_unique<ColorBinarySerializer>());
    serializers_.push_back(std::make_unique<TernarySerializer>());
    serializers_.push_back(std::make_unique<SpiralSerializer>());
    serializers_.push_back(std::make_unique<MdmxSerializer>());
    serializers_.push_back(std::make_unique<FuralitySomnaSerializer>());

    defaultSerializer_ = serializers_.front().get(); // VRSL, matching Loader.cs's default
}

ISerializer* SerializerRegistry::Find(const std::string& name) const {
    std::string resolved = name;
    if (resolved == "BinaryStageFlight") resolved = "MDMX"; // MDMX.cs's [TagAlias("BinaryStageFlight")]

    for (const auto& serializer : serializers_) {
        if (resolved == serializer->Name()) return serializer.get();
    }
    return nullptr;
}
