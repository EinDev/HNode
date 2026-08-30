#pragma once
// Rolling performance stats for the UI's "Nerdy statistics" panel (matches the
// root README's feature list and TextureWriter.cs's on-screen frame-time text:
// "Serialization Time: X ms" / "DMX Channels: N" / "Data Throughput: X/s") plus a
// render-time history graph, which the old Unity app got for free from the Graphy
// asset (see Assets/Dependencies/Graphy - Ultimate Stats Monitor). Owned by main.cpp,
// updated once per render and once per ArtNet packet, read by UiPanel.
#include <cstddef>

struct PerfStats {
    static constexpr int kHistorySize = 180; // ~last 45s of history at a 4Hz idle tick, denser while active

    float renderTimeHistoryMs[kHistorySize] = {};
    int historyWriteIndex = 0;
    int historyCount = 0; // clamps at kHistorySize; lets the graph avoid plotting unwritten zero-slots at startup

    float lastRenderTimeMs = 0.0f;
    size_t lastDmxChannelCount = 0;
    double dataThroughputBytesPerSecond = 0.0;
    double artNetPacketsPerSecond = 0.0;

    void PushRenderSample(float ms, size_t dmxChannelCount) {
        renderTimeHistoryMs[historyWriteIndex] = ms;
        historyWriteIndex = (historyWriteIndex + 1) % kHistorySize;
        if (historyCount < kHistorySize) historyCount++;
        lastRenderTimeMs = ms;
        lastDmxChannelCount = dmxChannelCount;
    }
};
