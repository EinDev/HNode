// Port of Assets/Plugin/Serializers/SerializerBinary.cs. SerializeChannel only -
// DeserializeChannel (Spout input) is out of scope, see native/README.md.
#include "BinarySerializer.h"
#include "../render/PixelOps.h"

void BinarySerializer::GetPositionData(int channel, int bitIndex, int& x, int& y) {
    int newChannel = (channel * 8) + bitIndex;
    x = (newChannel / kBlocksPerCol) * kBlockSize;
    y = (newChannel % kBlocksPerCol) * kBlockSize;
}

void BinarySerializer::SerializeChannel(std::vector<RGBA8>& pixels, uint8_t channelValue, int channel,
                                         int textureWidth, int textureHeight, bool autoMaskOnZero) {
    uint8_t alpha = GetBlockAlpha(channelValue, autoMaskOnZero);
    for (int i = 0; i < 8; ++i) {
        int x = 0;
        int y = 0;
        GetPositionData(channel, i, x, y);
        if (x >= textureWidth || y >= textureHeight) continue;

        bool bit = ((channelValue >> i) & 1) != 0;
        uint8_t component = bit ? 255 : 0;
        RGBA8 color{component, component, component, alpha};
        MakeColorBlock(pixels, x, y, color, kBlockSize, textureWidth, textureHeight);
    }
}
