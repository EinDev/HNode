#pragma once
// Port of Assets/Plugin/Serializers/SerializerTernary.cs. SerializeChannel only -
// DeserializeChannel is out of scope (and is explicitly marked "NOT IMPLEMENTED
// PROPERLY YET" in the C# reference itself).
#include "ISerializer.h"

class TernarySerializer : public ISerializer {
public:
    const char* Name() const override { return "Ternary"; }

    void SerializeChannel(std::vector<RGBA8>& pixels, uint8_t channelValue, int channel,
                           int textureWidth, int textureHeight, bool autoMaskOnZero) override;

private:
    static constexpr int kBlockSize = 4;
    static constexpr int kBlocksPerCol = 8 * 6;

    static void GetPositionData(int channel, int i, int& x, int& y);
    static void ConvertToTernary(uint8_t value, uint8_t (&outDigits)[6]);
};
