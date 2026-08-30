#pragma once
// Common interface for all DMX->pixel serializers, matching the shape of
// Assets/Plugin/Serializers/IDMXSerializer.cs's SerializeChannel/DeserializeChannel/
// InitFrame/CompleteFrame. Introduced so ShowConfig/UiPanel/FrameRenderer can pick a
// serializer at runtime the way UIController.cs's serializer dropdown does, instead of
// Phase 1's single hardcoded VrslSerializer.
#include <cstdint>
#include <vector>
#include "../render/PixelOps.h"

class ISerializer {
public:
    virtual ~ISerializer() = default;

    // Stable name used both as this serializer's YAML tag (e.g. "!VRSL") and as its
    // entry in the UI dropdown - must match the reference C# class name exactly for
    // .shwcfg compatibility (see the Unity class names in Assets/Plugin/Serializers).
    virtual const char* Name() const = 0;

    // Called once at the start of each rendered frame, before any SerializeChannel
    // calls - mirrors IDMXSerializer.InitFrame. Most serializers are stateless and
    // don't need this (default no-op); a few (Spiral, FuralitySomna) reset per-frame
    // walk/offset state here.
    virtual void InitFrame() {}

    virtual void SerializeChannel(std::vector<RGBA8>& pixels, uint8_t channelValue, int channel,
                                   int textureWidth, int textureHeight, bool autoMaskOnZero) = 0;

    // Inverse of SerializeChannel: reads one DMX channel's value back out of a
    // received Spout input frame - mirrors IDMXSerializer.DeserializeChannel, which
    // takes a Texture2D; this takes the same flat RGBA8 pixel buffer SerializeChannel
    // writes into (see native/src/spout/SpoutInput.h for how that buffer is produced).
    // Only VRSL, Binary, and MDMX have a real implementation, matching the C#
    // reference - the other serializers throw NotImplementedException there (Ternary's
    // is present but explicitly broken per an in-source comment), so this default
    // (return 0, i.e. no-op) is intentionally the norm, not a gap - see
    // native/README.md.
    virtual uint8_t DeserializeChannel(const std::vector<RGBA8>& pixels, int channel, int textureWidth,
                                        int textureHeight) {
        (void)pixels; (void)channel; (void)textureWidth; (void)textureHeight;
        return 0;
    }

    // Called once after all channels for the frame have gone through SerializeChannel -
    // mirrors IDMXSerializer.CompleteFrame. Default no-op; MDMX uses this to draw its
    // per-column CRC blocks.
    virtual void CompleteFrame(std::vector<RGBA8>& pixels, const std::vector<uint8_t>& channelValues,
                                int textureWidth, int textureHeight) {
        (void)pixels; (void)channelValues; (void)textureWidth; (void)textureHeight;
    }

    // Draws this serializer's own ImGui settings controls (mirrors
    // IDMXSerializer.ConstructUserInterface's per-serializer widgets, e.g. VRSL's
    // gamma/RGB-grid/output-config controls). Returns true if any control was edited
    // this frame. Default: no controls.
    virtual bool DrawUi() { return false; }
};
