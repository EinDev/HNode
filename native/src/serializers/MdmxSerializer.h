#pragma once
// Port of Assets/Plugin/Serializers/SerializerMDMX.cs (aka "Binary Stage Flight" -
// the C# class carries [TagAlias("BinaryStageFlight")] so old configs saved under
// that name still load; SerializerRegistry resolves that alias to this class's
// "MDMX" name).
#include <cstdint>
#include "ISerializer.h"

class MdmxSerializer : public ISerializer {
public:
    const char* Name() const override { return "MDMX"; }

    void SerializeChannel(std::vector<RGBA8>& pixels, uint8_t channelValue, int channel,
                           int textureWidth, int textureHeight, bool autoMaskOnZero) override;

    // Reads the data blocks back out (ignores the CRC row entirely, same as the C#
    // reference - its CRC verification is a documented TODO there too, never wired up
    // to actually reject bad data on read).
    uint8_t DeserializeChannel(const std::vector<RGBA8>& pixels, int channel, int textureWidth,
                                int textureHeight) override;

    // Draws the per-column CRC4 blocks below the data, mirrors
    // SerializerMDMX.cs's CompleteFrame.
    void CompleteFrame(std::vector<RGBA8>& pixels, const std::vector<uint8_t>& channelValues,
                        int textureWidth, int textureHeight) override;

private:
    static constexpr int kBlockSize = 4;
    static constexpr int kChannelsPerCol = 6;
    static constexpr int kBlocksPerCol = kChannelsPerCol * 8;
    static constexpr int kCrcBits = 4;

    static void GetPositionData(int channel, int bitIndex, int textureWidth, int& x, int& y);
    static void CalculateWrapping(int x, int y, int textureWidth, int& adjX, int& adjY);
    static uint8_t Crc4(const uint8_t* data, size_t count);
};
