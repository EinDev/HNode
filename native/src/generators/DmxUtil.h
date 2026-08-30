#pragma once
// C++ equivalents of the List<byte> extension methods generators rely on
// (Assets/Extensions.cs's EnsureCapacity, Assets/Util.cs's WriteToListAtPosition).
#include <cstddef>
#include <cstdint>
#include <vector>

// Grows `data` with zero-value bytes if it's smaller than `capacity`. No-op if already
// large enough (mirrors EnsureCapacity<T> - it only ever grows, never shrinks).
inline void EnsureDmxCapacity(std::vector<uint8_t>& data, size_t capacity) {
    if (data.size() < capacity) data.resize(capacity, 0);
}

// Writes `source` into `dest` starting at `position`, growing `dest` first if needed.
// Mirrors Util.WriteToListAtPosition.
inline void WriteDmxAtPosition(std::vector<uint8_t>& dest, const std::vector<uint8_t>& source, size_t position) {
    EnsureDmxCapacity(dest, position + source.size());
    for (size_t i = 0; i < source.size(); ++i) {
        dest[position + i] = source[i];
    }
}
