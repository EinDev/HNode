#pragma once
// Port of Assets/Plugin/Serializers/SerializerColorBinary.cs. SerializeChannel only -
// DeserializeChannel throws NotImplementedException in the C# original too (this
// serializer never supported Spout input), so there's nothing to defer here.
#include "ISerializer.h"

class ColorBinarySerializer : public ISerializer {
public:
    const char* Name() const override { return "ColorBinary"; }

    void SerializeChannel(std::vector<RGBA8>& pixels, uint8_t channelValue, int channel,
                           int textureWidth, int textureHeight, bool autoMaskOnZero) override;

private:
    static constexpr int kBlockSize = 4;
    static constexpr int kBlocksPerCol = 52;
};
