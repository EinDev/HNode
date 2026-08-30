#include "SpoutInput.h"
#include "../render/PixelOps.h"

// Vendored Spout2 SDK - see SpoutOutput.cpp's identical comment about why this can
// include the SDK header directly (no DLL build, SPOUT_DLLEXP expands to nothing).
#include "../../third_party/Spout/SpoutReceiver.h"

struct SpoutInput::Impl {
    SpoutReceiver receiver;
    std::string name;
    // Always non-empty (1x1 minimum) so .data() is never null when passed to
    // ReceiveImage() - resized reactively to the real sender dimensions below.
    std::vector<RGBA8> buffer{RGBA8{}};
};

SpoutInput::SpoutInput() : impl_(new Impl()) {
}

SpoutInput::~SpoutInput() {
    Release();
    delete impl_;
    impl_ = nullptr;
}

void SpoutInput::SetName(const std::string& name) {
    if (name == impl_->name) {
        return;
    }
    impl_->receiver.ReleaseReceiver();
    impl_->receiver.SetReceiverName(name.empty() ? nullptr : name.c_str());
    impl_->name = name;
}

bool SpoutInput::ReceiveFrame(std::vector<RGBA8>& outPixels, int& outWidth, int& outHeight) {
    // RGBA8 is exactly 4 bytes (r,g,b,a), matching GL_RGBA/GL_UNSIGNED_BYTE layout -
    // reinterpret the buffer directly rather than round-tripping through a separate
    // byte array.
    bool received = impl_->receiver.ReceiveImage(reinterpret_cast<unsigned char*>(impl_->buffer.data()), GL_RGBA,
                                                  /*bInvert=*/false, /*HostFbo=*/0);
    if (!received) return false;

    if (impl_->receiver.IsUpdated()) {
        // Sender connected or changed size - resize the buffer to match and skip this
        // cycle. The buffer contents right now are against the OLD size (or garbage on
        // first connect), not meaningful pixel data - matches the standard Spout SDK
        // receive pattern (TextureReader.cs sidesteps this entirely by always sizing
        // its Texture2D to a fixed configured resolution rather than the sender's
        // actual size, but that requires the sender to already match that fixed size
        // to receive correctly at all - sizing to the real sender dimensions here is
        // more robust and avoids that implicit assumption).
        unsigned int width = impl_->receiver.GetSenderWidth();
        unsigned int height = impl_->receiver.GetSenderHeight();
        size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
        if (pixelCount == 0) pixelCount = 1; // keep the 1x1 minimum invariant
        impl_->buffer.assign(pixelCount, RGBA8{});
        return false;
    }

    if (!impl_->receiver.IsConnected()) return false;
    if (!impl_->receiver.IsFrameNew()) return false;

    outWidth = static_cast<int>(impl_->receiver.GetSenderWidth());
    outHeight = static_cast<int>(impl_->receiver.GetSenderHeight());
    if (outWidth <= 0 || outHeight <= 0) return false;

    outPixels = impl_->buffer;
    return true;
}

void SpoutInput::Release() {
    if (impl_) {
        impl_->receiver.ReleaseReceiver();
    }
}
