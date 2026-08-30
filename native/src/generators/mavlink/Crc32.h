#pragma once
// Standard reflected CRC-32 (polynomial 0x04C11DB7, reflected form 0xEDB88320),
// init=0, no final XOR - matches the C# reference's CrcSharp-based
// `new CrcParameters(32, 0x04C11DB7, 0, 0, refIn: true, refOut: true)` used by
// FTPMessage.ftp_opcode.CalcFileCRC32 in Assets/Plugin/Generators/MAVLinkDrone/
// GeneratorMAVLinkDroneNetwork.cs. Note this is NOT the common "CRC-32"/zlib variant
// (which uses init=0xFFFFFFFF and a final XOR of 0xFFFFFFFF) - this app's reference
// implementation uses init=0 and no final XOR, so this reproduces that exact variant,
// not the more familiar one.
#include <cstddef>
#include <cstdint>

inline uint32_t Crc32Reflected(const uint8_t* data, size_t length) {
    uint32_t crc = 0;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
        }
    }
    return crc;
}
