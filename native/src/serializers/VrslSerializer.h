#pragma once
// Port of Assets/Plugin/Serializers/SerializerVRSL.cs. Only SerializeChannel (DMX ->
// pixels) is needed for Phase 1; DeserializeChannel (pixels -> DMX, i.e. Spout input)
// is out of scope per the native port plan.
#include <cstdint>
#include <vector>
#include "../render/PixelOps.h"

class VrslSerializer {
public:
    enum class OutputConfig { HorizontalTop, VerticalLeft, VerticalRight, HorizontalBottom };

    bool gammaCorrection = true;
    bool rgbGridMode = false;
    OutputConfig outputConfig = OutputConfig::HorizontalTop;

    void SerializeChannel(std::vector<RGBA8>& pixels, uint8_t channelValue, int channel,
                           int textureWidth, int textureHeight, bool autoMaskOnZero) const;

    static const char* ToString(OutputConfig config);
    static OutputConfig CycleNext(OutputConfig config);

private:
    static constexpr int kBlockSize = 16;      // 16x16 pixels per channel block
    static constexpr int kBlocksPerCol = 13;   // channels per column

    void GetPositionData(int channel, int& x, int& y, int textureWidth, int textureHeight) const;
    static int GetUniverseWrap(int channel);
};
