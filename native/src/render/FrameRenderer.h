#pragma once
// CPU pixel buffer -> GL preview texture, equivalent to the parts of
// Assets/TextureWriter.cs that Phase 1 needs: merge already-received DMX (via
// DmxBuffer), apply channel masking, run it through VrslSerializer, and upload the
// result to a GL_TEXTURE_2D for on-screen preview. Does NOT talk to Spout directly -
// callers pull the CPU pixel buffer via GetPixels() and hand it to SpoutOutput.
#include <cstdint>
#include <vector>
#include <memory>
#include "../dmx/DmxBuffer.h"
#include "../serializers/ISerializer.h"
#include "../generators/IGenerator.h"
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

    // Merges `dmx`, runs it through `generators` in order (mirrors
    // TextureWriter.cs's ordering: merge -> generators -> serializer), then
    // re-serializes through `serializer`, applying `maskedChannels`/`invertMask`/
    // `autoMaskOnZero`, clipped to `serializeUniverseCount` channels, and uploads the
    // result to the GL texture. Call only when dirty (new DMX data, a relevant
    // setting changed, or an animated generator's per-frame tick) - this is the
    // expensive path the whole native port exists to avoid running unconditionally.
    void Render(const DmxBuffer& dmx, const std::vector<std::unique_ptr<IGenerator>>& generators,
                ISerializer& serializer, const std::vector<DmxChannelRange>& maskedChannels,
                bool invertMask, bool autoMaskOnZero, int64_t serializeUniverseCount);

    unsigned int TextureId() const { return textureId_; }
    int Width() const { return width_; }
    int Height() const { return height_; }
    const std::vector<RGBA8>& Pixels() const { return pixels_; }

    // The flat, merged DMX buffer from the most recent Render() call - exporters
    // (e.g. MidiDmxExporter) that need the same per-frame channel data run off this
    // instead of re-merging DmxBuffer themselves.
    const std::vector<uint8_t>& MergedDmx() const { return mergedDmx_; }

private:
    int width_ = 0;
    int height_ = 0;
    unsigned int textureId_ = 0;
    std::vector<RGBA8> pixels_;
    std::vector<uint8_t> mergedDmx_;
};
