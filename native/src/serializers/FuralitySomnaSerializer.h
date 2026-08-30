#pragma once
// Port of Assets/Plugin/Serializers/SerializerFuralitySomna.cs. SerializeChannel only
// - DeserializeChannel throws NotImplementedException in the C# original too.
//
// `mergedChannels` (mapping a global DMX channel to a single RGB color channel it
// should be mixed into, so several 1-value DMX channels can share one RGB pixel
// block) has no UI in the C# reference either (ConstructUserInterface is empty
// there) - it's a config-only field. The native port carries the field for parity
// but, like the original, exposes no UI for editing it and does not yet persist it
// to .shwcfg (ShowConfig has no YAML field for it) - see native/README.md.
#include <unordered_map>
#include "ISerializer.h"

class FuralitySomnaSerializer : public ISerializer {
public:
    const char* Name() const override { return "FuralitySomna"; }

    void InitFrame() override;

    void SerializeChannel(std::vector<RGBA8>& pixels, uint8_t channelValue, int channel,
                           int textureWidth, int textureHeight, bool autoMaskOnZero) override;

    std::unordered_map<int, ColorChannel> mergedChannels;

private:
    static constexpr int kBlockSize = 16;
    static constexpr int kBlocksPerCol = 13;

    int cumulativeOffset_ = 0;
};
