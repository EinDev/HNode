// Port of Assets/Plugin/Serializers/SerializerTernary.cs. SerializeChannel only -
// DeserializeChannel is out of scope (and is explicitly marked "NOT IMPLEMENTED
// PROPERLY YET" in the C# reference itself).
#include "TernarySerializer.h"
#include "../render/PixelOps.h"

void TernarySerializer::GetPositionData(int channel, int i, int& x, int& y) {
    int newChannel = (channel * 6) + i;
    x = (newChannel / kBlocksPerCol) * kBlockSize;
    y = (newChannel % kBlocksPerCol) * kBlockSize;
}

void TernarySerializer::ConvertToTernary(uint8_t value, uint8_t (&outDigits)[6]) {
    if (value == 0) {
        for (int i = 0; i < 6; ++i) outDigits[i] = 0;
        return;
    }
    int index = 5;
    for (int i = 0; i < 6; ++i) {
        outDigits[index--] = static_cast<uint8_t>(value % 3);
        value = static_cast<uint8_t>(value / 3);
    }
}

void TernarySerializer::SerializeChannel(std::vector<RGBA8>& pixels, uint8_t channelValue, int channel,
                                          int textureWidth, int textureHeight, bool autoMaskOnZero) {
    uint8_t digits[6];
    ConvertToTernary(channelValue, digits);

    uint8_t alpha = GetBlockAlpha(channelValue, autoMaskOnZero);

    for (int i = 0; i < 6; ++i) {
        int x = 0;
        int y = 0;
        GetPositionData(channel, i, x, y);
        if (x >= textureWidth || y >= textureHeight) continue;

        float t = static_cast<float>(digits[i]) / 2.0f;
        uint8_t intensity = static_cast<uint8_t>(t * 255.0f);
        RGBA8 color{intensity, intensity, intensity, alpha};
        MakeColorBlock(pixels, x, y, color, kBlockSize, textureWidth, textureHeight);
    }
}
