// Standalone CPU benchmark for HNode's DMX merge + serialize hot path - the same
// code FrameRenderer::Render() runs on every dirty frame, minus the final GL texture
// upload (which needs a real GPU context this tool deliberately avoids depending on,
// so it can run on a headless CI runner with no display/GPU driver). Has ZERO
// dependencies beyond the C++ standard library plus DmxBuffer.cpp/VrslSerializer.cpp
// themselves - no vcpkg, no GLFW, no ImGui, no Spout - so native/build_bench.bat
// builds it in seconds.
//
// WHY THE CALIBRATION STEP:
// CI runners are shared, variable-speed machines - a run on a busy/throttled runner
// and a run on a fresh one can differ by a wide margin for reasons that have nothing
// to do with whether HNode's own code got faster or slower. Reporting a bare
// "serialize took 4.2ms" is close to meaningless run-over-run. Instead, this tool
// times a fixed, deterministic, CPU-bound reference workload (RunCalibration below)
// on the SAME machine in the SAME run, immediately before timing HNode's actual
// code, and reports the RATIO of the two. Since both numbers scale with however much
// CPU throughput is available *right now* together, the ratio is - to a first
// approximation - independent of which runner you land on or how busy it is.
//
// Track the "ratio" field over time (e.g. across CI runs), not the raw millisecond
// figures - a rising ratio means HNode's code got relatively slower; the raw ms
// numbers are only useful for sanity-checking on one fixed machine (e.g. locally).
#include "../dmx/DmxBuffer.h"
#include "../serializers/VrslSerializer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

double ElapsedMs(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

// Deterministic, CPU-bound reference workload: `total` feeds the printed output, so
// the optimizer can't eliminate the loop. sqrt() is a reasonable proxy for "general
// floating-point throughput", which is roughly what the real workload below also
// stresses (the gamma/lerp math in VrslSerializer::SerializeChannel).
double RunCalibration(int iterations) {
    double total = 0.0;
    for (int i = 0; i < iterations; ++i) {
        total += std::sqrt(static_cast<double>(i % 9973) + 1.0);
    }
    return total;
}

std::vector<uint8_t> MakeSyntheticUniverse(uint32_t seed) {
    std::vector<uint8_t> data(DmxBuffer::kChannelsPerUniverse);
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> dist(0, 255);
    for (auto& b : data) b = static_cast<uint8_t>(dist(rng));
    return data;
}

} // namespace

int main() {
    constexpr int kCalibrationIterations = 40'000'000;
    constexpr int kUniverseCount = 32;   // a mid-size show
    constexpr int kMergeIterations = 2000;
    constexpr int kSerializeIterations = 200;
    constexpr int kTextureWidth = 1920;
    constexpr int kTextureHeight = 1080;

    // --- Calibration ---
    auto calStart = Clock::now();
    double calibrationSink = RunCalibration(kCalibrationIterations);
    auto calEnd = Clock::now();
    double calibrationMs = ElapsedMs(calStart, calEnd);

    // --- DmxBuffer::Merge throughput ---
    DmxBuffer dmx;
    for (int u = 0; u < kUniverseCount; ++u) {
        std::vector<uint8_t> universeData = MakeSyntheticUniverse(static_cast<uint32_t>(u + 1));
        dmx.SetUniverse(static_cast<uint16_t>(u), universeData.data(), universeData.size());
    }

    std::vector<uint8_t> merged;
    auto mergeStart = Clock::now();
    for (int i = 0; i < kMergeIterations; ++i) {
        dmx.Merge(merged);
    }
    auto mergeEnd = Clock::now();
    double mergeMs = ElapsedMs(mergeStart, mergeEnd);

    // --- VRSL serialize throughput (the actual per-channel hot loop
    // FrameRenderer::Render() runs, minus the final GL texture upload) ---
    VrslSerializer serializer;
    std::vector<RGBA8> pixels(static_cast<size_t>(kTextureWidth) * static_cast<size_t>(kTextureHeight));
    size_t channelCount = merged.size();

    auto serializeStart = Clock::now();
    for (int pass = 0; pass < kSerializeIterations; ++pass) {
        std::fill(pixels.begin(), pixels.end(), RGBA8{});
        for (size_t ch = 0; ch < channelCount; ++ch) {
            serializer.SerializeChannel(pixels, merged[ch], static_cast<int>(ch), kTextureWidth, kTextureHeight,
                                         /*autoMaskOnZero=*/false);
        }
    }
    auto serializeEnd = Clock::now();
    double serializeMs = ElapsedMs(serializeStart, serializeEnd);

    double workloadMs = mergeMs + serializeMs;
    double ratio = workloadMs / calibrationMs;

    std::printf("HNode perf baseline\n");
    std::printf("  calibration:    %.2f ms  (%d sqrt iterations, sink=%.3f)\n", calibrationMs,
                kCalibrationIterations, calibrationSink);
    std::printf("  dmx merge:      %.2f ms  (%d iterations, %d universes)\n", mergeMs, kMergeIterations,
                kUniverseCount);
    std::printf("  vrsl serialize: %.2f ms  (%d passes, %zu channels/pass)\n", serializeMs, kSerializeIterations,
                channelCount);
    std::printf("  workload total: %.2f ms\n", workloadMs);
    std::printf("  RATIO (workload/calibration): %.6f   <-- track this number over time, not the raw ms above\n",
                ratio);

    std::printf(
        "\nBENCH_JSON:{\"calibrationMs\":%.4f,\"dmxMergeMs\":%.4f,\"serializeMs\":%.4f,\"workloadMs\":%.4f,"
        "\"ratio\":%.6f,\"channelCount\":%zu}\n",
        calibrationMs, mergeMs, serializeMs, workloadMs, ratio, channelCount);

    return 0;
}
