// Port of Assets/Plugin/Serializers/SerializerBinary.cs.
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

uint8_t BinarySerializer::DeserializeChannel(const std::vector<RGBA8>& pixels, int channel, int textureWidth,
                                              int textureHeight) {
    uint8_t value = 0;
    for (int i = 0; i < 8; ++i) {
        int x = 0;
        int y = 0;
        GetPositionData(channel, i, x, y);
        // Offset into the block, matching the C# reference.
        x += 1;
        y += 1;
        if (x >= textureWidth || y >= textureHeight) continue;

        RGBA8 color = GetPixelColor(pixels, x, y, textureWidth, textureHeight);
        bool bit = (color.r / 255.0f) > 0.5f;
        if (bit) value |= static_cast<uint8_t>(1u << i);
    }
    return value;
}
