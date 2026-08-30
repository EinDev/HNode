#include "DmxBuffer.h"
#include <algorithm>
#include <cstring>

void DmxBuffer::SetUniverse(uint16_t universe, const uint8_t* data, size_t length) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& buf = universes_[universe];
    buf.fill(0);
    std::memcpy(buf.data(), data, std::min(length, kChannelsPerUniverse));
}

void DmxBuffer::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    universes_.clear();
}

void DmxBuffer::Merge(std::vector<uint8_t>& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    out.clear();
    if (universes_.empty()) return;

    uint16_t maxUniverse = 0;
    for (const auto& [universe, data] : universes_) {
        maxUniverse = std::max(maxUniverse, universe);
    }

    out.resize(static_cast<size_t>(maxUniverse + 1) * kChannelsPerUniverse, 0);
    for (const auto& [universe, data] : universes_) {
        std::memcpy(out.data() + static_cast<size_t>(universe) * kChannelsPerUniverse,
                    data.data(), kChannelsPerUniverse);
    }
}
