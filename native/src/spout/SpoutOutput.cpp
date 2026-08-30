#include "SpoutOutput.h"

// Vendored Spout2 SDK. This project compiles the Spout .cpp sources directly into
// the app (no DLL build), so SPOUT_DLLEXP expands to nothing and SpoutSender is just
// a plain class. Also brings in GL/gl.h (via SpoutGLextensions.h) for GLenum/GLuint
// and GL_RGBA - no separate GL header needed here.
#include "../../third_party/Spout/SpoutSender.h"

struct SpoutOutput::Impl {
    SpoutSender sender;
    std::string name;
};

SpoutOutput::SpoutOutput() : impl_(new Impl()) {
}

SpoutOutput::~SpoutOutput() {
    Release();
    delete impl_;
    impl_ = nullptr;
}

void SpoutOutput::SetName(const std::string& name) {
    if (name == impl_->name) {
        return;
    }
    // Release any existing sender before renaming so we don't leave a stale sender
    // registered under the old name.
    impl_->sender.ReleaseSender();
    impl_->sender.SetSenderName(name.c_str());
    impl_->name = name;
}

bool SpoutOutput::SendFrame(const uint8_t* rgba8Pixels, unsigned int width, unsigned int height) {
    // bInvert = true: the incoming buffer is top-down (row 0 = top), while Spout/OpenGL's
    // native image convention is bottom-up, so the SDK needs to flip it while sending.
    return impl_->sender.SendImage(rgba8Pixels, width, height, GL_RGBA, /*bInvert=*/true, /*HostFBO=*/0);
}

void SpoutOutput::Release() {
    if (impl_) {
        impl_->sender.ReleaseSender();
    }
}
