#include "LightEvent.h"

// Port of Assets/Plugin/Generators/MAVLinkDrone/BlockData/LightEvent.cs.

namespace {
// Matches LightEvent.cs's private GetDuration(): a varint frame count at 50 FPS
// (20ms/frame).
double ReadDurationMs(ByteReader& reader) {
    return static_cast<double>(reader.ReadVarInt()) * 20.0;
}
} // namespace

LightEvent::LightEvent(ByteReader& reader) {
    opcode = static_cast<Opcode>(reader.ReadByte());

    switch (opcode) {
        case Opcode::End:
        case Opcode::Nop:
        case Opcode::LoopEnd:
        case Opcode::ResetClock:
        case Opcode::Unused1:
            break;
        case Opcode::Sleep:
        case Opcode::WaitUntil: // not technically correct per the C# reference's own comment, but unused/harmless
            durationMs = ReadDurationMs(reader);
            break;
        case Opcode::SetColor:
        case Opcode::SetColorFromChannels: { // grouped identically to SetColor in the C# reference's actual switch
            uint8_t r = reader.ReadByte();
            uint8_t g = reader.ReadByte();
            uint8_t b = reader.ReadByte();
            color = RGBA8{r, g, b, 255};
            durationMs = ReadDurationMs(reader);
            break;
        }
        case Opcode::SetGray: {
            uint8_t v = reader.ReadByte();
            color = RGBA8{v, v, v, 255};
            durationMs = ReadDurationMs(reader);
            break;
        }
        case Opcode::SetBlack:
            color = RGBA8{0, 0, 0, 255};
            durationMs = ReadDurationMs(reader);
            break;
        case Opcode::SetWhite:
            color = RGBA8{255, 255, 255, 255};
            durationMs = ReadDurationMs(reader);
            break;
        case Opcode::FadeToColor:
        case Opcode::FadeToColorFromChannels: { // grouped identically to FadeToColor in the C# reference
            uint8_t r = reader.ReadByte();
            uint8_t g = reader.ReadByte();
            uint8_t b = reader.ReadByte();
            color = RGBA8{r, g, b, 255};
            durationMs = ReadDurationMs(reader);
            break;
        }
        case Opcode::FadeToGray: {
            uint8_t v = reader.ReadByte();
            color = RGBA8{v, v, v, 255};
            durationMs = ReadDurationMs(reader);
            break;
        }
        case Opcode::FadeToBlack:
            color = RGBA8{0, 0, 0, 255};
            durationMs = ReadDurationMs(reader);
            break;
        case Opcode::FadeToWhite:
            color = RGBA8{255, 255, 255, 255};
            durationMs = ReadDurationMs(reader);
            break;
        case Opcode::LoopBegin:
            // Parsed for byte-alignment but never executed - matches the C#
            // reference (LOOP_BEGIN/LOOP_END/JUMP are walked over in recorded
            // linear order, not actually looped/jumped at runtime; see the enum's
            // own "ignore, shouldnt be used anymore" comments in the reference).
            counter = reader.ReadByte();
            break;
        case Opcode::Jump:
            address = reader.ReadVarInt();
            break;
        case Opcode::SetPyro:
        case Opcode::SetPyroAll:
            reader.ReadByte(); // argument byte read-and-discarded, matches the C# reference exactly
            break;
        default:
            // The C# reference throws NotImplementedException for any opcode without
            // a case above (e.g. TriggeredJump=19, which has none). This parses
            // untrusted, network-uploaded show-file bytes on a background thread -
            // crashing the whole app on an unexpected opcode would be far worse than
            // Unity's failure mode (an exception caught per-frame by the engine), so
            // this port just stops here (0 duration, no color) instead of throwing.
            // Events after this one in the same block may desync since this opcode's
            // true byte length is unknown, but that only affects a single malformed
            // or not-yet-supported show file, not the app's stability.
            break;
    }
}

bool LightEvent::SetsColor() const {
    switch (opcode) {
        case Opcode::SetColor:
        case Opcode::SetColorFromChannels:
        case Opcode::SetGray:
        case Opcode::SetBlack:
        case Opcode::SetWhite:
        case Opcode::FadeToColor:
        case Opcode::FadeToColorFromChannels:
        case Opcode::FadeToGray:
        case Opcode::FadeToBlack:
        case Opcode::FadeToWhite:
            return true;
        default:
            return false;
    }
}

bool LightEvent::IsFade() const {
    switch (opcode) {
        case Opcode::FadeToColor:
        case Opcode::FadeToColorFromChannels:
        case Opcode::FadeToGray:
        case Opcode::FadeToBlack:
        case Opcode::FadeToWhite:
            return true;
        default:
            return false;
    }
}
