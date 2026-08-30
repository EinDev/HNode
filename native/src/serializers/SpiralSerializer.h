#pragma once
// Port of Assets/Plugin/Serializers/SerializerSpiral.cs. SerializeChannel only -
// DeserializeChannel throws NotImplementedException in the C# original too.
#include <vector>
#include "ISerializer.h"

class SpiralSerializer : public ISerializer {
public:
    const char* Name() const override { return "Spiral"; }

    // Resets the walk position/direction/visited-cells state - must run once at the
    // start of every frame (mirrors SerializerSpiral.cs's InitFrame).
    void InitFrame() override;

    void SerializeChannel(std::vector<RGBA8>& pixels, uint8_t channelValue, int channel,
                           int textureWidth, int textureHeight, bool autoMaskOnZero) override;

private:
    static constexpr int kBlockSize = 8;

    void CalculateNextMove(int& nextX, int& nextY) const;

    int x_ = 0;
    int y_ = 0;
    int state_ = 0;
    std::vector<std::pair<int, int>> visited_;
};
