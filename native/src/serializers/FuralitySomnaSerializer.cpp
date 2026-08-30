#include "FuralitySomnaSerializer.h"

void FuralitySomnaSerializer::InitFrame() {
    cumulativeOffset_ = 0;
}

void FuralitySomnaSerializer::SerializeChannel(std::vector<RGBA8>& pixels, uint8_t channelValue, int channel,
                                                int textureWidth, int textureHeight, bool autoMaskOnZero) {
    int x = ((channel - cumulativeOffset_) / kBlocksPerCol) * kBlockSize;
    int y = ((channel - cumulativeOffset_) % kBlocksPerCol) * kBlockSize;

    auto it = mergedChannels.find(channel);
    if (it != mergedChannels.end()) {
        MixColorBlock(pixels, x, y, channelValue, it->second, kBlockSize, textureWidth, textureHeight);
    } else {
        RGBA8 color{channelValue, channelValue, channelValue, GetBlockAlpha(channelValue, autoMaskOnZero)};
        MakeColorBlock(pixels, x, y, color, kBlockSize, textureWidth, textureHeight);
    }

    if (it != mergedChannels.end()) {
        if (it->second == ColorChannel::Blue) return;
        cumulativeOffset_++;
    }
}
