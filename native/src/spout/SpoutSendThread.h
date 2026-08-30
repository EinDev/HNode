#pragma once
// Runs SpoutOutput::SendFrame (and SetName) on a dedicated thread with its own GL
// context - sharing objects (textures, etc.) with the main window's context via
// GLFW's window-sharing constructor parameter - so Spout's internal CheckAccess()
// mutex wait (up to ~67ms if OBS/the receiver is slow to release the shared texture)
// can no longer stall the main thread, which also has to stay responsive to incoming
// ArtNet DMX and UI input. See the Spout-delay investigation this follows on from -
// SpoutOutput::SendFrame's move to SendTexture() fixed the duplicate-upload half of
// that latency; this addresses the remaining mutex-wait half.
//
// Only ONE frame is ever "in flight": if a new frame is submitted before the previous
// SendFrame call has finished, the new one simply replaces the pending one - Spout
// only cares about delivering the latest frame, there's no reason to queue and send
// stale ones once a newer frame exists.
#include <mutex>
#include <condition_variable>
#include <string>
#include <thread>

#include "SpoutOutput.h"

struct GLFWwindow;

class SpoutSendThread {
public:
    SpoutSendThread();
    ~SpoutSendThread();

    SpoutSendThread(const SpoutSendThread&) = delete;
    SpoutSendThread& operator=(const SpoutSendThread&) = delete;

    // sharedContextWindow must be a hidden GLFWwindow created with the main window
    // passed as its `share` argument (glfwCreateWindow's 4th parameter) and must not
    // be current on any thread when this is called - responsibility for making it
    // current transfers to this class's worker thread until Stop() is called.
    void Start(GLFWwindow* sharedContextWindow);

    // Thread-safe. Queues a sender rename; applied before the next queued SendFrame.
    void SetName(const std::string& name);

    // Thread-safe, non-blocking. Hands off a rendered frame to send.
    //
    // The caller (main thread) must call glFlush() right after finishing its GL
    // writes to this texture (e.g. FrameRenderer's glTexSubImage2D upload) and before
    // calling this - per the OpenGL spec, objects shared between contexts in the same
    // share group are only guaranteed visible to another context/thread after the
    // writing context flushes AND that happens-before edge is communicated some other
    // way (here: the mutex/condvar handoff inside this call, which happens after the
    // caller's glFlush() returns).
    void SubmitFrame(unsigned int textureId, unsigned int width, unsigned int height);

    // Joins the worker thread and releases the Spout sender. Safe to call once, and
    // must happen before the GLFW window/context this was Start()-ed with is
    // destroyed.
    void Stop();

private:
    void Run(GLFWwindow* sharedContextWindow);

    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;

    bool hasName_ = false;
    std::string pendingName_;
    bool hasFrame_ = false;
    unsigned int pendingTextureId_ = 0;
    unsigned int pendingWidth_ = 0;
    unsigned int pendingHeight_ = 0;
    bool stop_ = false;
};
