#pragma once
// Port of Assets/Plugin/Exporters/FrameSnapshotExporter.cs - listens on UDP port 9123
// for a JSON "save_frame" command ({command, frame_number, file_path, response_port}),
// and on the next rendered frame, encodes the current pixels as PNG and writes them to
// `file_path`, then sends a JSON status response back to the sender's IP on
// `response_port`.
#include "IExporter.h"
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

class FrameSnapshotExporter : public IExporter {
public:
    FrameSnapshotExporter();
    ~FrameSnapshotExporter() override;

    FrameSnapshotExporter(const FrameSnapshotExporter&) = delete;
    FrameSnapshotExporter& operator=(const FrameSnapshotExporter&) = delete;

    const char* Name() const override { return "FrameSnapshotExporter"; }

    // Starts the UDP command-listener thread on port 9123.
    void Construct() override;
    // Stops the listener thread.
    void Deconstruct() override;

    // If a save was queued by an incoming command, encodes `pixels` as PNG to the
    // requested path and sends the response. No-op otherwise. Note this only actually
    // fires when FrameRenderer::Render() has run (i.e. the frame was dirty) - see
    // main.cpp's `RequestDirty` wiring: this exporter's UDP thread must be able to
    // force a fresh render before saving, since the last-rendered pixels could
    // otherwise be stale. See SetDirtyCallback().
    void FrameRendered(const std::vector<RGBA8>& pixels, int width, int height) override;

    // main.cpp calls this once at startup with a callback that marks the render loop
    // dirty and wakes it (glfwPostEmptyEvent-equivalent) - the UDP listener thread
    // calls it when a save_frame command arrives, so a fresh frame actually gets
    // rendered (and thus FrameRendered() called) promptly instead of only on the next
    // unrelated DMX change.
    void SetDirtyCallback(std::function<void()> callback) override { requestDirty_ = std::move(callback); }

private:
    void ListenLoop();
    void SendResponse(const std::string& status, const std::string& error);

    std::thread thread_;
    std::atomic<bool> stopRequested_{false};
    std::uintptr_t socketHandle_ = 0; // SOCKET, type-erased to keep winsock out of this header

    std::mutex mutex_;
    bool saveQueued_ = false;
    int pendingFrameNumber_ = 0;
    std::string pendingFilePath_;
    std::string pendingResponseAddress_;
    int pendingResponsePort_ = 0;

    std::function<void()> requestDirty_;
};
