#pragma once
// Thin wrapper around the vendored Spout2 SDK's SpoutReceiver class - the input-side
// mirror of SpoutOutput.h. Equivalent role to Klak.Spout's SpoutReceiver component
// that Assets/TextureReader.cs reads from, except this goes straight to CPU pixel
// bytes via the SDK's ReceiveImage() rather than the C# reference's GPU-texture ->
// RenderTexture -> Texture2D.ReadPixels() round trip - simpler, since this app's
// DeserializeChannel implementations only ever need raw pixel bytes (see
// ISerializer.h), never a GL texture handle.
//
// Must be constructed/used only while a current OpenGL context exists on the calling
// thread, since Spout's receiver uses GL/DX interop internally - same contract as
// SpoutOutput. See SpoutReceiveThread.h for why this runs on its own worker thread
// with its own shared GL context, same reasoning as SpoutSendThread.
#include <cstdint>
#include <string>
#include <vector>

struct RGBA8;

class SpoutInput {
public:
    SpoutInput();
    ~SpoutInput();

    SpoutInput(const SpoutInput&) = delete;
    SpoutInput& operator=(const SpoutInput&) = delete;

    // (Re)sets which sender name to connect to. Safe to call at any time. Passing an
    // empty name connects to whatever sender is currently "active" (matches the SDK's
    // SetReceiverName(nullptr) behavior, and Klak.Spout's sourceName="" convention).
    void SetName(const std::string& name);

    // Attempts to receive one frame. Returns true and fills outPixels/outWidth/
    // outHeight only when a genuinely new frame's pixels are ready (mirrors the SDK's
    // own IsFrameNew() semantics) - returns false (leaving outputs untouched) on a
    // dimension change/initial connect (the internal buffer needs a cycle to resize
    // before real pixel data is available), when not connected, or when the sender
    // hasn't produced a new frame since the last successful call.
    bool ReceiveFrame(std::vector<RGBA8>& outPixels, int& outWidth, int& outHeight);

    void Release();

private:
    struct Impl;
    Impl* impl_;
};
