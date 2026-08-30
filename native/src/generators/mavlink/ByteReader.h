#pragma once
// Minimal sequential byte-buffer reader, standing in for the C# reference's
// Queue<byte>-based DequeueChunk() pattern used throughout the MAVLinkDrone show-file
// parsing code (ShowFile.cs, Trajectory.cs, LightEvent.cs, PyroEvent.cs). Reads
// consume bytes from the front in the same order the reference does, just via a
// cursor over a fixed buffer instead of an actual queue - simpler/faster in C++, and
// no behavioral difference since the reference never re-queues bytes.
//
// Defensive-by-design: reading past the end returns zero bytes instead of throwing.
// The C# reference has no equivalent guard (Queue<byte>.Dequeue() throws on an empty
// queue) - but this parses untrusted, network-uploaded show-file bytes (via MAVLink
// FTP) on a background thread, so a truncated/malformed upload degrading to garbage
// output is a far better failure mode here than crashing the whole app.
#include <cstddef>
#include <cstdint>
#include <vector>

class ByteReader {
public:
    explicit ByteReader(const std::vector<uint8_t>& data) : data_(data), pos_(0) {}

    size_t Remaining() const { return pos_ < data_.size() ? data_.size() - pos_ : 0; }

    uint8_t ReadByte() {
        if (pos_ >= data_.size()) return 0;
        return data_[pos_++];
    }

    std::vector<uint8_t> ReadBytes(size_t count) {
        std::vector<uint8_t> out;
        size_t avail = Remaining();
        size_t n = count < avail ? count : avail;
        out.assign(data_.begin() + static_cast<ptrdiff_t>(pos_), data_.begin() + static_cast<ptrdiff_t>(pos_ + n));
        pos_ += n;
        while (out.size() < count) out.push_back(0); // pad on truncated input, see class comment
        return out;
    }

    int16_t ReadInt16LE() {
        auto b = ReadBytes(2);
        return static_cast<int16_t>(static_cast<uint16_t>(b[0]) | (static_cast<uint16_t>(b[1]) << 8));
    }

    uint16_t ReadUInt16LE() {
        auto b = ReadBytes(2);
        return static_cast<uint16_t>(static_cast<uint16_t>(b[0]) | (static_cast<uint16_t>(b[1]) << 8));
    }

    // Mirrors ShowFile.cs's static GetVarInt(): 7 data bits per byte, MSB = "more
    // bytes follow" continuation flag.
    int ReadVarInt() {
        int val = 0;
        int shift = 0;
        uint8_t b;
        do {
            b = ReadByte();
            val |= (static_cast<int>(b & 0x7F)) << shift;
            shift += 7;
            if (shift >= 32) break; // defensive: never spin forever on malformed/truncated input
        } while ((b & 0x80) != 0);
        return val;
    }

private:
    std::vector<uint8_t> data_;
    size_t pos_;
};
