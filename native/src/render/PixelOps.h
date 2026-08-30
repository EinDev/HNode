#pragma once
// Pixel-block helpers shared by the frame renderer and serializers.
// Ported from Assets/TextureWriter.cs (PixelToIndex, MakeColorBlock, MixColorBlock)
// and Assets/Util.cs (GetBlockAlpha).
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <vector>

struct RGBA8 {
    uint8_t r = 0, g = 0, b = 0, a = 0;
};

enum class ColorChannel { Red, Green, Blue };

// Converts (x, y) with y=0 at the bottom (texture space) to a flat index into a
// width*height RGBA8 buffer stored top-down, or -1 if out of bounds.
// Mirrors TextureWriter.PixelToIndex, including its Y-flip.
inline int PixelToIndex(int x, int y, int textureWidth, int textureHeight) {
    if (x < 0 || x >= textureWidth || y < 0 || y >= textureHeight) return -1;
    y = textureHeight - 1 - y;
    return y * textureWidth + x;
}

// Fills a size x size block starting at (x, y) with a solid color.
// Mirrors TextureWriter.MakeColorBlock.
inline void MakeColorBlock(std::vector<RGBA8>& pixels, int x, int y, RGBA8 color, int size,
                            int textureWidth, int textureHeight) {
    for (int i = 0; i < size; ++i) {
        for (int j = 0; j < size; ++j) {
            int index = PixelToIndex(x + i, y + j, textureWidth, textureHeight);
            if (index == -1) return;
            pixels[static_cast<size_t>(index)] = color;
        }
    }
}

// Writes `channelValue` into a single color channel of a size x size block,
// forcing alpha to 255. Mirrors TextureWriter.MixColorBlock.
inline void MixColorBlock(std::vector<RGBA8>& pixels, int x, int y, uint8_t channelValue,
                           ColorChannel channel, int size, int textureWidth, int textureHeight) {
    for (int i = 0; i < size; ++i) {
        for (int j = 0; j < size; ++j) {
            int index = PixelToIndex(x + i, y + j, textureWidth, textureHeight);
            if (index == -1) return;
            RGBA8& p = pixels[static_cast<size_t>(index)];
            switch (channel) {
                case ColorChannel::Red:   p.r = channelValue; break;
                case ColorChannel::Green: p.g = channelValue; break;
                case ColorChannel::Blue:  p.b = channelValue; break;
            }
            p.a = 255;
        }
    }
}

// Mirrors Util.GetBlockAlpha: forces alpha transparent when auto-masking zero-value
// channels is enabled, otherwise fully opaque.
inline uint8_t GetBlockAlpha(uint8_t channelValue, bool autoMaskOnZero) {
    if (autoMaskOnZero && channelValue == 0) return 0;
    return 255;
}

// Unity's Color.linear: converts a single sRGB (gamma space) component in [0,1] to
// linear space using the standard sRGB EOTF. Used by VRSL's GammaCorrection toggle.
inline float SrgbToLinear(float c) {
    if (c <= 0.04045f) return c / 12.92f;
    return powf((c + 0.055f) / 1.055f, 2.4f);
}

// Unity's Color.gamma: the inverse of SrgbToLinear above (linear -> sRGB gamma
// space). Used by VRSL's DeserializeChannel (Spout input), mirroring
// TextureReader.GetColor's `.gamma` call.
inline float LinearToSrgb(float c) {
    if (c <= 0.0031308f) return c * 12.92f;
    return 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
}

// Reads a single pixel at (x, y) with the same bottom-up Y-flip convention
// TextureReader.GetColor uses (Assets/TextureReader.cs) - the inverse read of what
// MakeColorBlock/MixColorBlock above write. Returns a transparent-black RGBA8 if
// (x, y) is out of bounds, matching PixelToIndex's -1 sentinel.
inline RGBA8 GetPixelColor(const std::vector<RGBA8>& pixels, int x, int y, int textureWidth, int textureHeight) {
    int index = PixelToIndex(x, y, textureWidth, textureHeight);
    if (index == -1) return RGBA8{};
    return pixels[static_cast<size_t>(index)];
}
