#include "FrameRenderer.h"
#include <algorithm>
#include <cstring>
#include <windows.h>
#include <GL/gl.h>

// GL_CLAMP_TO_EDGE is GL 1.2; the legacy <GL/gl.h> shipped with the Windows SDK only
// declares up to GL 1.1, so it's not defined there even though every driver supports it.
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

FrameRenderer::FrameRenderer() = default;

FrameRenderer::~FrameRenderer() {
    if (textureId_ != 0) {
        GLuint id = textureId_;
        glDeleteTextures(1, &id);
    }
}

void FrameRenderer::SetResolution(int width, int height) {
    if (width == width_ && height == height_ && textureId_ != 0) return;

    width_ = width;
    height_ = height;
    pixels_.assign(static_cast<size_t>(width) * static_cast<size_t>(height), RGBA8{});

    if (textureId_ == 0) {
        GLuint id;
        glGenTextures(1, &id);
        textureId_ = id;
    }

    glBindTexture(GL_TEXTURE_2D, textureId_);
    // FilterMode.Point / TextureWrapMode.Clamp equivalent (TextureWriter.cs Start()).
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels_.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

std::vector<uint8_t> FrameRenderer::ComputeTranscodedDmx(const TranscodeInput& transcodeInput) {
    // std::max<int>(...) - not a bare std::max() call, which windows.h's function-like
    // `max` macro (no NOMINMAX defined project-wide) would corrupt; the explicit
    // template argument makes the token after "max" a "<" instead of "(", which the
    // macro doesn't match. Same trick this file's pre-existing std::min<int64_t>
    // call below already relies on.
    std::vector<uint8_t> dmxData(static_cast<size_t>(std::max<int>(0, transcodeInput.universeCount)) * 512, 0);
    if (!transcodeInput.deserializer) return dmxData;

    static const std::vector<RGBA8> kEmptyPixels;
    const std::vector<RGBA8>& pixels = transcodeInput.pixels ? *transcodeInput.pixels : kEmptyPixels;

    for (size_t i = 0; i < dmxData.size(); ++i) {
        dmxData[i] = transcodeInput.deserializer->DeserializeChannel(pixels, static_cast<int>(i), transcodeInput.width,
                                                                       transcodeInput.height);
    }
    return dmxData;
}

void FrameRenderer::Render(const DmxBuffer& dmx, const std::vector<std::unique_ptr<IGenerator>>& generators,
                            ISerializer& serializer, const std::vector<DmxChannelRange>& maskedChannels,
                            bool invertMask, bool autoMaskOnZero, int64_t serializeUniverseCount,
                            const TranscodeInput& transcodeInput) {
    if (width_ == 0 || height_ == 0) return;

    // Fill with transparent, matching TextureWriter.Update()'s Array.Clear(pixels, ...).
    std::fill(pixels_.begin(), pixels_.end(), RGBA8{});

    // Mirrors TextureWriter.cs's Transcode/MergeTranscode branches exactly: a
    // transcode-without-merge REPLACES the ArtNet merge entirely (the ArtNet merge
    // doesn't even run - matches the C#'s `if (...) {...} else if (dmxManager...) {...}`
    // being mutually exclusive), while a transcode-with-merge runs both and combines.
    if (transcodeInput.transcode && !transcodeInput.mergeTranscode) {
        mergedDmx_ = ComputeTranscodedDmx(transcodeInput);
    } else {
        dmx.Merge(mergedDmx_);
    }

    if (transcodeInput.transcode && transcodeInput.mergeTranscode) {
        std::vector<uint8_t> transcodedValues = ComputeTranscodedDmx(transcodeInput);
        // Channel-wise max() merge, extending mergedDmx_ if the transcoded data is
        // longer - matches TextureWriter.cs's loop. (Indices where only mergedDmx_
        // already has a value are left untouched, matching the C# reference exactly -
        // no need to explicitly handle that case since we simply don't touch it here.)
        if (mergedDmx_.size() < transcodedValues.size()) {
            mergedDmx_.resize(transcodedValues.size(), 0);
        }
        for (size_t i = 0; i < transcodedValues.size(); ++i) {
            mergedDmx_[i] = std::max<uint8_t>(transcodedValues[i], mergedDmx_[i]);
        }
    }

    for (const auto& generator : generators) {
        generator->GenerateDMX(mergedDmx_);
    }
    serializer.InitFrame();

    int64_t channelsToSerialize = std::min<int64_t>(serializeUniverseCount * 512,
                                                      static_cast<int64_t>(mergedDmx_.size()));

    for (int64_t i = 0; i < channelsToSerialize; ++i) {
        bool isMasked = false;
        for (const auto& range : maskedChannels) {
            if (range.Contains(static_cast<int>(i))) {
                isMasked = true;
                break;
            }
        }
        if (invertMask) isMasked = !isMasked;
        if (isMasked) continue;

        serializer.SerializeChannel(pixels_, mergedDmx_[static_cast<size_t>(i)],
                                     static_cast<int>(i), width_, height_, autoMaskOnZero);
    }

    serializer.CompleteFrame(pixels_, mergedDmx_, width_, height_);

    glBindTexture(GL_TEXTURE_2D, textureId_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, pixels_.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}
