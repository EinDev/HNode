#pragma once
// Flat per-universe DMX storage and merge, equivalent to ArtNet-Unity's DmxManager
// plus the merge loop in Assets/TextureWriter.cs (TextureWriter.Update, lines ~71-98).
#include <array>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

class DmxBuffer {
public:
    static constexpr size_t kChannelsPerUniverse = 512;

    // Thread-safe: called from the ArtNet receive thread.
    void SetUniverse(uint16_t universe, const uint8_t* data, size_t length);

    void Clear();

    // Flattens universes [0 .. maxUniverse] into `out`, 512 bytes each, zero-filling
    // any universe that has never been received. Mirrors the merge loop that builds
    // mergedDmxValues in TextureWriter.cs.
    void Merge(std::vector<uint8_t>& out) const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<uint16_t, std::array<uint8_t, kChannelsPerUniverse>> universes_;
};
