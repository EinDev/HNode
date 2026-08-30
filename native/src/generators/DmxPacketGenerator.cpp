// Port of Assets/Plugin/Generators/GeneratorDMXPacket.cs (class `DMXPacket`).
#include "DmxPacketGenerator.h"

#include <iterator>
#include <map>

namespace {

// Mirrors the C# nested `DMXPacketInfo` struct.
struct DmxPacketInfo {
    uint16_t channelOffset = 0;
    uint8_t length = 0;
    std::vector<uint8_t> data;

    static constexpr int kMaxDataLength = 255; // byte.MaxValue

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> bytes;
        bytes.reserve(3 + data.size());
        bytes.push_back(static_cast<uint8_t>(channelOffset & 0xFF));        // low byte
        bytes.push_back(static_cast<uint8_t>((channelOffset >> 8) & 0xFF)); // high byte
        bytes.push_back(length);
        bytes.insert(bytes.end(), data.begin(), data.end());
        return bytes;
    }
};

} // namespace

void DmxPacketGenerator::GenerateDMX(std::vector<uint8_t>& dmxData) {
    // std::map keeps ascending-key iteration order, replicating the effective
    // insertion order of the C# Dictionary<int, byte> `diffs` in this method
    // (always built/copied via ascending-index loops - see header comment and
    // the porting notes this file was written from).
    std::map<int, uint8_t> diffs;

    // look for differences to last frame
    for (size_t i = 0; i < dmxData.size(); ++i) {
        if (i >= lastFrameData_.size() || dmxData[i] != lastFrameData_[i]) {
            diffs[static_cast<int>(i)] = dmxData[i];
        }
    }

    // if no diffs, use the last diff
    if (diffs.empty()) {
        for (const auto& kv : lastDiff_) {
            diffs[kv.first] = kv.second;
        }
    } else {
        lastDiff_.clear();
        for (const auto& kv : diffs) {
            lastDiff_[kv.first] = kv.second;
        }
    }

    // get a set of "packets" based on the diffs
    std::vector<DmxPacketInfo> packets;

    // generate an idle scan packet first
    DmxPacketInfo idlePacket;
    idlePacket.channelOffset = static_cast<uint16_t>(idleScanPointer_);
    for (int i = idleScanPointer_; i < kIdleScanSize + idleScanPointer_; ++i) {
        if (i < static_cast<int>(dmxData.size())) {
            idlePacket.data.push_back(dmxData[static_cast<size_t>(i)]);
            idlePacket.length++;
            // remove it from the diffs if it exists there
            diffs.erase(i);
        } else {
            break;
        }
    }
    // reset the pointer
    idleScanPointer_ += kIdleScanSize;
    if (idleScanPointer_ >= static_cast<int>(dmxData.size())) {
        idleScanPointer_ = 0;
    }
    packets.push_back(std::move(idlePacket));

    // iterate over the diff, coalescing consecutive channels into runs
    for (auto it = diffs.begin(); it != diffs.end();) {
        int startChannel = it->first;
        uint8_t length = 1;

        auto next = std::next(it);
        while (next != diffs.end() &&
               next->first == startChannel + length &&
               length < DmxPacketInfo::kMaxDataLength) {
            ++length;
            it = next;
            next = std::next(it);
        }

        DmxPacketInfo packet;
        packet.channelOffset = static_cast<uint16_t>(startChannel);
        packet.length = length;
        packet.data.reserve(length);
        for (int j = 0; j < length; ++j) {
            packet.data.push_back(diffs[startChannel + j]);
        }
        packets.push_back(std::move(packet));

        ++it; // advance past the last channel of this run
    }

    // save this frame as the last frame for next iteration
    lastFrameData_ = dmxData;
    dmxData.clear();

    // serialize the data packets
    for (const auto& packet : packets) {
        std::vector<uint8_t> serialized = packet.Serialize();
        dmxData.insert(dmxData.end(), serialized.begin(), serialized.end());
    }
}
