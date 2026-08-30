#include "VrslSerializer.h"
#include "imgui.h"

void VrslSerializer::GetPositionData(int channel, int& x, int& y, int textureWidth, int textureHeight) const {
    int channelInUniverse = channel % 512;
    int universe = (channel - channelInUniverse) / 512;

    if (rgbGridMode) {
        universe = universe % 3;
    }

    int universeOffset = universe * ((512 / kBlocksPerCol * kBlockSize) + kBlockSize);

    int tempY = (channelInUniverse % kBlocksPerCol) * kBlockSize;
    int tempX = ((channelInUniverse / kBlocksPerCol) * kBlockSize) + universeOffset;

    bool isVertical = outputConfig == OutputConfig::VerticalLeft || outputConfig == OutputConfig::VerticalRight;
    x = isVertical ? tempY : tempX;
    y = isVertical ? tempX : tempY;

    switch (outputConfig) {
        case OutputConfig::HorizontalTop:
            break;
        case OutputConfig::HorizontalBottom:
            y += textureHeight - (kBlocksPerCol * kBlockSize);
            break;
        case OutputConfig::VerticalLeft:
            y = textureHeight - y - kBlockSize;
            break;
        case OutputConfig::VerticalRight:
            y = textureHeight - y - kBlockSize;
            x += textureWidth - (kBlocksPerCol * kBlockSize);
            break;
    }
}

int VrslSerializer::GetUniverseWrap(int channel) {
    int universe = channel / 512;
    return universe / 3;
}

void VrslSerializer::SerializeChannel(std::vector<RGBA8>& pixels, uint8_t channelValue, int channel,
                                       int textureWidth, int textureHeight, bool autoMaskOnZero) {
    int x, y;
    GetPositionData(channel, x, y, textureWidth, textureHeight);

    float v = channelValue / 255.0f;
    float a = GetBlockAlpha(channelValue, autoMaskOnZero) / 255.0f;
    if (gammaCorrection) {
        v = SrgbToLinear(v);
        // Alpha is not a color component in Unity's Color.linear conversion - it passes
        // through unchanged, matching `color.linear` operating on r/g/b only.
    }

    RGBA8 color{
        static_cast<uint8_t>(v * 255.0f + 0.5f),
        static_cast<uint8_t>(v * 255.0f + 0.5f),
        static_cast<uint8_t>(v * 255.0f + 0.5f),
        static_cast<uint8_t>(a * 255.0f + 0.5f)
    };

    if (rgbGridMode) {
        ColorChannel cchannel;
        switch (GetUniverseWrap(channel)) {
            case 0: cchannel = ColorChannel::Red; break;
            case 1: cchannel = ColorChannel::Green; break;
            case 2: cchannel = ColorChannel::Blue; break;
            default: return;
        }
        uint8_t value = cchannel == ColorChannel::Red ? color.r : cchannel == ColorChannel::Green ? color.g : color.b;
        MixColorBlock(pixels, x, y, value, cchannel, kBlockSize, textureWidth, textureHeight);
    } else {
        MakeColorBlock(pixels, x, y, color, kBlockSize, textureWidth, textureHeight);
    }
}

const char* VrslSerializer::ToString(OutputConfig config) {
    switch (config) {
        case OutputConfig::HorizontalTop: return "HorizontalTop";
        case OutputConfig::VerticalLeft: return "VerticalLeft";
        case OutputConfig::VerticalRight: return "VerticalRight";
        case OutputConfig::HorizontalBottom: return "HorizontalBottom";
    }
    return "HorizontalTop";
}

bool VrslSerializer::DrawUi() {
    bool changed = false;
    changed |= ImGui::Checkbox("Gamma Correction", &gammaCorrection);
    changed |= ImGui::Checkbox("RGB Grid Mode", &rgbGridMode);
    ImGui::Text("Output Config: %s", ToString(outputConfig));
    if (ImGui::Button("Cycle Output Config")) {
        outputConfig = CycleNext(outputConfig);
        changed = true;
    }
    return changed;
}

VrslSerializer::OutputConfig VrslSerializer::CycleNext(OutputConfig config) {
    switch (config) {
        case OutputConfig::HorizontalTop: return OutputConfig::VerticalLeft;
        case OutputConfig::VerticalLeft: return OutputConfig::VerticalRight;
        case OutputConfig::VerticalRight: return OutputConfig::HorizontalBottom;
        case OutputConfig::HorizontalBottom: return OutputConfig::HorizontalTop;
    }
    return OutputConfig::HorizontalTop;
}
