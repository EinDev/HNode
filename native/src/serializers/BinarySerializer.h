#pragma once
// Port of Assets/Plugin/Serializers/SerializerBinary.cs.
#include "ISerializer.h"

class BinarySerializer : public ISerializer {
public:
    const char* Name() const override { return "Binary"; }

    void SerializeChannel(std::vector<RGBA8>& pixels, uint8_t channelValue, int channel,
                           int textureWidth, int textureHeight, bool autoMaskOnZero) override;

    uint8_t DeserializeChannel(const std::vector<RGBA8>& pixels, int channel, int textureWidth,
                                int textureHeight) override;

private:
    static constexpr int kBlockSize = 4;
    static constexpr int kBlocksPerCol = 52;

    static void GetPositionData(int channel, int bitIndex, int& x, int& y);
};
