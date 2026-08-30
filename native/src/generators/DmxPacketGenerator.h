#pragma once
// Port of Assets/Plugin/Generators/GeneratorDMXPacket.cs - an unusual "generator" that
// REPLACES the entire dmxData buffer with a custom diff-based binary packet encoding
// (channel-offset + length + data runs, plus a rotating idle-scan run each frame).
// Any generator/serializer running after this one in the pipeline sees the encoded
// bytes, not real DMX values - that's inherent to what this one does in the original
// too, not a native-port bug. Not animated: it only re-encodes when GenerateDMX is
// actually called (i.e. when something else already marked the frame dirty), which
// means the idle-scan pointer advances on real updates rather than a free-running
// timer - a deliberate simplification, see DmxPacketGenerator.cpp.
#include "IGenerator.h"
#include <unordered_map>
#include <vector>

class DmxPacketGenerator : public IGenerator {
public:
    const char* Name() const override { return "DMXPacket"; }

    void GenerateDMX(std::vector<uint8_t>& dmxData) override;

private:
    std::vector<uint8_t> lastFrameData_;
    std::unordered_map<int, uint8_t> lastDiff_;
    int idleScanPointer_ = 0;
    static constexpr int kIdleScanSize = 50;
};
