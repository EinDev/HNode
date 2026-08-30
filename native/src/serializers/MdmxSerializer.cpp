// Port of Assets/Plugin/Serializers/SerializerMDMX.cs (aka "Binary Stage Flight").
// SerializeChannel + CompleteFrame (the CRC block pass) only - DeserializeChannel
// (Spout input / transcode CRC check) is out of scope, see native/README.md.
#include "MdmxSerializer.h"
#include <vector>
#include "../render/PixelOps.h"

void MdmxSerializer::GetPositionData(int channel, int bitIndex, int textureWidth, int& x, int& y) {
    // encode backwards, endianness flip
    int newChannel = (channel * 8) + (7 - bitIndex);
    x = (newChannel / kBlocksPerCol) * kBlockSize;
    y = (newChannel % kBlocksPerCol) * kBlockSize;
    CalculateWrapping(x, y, textureWidth, x, y);
}

void MdmxSerializer::CalculateWrapping(int x, int y, int textureWidth, int& adjX, int& adjY) {
    int wrap = x / textureWidth;
    int newAdjX = x % textureWidth;
    int newAdjY = y + (wrap * (kBlocksPerCol + kCrcBits) * kBlockSize); // +CRCBits rows reserved per wrapped "row" for the CRC blocks
    adjX = newAdjX;
    adjY = newAdjY;
}

void MdmxSerializer::SerializeChannel(std::vector<RGBA8>& pixels, uint8_t channelValue, int channel,
                                       int textureWidth, int textureHeight, bool autoMaskOnZero) {
    uint8_t alpha = GetBlockAlpha(channelValue, autoMaskOnZero);
    for (int i = 0; i < 8; ++i) {
        int x = 0;
        int y = 0;
        GetPositionData(channel, i, textureWidth, x, y);
        // Bounds checking is disabled in the reference (commented out there too) -
        // MakeColorBlock/PixelToIndex safely no-op on out-of-range writes instead.
        bool bit = ((channelValue >> i) & 1) != 0;
        uint8_t component = bit ? 255 : 0;
        RGBA8 color{component, component, component, alpha};
        MakeColorBlock(pixels, x, y, color, kBlockSize, textureWidth, textureHeight);
    }
}

void MdmxSerializer::CompleteFrame(std::vector<RGBA8>& pixels, const std::vector<uint8_t>& channelValues,
                                    int textureWidth, int textureHeight) {
    // Y position where the CRC row starts, right below the kBlocksPerCol data rows.
    int startY = kBlocksPerCol * kBlockSize;

    for (size_t i = 0; i < channelValues.size(); i += kChannelsPerCol) {
        // The C# does channelValues.GetRange(i, channelsPerCol), which throws if fewer
        // than channelsPerCol values remain. Deliberate deviation: instead of
        // replicating that crash, take whatever's actually available and zero-pad up
        // to kChannelsPerCol bytes before computing the CRC.
        uint8_t values[kChannelsPerCol] = {0};
        size_t available = channelValues.size() - i;
        if (available > static_cast<size_t>(kChannelsPerCol)) available = static_cast<size_t>(kChannelsPerCol);
        for (size_t k = 0; k < available; ++k) values[k] = channelValues[i + k];

        uint8_t crc = Crc4(values, kChannelsPerCol);

        int x = (static_cast<int>(i) / kChannelsPerCol) * kBlockSize;
        for (int j = 0; j < kCrcBits; ++j) {
            int y = startY + j * kBlockSize;
            int xd = 0;
            int yd = 0;
            CalculateWrapping(x, y, textureWidth, xd, yd);

            // bits[7 - j]: the CRC value occupies the top 4 bits of crc (bits 4-7),
            // so j = 0..3 draws bits 7,6,5,4 (MSB-first).
            bool bit = ((crc >> (7 - j)) & 1) != 0;
            uint8_t component = bit ? 255 : 0;
            // Alpha is always forced fully-on here (Util.GetBlockAlpha(255) in the C#),
            // ignoring autoMaskOnZero entirely - intentional per the C# comment.
            RGBA8 color{component, component, component, 255};
            MakeColorBlock(pixels, xd, yd, color, kBlockSize, textureWidth, textureHeight);
        }
    }
}

uint8_t MdmxSerializer::Crc4(const uint8_t* data, size_t count) {
    uint32_t crc = 0u;
    const uint32_t polynomial = 0x03u;

    for (size_t n = 0; n < count; ++n) {
        uint32_t v = data[n];
        for (int bit = 7; bit >= 0; --bit) {
            uint32_t inBit = (v >> bit) & 1u;
            bool top = (crc & 0x8u) != 0u;
            crc = ((crc << 1) | inBit) & 0xFu;
            if (top) crc ^= polynomial;
        }
    }
    return static_cast<uint8_t>(crc << kCrcBits);
}
