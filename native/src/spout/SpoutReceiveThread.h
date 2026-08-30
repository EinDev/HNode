#pragma once
// Runs SpoutInput::ReceiveFrame (and SetName) on a dedicated thread with its own GL
// context - sharing objects with the main window's context via GLFW's window-sharing
// parameter, same pattern as SpoutSendThread (see that header for the full rationale:
// Spout's GL/DX interop internals can block, and that must never stall the main
// thread). Unlike SpoutSendThread, this is a continuous POLL loop rather than an
// event-driven queue - Spout receiving has no "wait for frame" push notification, the
// SDK is inherently poll-based (call ReceiveImage() periodically, check IsFrameNew()).
//
// Only enabled while explicitly told to be (mirrors Assets/TextureReader.cs's
// `Update()` early-out when `!Loader.showconf.Transcode` - no point polling a Spout
// receiver nobody wants input from).
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "SpoutInput.h"
#include "../render/PixelOps.h"

struct GLFWwindow;

class SpoutReceiveThread {
public:
    SpoutReceiveThread();
    ~SpoutReceiveThread();

    SpoutReceiveThread(const SpoutReceiveThread&) = delete;
    SpoutReceiveThread& operator=(const SpoutReceiveThread&) = delete;

    // sharedContextWindow must be a hidden GLFWwindow created with the main window
    // passed as its `share` argument, distinct from the one SpoutSendThread already
    // owns exclusively on its own thread - see main.cpp for where this is created.
    void Start(GLFWwindow* sharedContextWindow);

    // Thread-safe. Queues a receiver rename, applied before the next receive attempt.
    void SetName(const std::string& name);

    // Thread-safe. Enables/disables polling - matches config.transcode's on/off state.
    void SetEnabled(bool enabled);

    // Called from the RECEIVE thread (not the caller's thread) whenever a genuinely
    // new frame's pixels have been captured - intended to mark the main loop dirty and
    // wake it, mirroring ArtNetReceiver::SetCallback's contract exactly (see
    // ArtNetReceiver.h). Must be cheap/thread-safe. Set once, before Start().
    using FrameCallback = std::function<void()>;
    void SetCallback(FrameCallback callback);

    // Thread-safe. Copies the most recently received frame into the outputs and
    // returns true if a new one has arrived since the last successful call here;
    // otherwise returns false and leaves the outputs untouched. The main thread calls
    // this once per Render() to pick up whatever the receive thread captured.
    bool TryGetLatestFrame(std::vector<RGBA8>& outPixels, int& outWidth, int& outHeight);

    // Joins the worker thread and releases the Spout receiver. Safe to call once, and
    // must happen before the GLFW window/context this was Start()-ed with is
    // destroyed.
    void Stop();

private:
    void Run(GLFWwindow* sharedContextWindow);

    std::thread thread_;
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> enabled_{false};

    std::mutex nameMutex_;
    std::string pendingName_;
    bool nameChanged_ = false;

    std::mutex frameMutex_;
    std::vector<RGBA8> latestFrame_;
    int latestWidth_ = 0;
    int latestHeight_ = 0;
    bool hasNewFrame_ = false;

    FrameCallback callback_; // set once before Start(), never touched by the worker thread afterward
};
