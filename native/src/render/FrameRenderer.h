#pragma once
// CPU pixel buffer -> GL preview texture, equivalent to the parts of
// Assets/TextureWriter.cs that Phase 1 needs: merge already-received DMX (via
// DmxBuffer), apply channel masking, run it through VrslSerializer, and upload the
// result to a GL_TEXTURE_2D for on-screen preview. Does NOT talk to Spout directly -
// callers pull the CPU pixel buffer via GetPixels() and hand it to SpoutOutput.
#include <cstdint>
#include <vector>
#include "../dmx/DmxBuffer.h"
#include "../serializers/VrslSerializer.h"
#include "../render/PixelOps.h"
#include "../config/ShowConfig.h" // for DmxChannelRange

class FrameRenderer {
public:
    FrameRenderer();
    ~FrameRenderer();

    FrameRenderer(const FrameRenderer&) = delete;
    FrameRenderer& operator=(const FrameRenderer&) = delete;

    // (Re)allocates the pixel buffer and GL texture. Cheap no-op if the size is
    // already current. Must be called at least once before Render(), with a current
    // GL context bound.
    void SetResolution(int width, int height);

    // Re-serializes `dmx` through `serializer`, applying `maskedChannels`/`invertMask`/
    // `autoMaskOnZero`, clipped to `serializeUniverseCount` channels (mirrors the
    // masking/serialize loop in TextureWriter.cs), then uploads the result to the GL
    // texture. Call only when dirty (new DMX data, or a relevant setting changed) -
    // this is the expensive path the whole native port exists to avoid running every
    // frame unconditionally.
    void Render(const DmxBuffer& dmx, const VrslSerializer& serializer,
                const std::vector<DmxChannelRange>& maskedChannels, bool invertMask,
                bool autoMaskOnZero, int64_t serializeUniverseCount);

    unsigned int TextureId() const { return textureId_; }
    int Width() const { return width_; }
    int Height() const { return height_; }
    const std::vector<RGBA8>& Pixels() const { return pixels_; }

private:
    int width_ = 0;
    int height_ = 0;
    unsigned int textureId_ = 0;
    std::vector<RGBA8> pixels_;
    mutable std::vector<uint8_t> mergedDmx_;
};
