#pragma once
// Port of Assets/Plugin/Serializers/SerializerVRSL.cs.
#include <cstdint>
#include <vector>
#include "ISerializer.h"
#include "../render/PixelOps.h"

class VrslSerializer : public ISerializer {
public:
    enum class OutputConfig { HorizontalTop, VerticalLeft, VerticalRight, HorizontalBottom };

    bool gammaCorrection = true;
    bool rgbGridMode = false;
    OutputConfig outputConfig = OutputConfig::HorizontalTop;

    const char* Name() const override { return "VRSL"; }

    void SerializeChannel(std::vector<RGBA8>& pixels, uint8_t channelValue, int channel,
                           int textureWidth, int textureHeight, bool autoMaskOnZero) override;

    uint8_t DeserializeChannel(const std::vector<RGBA8>& pixels, int channel, int textureWidth,
                                int textureHeight) override;

    // Mirrors SerializerVRSL.cs's ConstructUserInterface: gamma/RGB-grid toggles and
    // the output-config cycle button.
    bool DrawUi() override;

    static const char* ToString(OutputConfig config);
    static OutputConfig CycleNext(OutputConfig config);

private:
    static constexpr int kBlockSize = 16;      // 16x16 pixels per channel block
    static constexpr int kBlocksPerCol = 13;   // channels per column

    void GetPositionData(int channel, int& x, int& y, int textureWidth, int textureHeight) const;
    static int GetUniverseWrap(int channel);
};
