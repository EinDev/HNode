#include "GeneratorRegistry.h"

#include "StaticValueGenerator.h"
#include "RemapGenerator.h"
#include "RemapOnDemandGenerator.h"
#include "SnapshotGenerator.h"
#include "FadeGenerator.h"
#include "StrobeGenerator.h"
#include "DmxPacketGenerator.h"
#include "TextGenerator.h"
#include "SrtGenerator.h"
#include "LrcGenerator.h"
#include "AssGenerator.h"
#include "TwitchChatGenerator.h"
#include "OnTimeGenerator.h"

std::vector<std::string> GeneratorRegistry::Names() const {
    return {"StaticValue", "Remap",      "RemapOnDemand", "Snapshot",   "Fade",
            "Strobe",      "DMXPacket",  "Text",          "Time",       "SRT",
            "LRC",         "ASS",        "TwitchChat",    "OnTime"};
}

std::unique_ptr<IGenerator> GeneratorRegistry::Create(const std::string& name) const {
    if (name == "StaticValue") return std::make_unique<StaticValueGenerator>();
    if (name == "Remap") return std::make_unique<RemapGenerator>();
    if (name == "RemapOnDemand") return std::make_unique<RemapOnDemandGenerator>();
    if (name == "Snapshot") return std::make_unique<SnapshotGenerator>();
    if (name == "Fade") return std::make_unique<FadeGenerator>();
    if (name == "Strobe") return std::make_unique<StrobeGenerator>();
    if (name == "DMXPacket") return std::make_unique<DmxPacketGenerator>();
    if (name == "Text") return std::make_unique<TextGenerator>();
    if (name == "Time") return std::make_unique<TimeGenerator>();
    if (name == "SRT") return std::make_unique<SrtGenerator>();
    if (name == "LRC") return std::make_unique<LrcGenerator>();
    if (name == "ASS") return std::make_unique<AssGenerator>();
    if (name == "TwitchChat") return std::make_unique<TwitchChatGenerator>();
    if (name == "OnTime") return std::make_unique<OnTimeGenerator>();
    return nullptr;
}
