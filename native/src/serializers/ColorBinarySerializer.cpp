// Port of Assets/Plugin/Serializers/SerializerColorBinary.cs. SerializeChannel only -
// DeserializeChannel throws NotImplementedException in the C# original too (this
// serializer never supported Spout input), so there's nothing to defer here.
#include "ColorBinarySerializer.h"
#include "../render/PixelOps.h"

void ColorBinarySerializer::SerializeChannel(std::vector<RGBA8>& pixels, uint8_t channelValue, int channel,
                                              int textureWidth, int textureHeight, bool autoMaskOnZero) {
    uint8_t alpha = GetBlockAlpha(channelValue, autoMaskOnZero);

    // 8 bits of channelValue (LSB-first), plus one dummy trailing bit, matching the
    // C# reference's `bitsList` of 9 entries.
    bool bitsList[9];
    for (int i = 0; i < 8; ++i) {
        bitsList[i] = ((channelValue >> i) & 1) != 0;
    }
    bitsList[8] = false;

    for (int i = 0; i < 9; i += 3) {
        int newChannel = (channel * 3) + i / 3;
        int x = (newChannel / kBlocksPerCol) * kBlockSize;
        int y = (newChannel % kBlocksPerCol) * kBlockSize;
        if (x >= textureWidth || y >= textureHeight) continue;

        RGBA8 color{
            static_cast<uint8_t>(bitsList[i] ? 255 : 0),
            static_cast<uint8_t>(bitsList[i + 1] ? 255 : 0),
            static_cast<uint8_t>(bitsList[i + 2] ? 255 : 0),
            alpha
        };
        MakeColorBlock(pixels, x, y, color, kBlockSize, textureWidth, textureHeight);
    }
}
