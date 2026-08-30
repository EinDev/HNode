#pragma once
// Thin wrapper around the vendored Spout2 SDK (native/third_party/Spout, SpoutGL's
// SpoutSender class) - equivalent role to Klak.Spout's SpoutSender component that
// Assets/TextureWriter.cs sends the finished frame texture through.
//
// Must be constructed/used only while a current OpenGL context exists on the calling
// thread, since Spout's sender uses GL/DX interop internally. As of the SpoutSendThread
// change, that's a dedicated worker thread's context (sharing GL objects with the main
// window's context) rather than the main thread itself - see SpoutSendThread.h.
#include <cstdint>
#include <string>

// Forward-declared to avoid pulling GL headers into this header; implemented against
// a GLuint 2D texture (GL_TEXTURE_2D) matching FrameRenderer's output texture.
class SpoutOutput {
public:
    SpoutOutput();
    ~SpoutOutput();

    SpoutOutput(const SpoutOutput&) = delete;
    SpoutOutput& operator=(const SpoutOutput&) = delete;

    // (Re)sets the sender's public name. Safe to call at any time, including with the
    // sender already active (matches Loader.cs setting spoutSender.spoutName on every
    // show-config (re)load - Klak.Spout recreates the sender internally on name change).
    void SetName(const std::string& name);

    // Sends an existing GL_TEXTURE_2D (top-down, row 0 = top of image - matching
    // FrameRenderer's texture, which is exactly what this is meant to be called with)
    // as this frame's Spout output, creating/recreating the sender as needed if the
    // name or resolution changed since the last send. Returns false on failure.
    //
    // Takes a texture, not a raw CPU pixel buffer, specifically to avoid a redundant
    // CPU->GPU upload: FrameRenderer already uploads the rendered frame to a GL
    // texture for the on-screen preview, so re-uploading the same bytes again here
    // (as an earlier SendImage()-based version of this function did) doubled the
    // per-frame upload cost for no benefit, on the same thread that also has to stay
    // responsive to new ArtNet data - see the investigation behind this change for
    // the full latency analysis.
    bool SendFrame(unsigned int textureId, unsigned int width, unsigned int height);

    void Release();

private:
    struct Impl;
    Impl* impl_;
};
