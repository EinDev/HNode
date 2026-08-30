#include "SpoutSendThread.h"

#include <GLFW/glfw3.h>

SpoutSendThread::SpoutSendThread() = default;

SpoutSendThread::~SpoutSendThread() {
    Stop();
}

void SpoutSendThread::Start(GLFWwindow* sharedContextWindow) {
    thread_ = std::thread([this, sharedContextWindow]() { Run(sharedContextWindow); });
}

void SpoutSendThread::SetName(const std::string& name) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingName_ = name;
        hasName_ = true;
    }
    cv_.notify_one();
}

void SpoutSendThread::SubmitFrame(unsigned int textureId, unsigned int width, unsigned int height) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingTextureId_ = textureId;
        pendingWidth_ = width;
        pendingHeight_ = height;
        hasFrame_ = true;
    }
    cv_.notify_one();
}

void SpoutSendThread::Stop() {
    if (!thread_.joinable()) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_one();
    thread_.join();
}

void SpoutSendThread::Run(GLFWwindow* sharedContextWindow) {
    glfwMakeContextCurrent(sharedContextWindow);

    // Constructed here (not passed in from the caller) so SpoutSender's own GL/DX
    // interop setup (SpoutGL's LoadGLextensions()/OpenSpout(), triggered from
    // SpoutOutput's constructor and first SendFrame) happens with THIS thread's
    // context current - matching SpoutOutput's "must be constructed/used only while a
    // current OpenGL context exists" contract, just against the shared context
    // instead of the main window's.
    SpoutOutput spoutOutput;

    for (;;) {
        std::string name;
        bool doName = false;
        unsigned int textureId = 0;
        unsigned int width = 0;
        unsigned int height = 0;
        bool doFrame = false;
        bool shouldStop = false;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() { return hasName_ || hasFrame_ || stop_; });
            if (hasName_) {
                name = pendingName_;
                doName = true;
                hasName_ = false;
            }
            if (hasFrame_) {
                textureId = pendingTextureId_;
                width = pendingWidth_;
                height = pendingHeight_;
                doFrame = true;
                hasFrame_ = false;
            }
            shouldStop = stop_;
        }

        // Drain whatever was pending even on the iteration that observes stop_, so a
        // rename/frame submitted right before Stop() isn't silently dropped.
        if (doName) {
            spoutOutput.SetName(name);
        }
        if (doFrame) {
            spoutOutput.SendFrame(textureId, width, height);
        }
        if (shouldStop) {
            break;
        }
    }

    spoutOutput.Release();
    glfwMakeContextCurrent(nullptr);
}
