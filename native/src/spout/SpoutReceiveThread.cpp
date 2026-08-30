#include "SpoutReceiveThread.h"

#include <chrono>

#include <GLFW/glfw3.h>

SpoutReceiveThread::SpoutReceiveThread() = default;

SpoutReceiveThread::~SpoutReceiveThread() {
    Stop();
}

void SpoutReceiveThread::Start(GLFWwindow* sharedContextWindow) {
    thread_ = std::thread([this, sharedContextWindow]() { Run(sharedContextWindow); });
}

void SpoutReceiveThread::SetName(const std::string& name) {
    std::lock_guard<std::mutex> lock(nameMutex_);
    pendingName_ = name;
    nameChanged_ = true;
}

void SpoutReceiveThread::SetEnabled(bool enabled) {
    enabled_.store(enabled);
}

void SpoutReceiveThread::SetCallback(FrameCallback callback) {
    callback_ = std::move(callback);
}

bool SpoutReceiveThread::TryGetLatestFrame(std::vector<RGBA8>& outPixels, int& outWidth, int& outHeight) {
    std::lock_guard<std::mutex> lock(frameMutex_);
    if (!hasNewFrame_) return false;
    outPixels = std::move(latestFrame_);
    outWidth = latestWidth_;
    outHeight = latestHeight_;
    hasNewFrame_ = false;
    return true;
}

void SpoutReceiveThread::Stop() {
    if (!thread_.joinable()) return;
    stopRequested_ = true;
    thread_.join();
}

void SpoutReceiveThread::Run(GLFWwindow* sharedContextWindow) {
    glfwMakeContextCurrent(sharedContextWindow);

    // Constructed here (not passed in) so SpoutReceiver's own GL/DX interop setup
    // happens with THIS thread's context current - same reasoning as
    // SpoutSendThread's SpoutOutput construction.
    SpoutInput spoutInput;

    while (!stopRequested_) {
        {
            std::lock_guard<std::mutex> lock(nameMutex_);
            if (nameChanged_) {
                spoutInput.SetName(pendingName_);
                nameChanged_ = false;
            }
        }

        if (!enabled_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        std::vector<RGBA8> pixels;
        int width = 0;
        int height = 0;
        if (spoutInput.ReceiveFrame(pixels, width, height)) {
            {
                std::lock_guard<std::mutex> lock(frameMutex_);
                latestFrame_ = std::move(pixels);
                latestWidth_ = width;
                latestHeight_ = height;
                hasNewFrame_ = true;
            }
            if (callback_) callback_();
        }

        // ~60Hz poll cadence - Spout senders rarely exceed that, and this thread has
        // nothing else to do between polls.
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    spoutInput.Release();
    glfwMakeContextCurrent(nullptr);
}
