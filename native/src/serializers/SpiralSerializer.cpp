#include "SpiralSerializer.h"

#include <algorithm>
#include <utility>

void SpiralSerializer::InitFrame() {
    x_ = 0;
    y_ = 0;
    state_ = 0;
    visited_.clear();
}

void SpiralSerializer::CalculateNextMove(int& nextX, int& nextY) const {
    nextX = x_;
    nextY = y_;
    switch (state_) {
        case 0: nextX = x_ + 1; break;  // right
        case 1: nextY = y_ + 1; break;  // down
        case 2: nextX = x_ - 1; break;  // left
        case 3: nextY = y_ - 1; break;  // up
        default: break;
    }
}

void SpiralSerializer::SerializeChannel(std::vector<RGBA8>& pixels, uint8_t channelValue, int channel,
                                         int textureWidth, int textureHeight, bool autoMaskOnZero) {
    (void)channel;

    int scaledWidth = textureWidth / kBlockSize;
    int scaledHeight = textureHeight / kBlockSize;

    int xfinal = x_ * kBlockSize;
    int yfinal = y_ * kBlockSize;

    RGBA8 color{channelValue, channelValue, channelValue, GetBlockAlpha(channelValue, autoMaskOnZero)};
    MakeColorBlock(pixels, xfinal, yfinal, color, kBlockSize, textureWidth, textureHeight);

    int nextX = x_;
    int nextY = y_;
    CalculateNextMove(nextX, nextY);

    if (std::find(visited_.begin(), visited_.end(), std::make_pair(nextX, nextY)) != visited_.end() ||
        nextX < 0 || nextY < 0 || nextX >= scaledWidth || nextY >= scaledHeight) {
        state_++;
        if (state_ > 3) state_ = 0;
        CalculateNextMove(nextX, nextY);
    }

    visited_.emplace_back(x_, y_);
    x_ = nextX;
    y_ = nextY;
}
