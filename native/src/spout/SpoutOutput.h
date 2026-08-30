#pragma once
// Thin wrapper around the vendored Spout2 SDK (native/third_party/Spout, SpoutGL's
// SpoutSender class) - equivalent role to Klak.Spout's SpoutSender component that
// Assets/TextureWriter.cs sends the finished frame texture through.
//
// Must be constructed/used only while a current OpenGL context exists (same thread
// as the GLFW window's context), since Spout's sender uses GL/DX interop internally.
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

    // Sends an RGBA8, top-down (row 0 = top of image) pixel buffer of width*height*4
    // bytes as this frame's Spout output, creating/recreating the sender as needed if
    // the name or resolution changed since the last send. Returns false on failure.
    bool SendFrame(const uint8_t* rgba8Pixels, unsigned int width, unsigned int height);

    void Release();

private:
    struct Impl;
    Impl* impl_;
};
