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

void FrameRenderer::Render(const DmxBuffer& dmx, const VrslSerializer& serializer,
                            const std::vector<DmxChannelRange>& maskedChannels, bool invertMask,
                            bool autoMaskOnZero, int64_t serializeUniverseCount) {
    if (width_ == 0 || height_ == 0) return;

    // Fill with transparent, matching TextureWriter.Update()'s Array.Clear(pixels, ...).
    std::fill(pixels_.begin(), pixels_.end(), RGBA8{});

    dmx.Merge(mergedDmx_);

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

    glBindTexture(GL_TEXTURE_2D, textureId_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, pixels_.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}
