#pragma once
// Port of Assets/Plugin/Generators/MAVLinkDrone/BlockData/LightEvent.cs - one opcode
// from a drone's LIGHT_PROGRAM bytecode block, decoded into a timed color event.
#include <cstdint>
#include <optional>

#include "../../render/PixelOps.h"
#include "ByteReader.h"

class LightEvent {
public:
    // https://github.com/skybrush-io/libskybrush/blob/9ecf48d6fcf258c5be77bf31d88a91241b1a5700/src/lights/commands.h#L39
    enum class Opcode : uint8_t {
        End = 0,
        Nop = 1,
        Sleep = 2,
        WaitUntil = 3,
        SetColor = 4,
        SetGray = 5,
        SetBlack = 6,
        SetWhite = 7,
        FadeToColor = 8,
        FadeToGray = 9,
        FadeToBlack = 10,
        FadeToWhite = 11,
        LoopBegin = 12,
        LoopEnd = 13,
        ResetClock = 14,
        Unused1 = 15,
        SetColorFromChannels = 16,
        FadeToColorFromChannels = 17,
        Jump = 18,
        TriggeredJump = 19,
        SetPyro = 20,
        SetPyroAll = 21,
    };

    explicit LightEvent(ByteReader& reader);

    bool InsideEvent(double timeMs) const { return timeMs >= startTimeMs && timeMs < endTimeMs; }
    bool SetsColor() const;
    bool IsFade() const;

    Opcode opcode = Opcode::End;
    RGBA8 color{0, 0, 0, 255};
    RGBA8 previousEventColor{0, 0, 0, 255};
    double durationMs = 0.0;

    // Sentinel "before any real time" defaults - always overwritten by ShowFile's
    // cumulative start/end-time pass once the whole LIGHT_PROGRAM block is decoded
    // (matches LightEvent.cs's `new TimeSpan(-50)`/`new TimeSpan(-1)` defaults; the
    // exact tiny negative values don't matter since they're never read before being
    // replaced).
    double startTimeMs = -5.0;
    double endTimeMs = -0.1;

    std::optional<uint8_t> counter;
    std::optional<int> address;
};
