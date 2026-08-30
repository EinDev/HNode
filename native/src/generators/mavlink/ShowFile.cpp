#include "ShowFile.h"

#include <algorithm>
#include <cmath>

// Port of Assets/Plugin/Generators/MAVLinkDrone/ShowFile.cs.

namespace {

enum class BlockType : uint8_t {
    Trajectory = 1,
    LightProgram = 2,
    Comment = 3,
    RthPlan = 4,
    YawControl = 5,
    EventList = 6,
};

// Matches PyroEvent.cs's minMillisecondsPerFiring constant.
constexpr double kMinMsPerFiring = 300.0;

// Matches UnityEngine.Color32.Lerp: per-channel byte lerp with t clamped to [0,1].
RGBA8 LerpColor(const RGBA8& a, const RGBA8& b, float t) {
    float tc = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    auto lerpChannel = [&](uint8_t ca, uint8_t cb) {
        float v = static_cast<float>(ca) + (static_cast<float>(cb) - static_cast<float>(ca)) * tc;
        return static_cast<uint8_t>(v + 0.5f); // round-to-nearest, matching (byte) truncation of a rounded value
    };
    return RGBA8{lerpChannel(a.r, b.r), lerpChannel(a.g, b.g), lerpChannel(a.b, b.b), 255};
}

} // namespace

ShowFile::ShowFile(const std::vector<uint8_t>& rawData) {
    // 2 days in the future by default ("assume way into the future"), matching
    // ShowFile.cs's `DateTime.UtcNow + TimeSpan.FromDays(2)` - keeps playback pinned
    // to the first program entry (via the "time < first.startTime" early-out below)
    // until a real SHOW_START_TIME parameter arrives.
    showStartTime = std::chrono::system_clock::now() + std::chrono::hours(48);

    ByteReader reader(rawData);
    reader.ReadBytes(10); // file header - unparsed/unused, matches the C# reference

    while (reader.Remaining() > 0) {
        ExtractBlock(reader);
    }
}

void ShowFile::ExtractBlock(ByteReader& reader) {
    BlockType blockType = static_cast<BlockType>(reader.ReadByte());
    uint16_t blockSize = reader.ReadUInt16LE();
    std::vector<uint8_t> blockBytes = reader.ReadBytes(blockSize);
    ByteReader blockReader(blockBytes);

    switch (blockType) {
        case BlockType::LightProgram: {
            while (blockReader.Remaining() > 0) {
                lightProgram_.emplace_back(blockReader);
            }
            if (!lightProgram_.empty()) {
                lightProgram_[0].startTimeMs = 0.0;
                for (size_t i = 1; i < lightProgram_.size(); ++i) {
                    lightProgram_[i - 1].endTimeMs = lightProgram_[i - 1].startTimeMs + lightProgram_[i - 1].durationMs;
                    lightProgram_[i].startTimeMs = lightProgram_[i - 1].endTimeMs;
                }
                lightProgram_.back().endTimeMs = lightProgram_.back().startTimeMs + lightProgram_.back().durationMs;

                RGBA8 lastColor{0, 0, 0, 255};
                for (auto& ev : lightProgram_) {
                    ev.previousEventColor = lastColor;
                    if (ev.SetsColor()) lastColor = ev.color;
                }
                lightProgramPointer = 0;
            }
            break;
        }
        case BlockType::Trajectory: {
            if (blockBytes.size() == 1) {
                trajectoryProgram_.emplace_back(); // blank/default trajectory - matches an "empty trajectory block"
                break;
            }

            uint8_t flags = blockReader.ReadByte();
            uint8_t scale = flags & 0x7F; // MSB unused, scale is the remaining 7 bits
            Vec3 startPos = Trajectory::DecodeStartSpatialCoordinates(blockReader, scale);
            float startYaw = Trajectory::DecodeAngleCoordinate(blockReader);

            while (blockReader.Remaining() > 0) {
                trajectoryProgram_.emplace_back(blockReader, startPos, startYaw, scale);
                startPos = trajectoryProgram_.back().lastPosition;
                startYaw = trajectoryProgram_.back().lastYaw;
            }

            if (!trajectoryProgram_.empty()) {
                trajectoryProgram_[0].startTimeMs = 0.0;
                for (size_t i = 1; i < trajectoryProgram_.size(); ++i) {
                    trajectoryProgram_[i - 1].endTimeMs =
                        trajectoryProgram_[i - 1].startTimeMs + trajectoryProgram_[i - 1].durationMs;
                    trajectoryProgram_[i].startTimeMs = trajectoryProgram_[i - 1].endTimeMs;
                }
                trajectoryProgram_.back().endTimeMs =
                    trajectoryProgram_.back().startTimeMs + trajectoryProgram_.back().durationMs;
                trajectoryProgramPointer = 0;
            }
            break;
        }
        case BlockType::EventList: {
            // The C# reference flags this format as guessed/reverse-engineered, with
            // no external spec beyond its own source - ported byte-for-byte, not
            // "corrected" against anything.
            while (blockReader.Remaining() > 0) {
                PyroEvent ev;
                ev.startMs = static_cast<double>(blockReader.ReadVarInt());
                double durMs = static_cast<double>(blockReader.ReadVarInt());
                durMs = std::max(durMs, kMinMsPerFiring);
                ev.durationMs = durMs;
                ev.endMs = ev.startMs + ev.durationMs;
                ev.pyroIndex = blockReader.ReadVarInt();

                uint8_t signs = blockReader.ReadByte();
                int pitchSign = (signs & 0b00000100) ? -1 : 1;
                int yawSign = (signs & 0b00000010) ? -1 : 1;
                int rollSign = (signs & 0b00000001) ? -1 : 1;

                ev.pitch = static_cast<float>(blockReader.ReadVarInt() * pitchSign);
                ev.yaw = static_cast<float>(blockReader.ReadVarInt() * yawSign);
                ev.roll = static_cast<float>(blockReader.ReadVarInt() * rollSign);

                pyroProgram_.push_back(ev);
            }
            break;
        }
        default:
            break; // COMMENT/RTH_PLAN/YAW_CONTROL/unknown - bytes already consumed above, safely skipped
    }
}

Vec3 ShowFile::GetPositionAtRealTime(std::chrono::system_clock::time_point now) {
    double elapsedMs = std::chrono::duration<double, std::milli>(now - showStartTime).count();
    return GetPositionAtTime(elapsedMs);
}

Vec3 ShowFile::GetPositionAtTime(double timeMs) {
    // Defensive: the C# reference calls .Last()/[0] unguarded here and would throw
    // on an empty program (e.g. a show file with no TRAJECTORY block) - see
    // ByteReader.h's header comment for why this port avoids that failure mode.
    if (trajectoryProgram_.empty()) return Vec3{};

    if (timeMs > trajectoryProgram_.back().endTimeMs) {
        return trajectoryProgram_.back().Evaluate(1.0f);
    }
    if (timeMs < trajectoryProgram_.front().startTimeMs) {
        return trajectoryProgram_.front().Evaluate(0.0f);
    }

    if (!trajectoryProgram_[static_cast<size_t>(trajectoryProgramPointer)].InsideEvent(timeMs)) {
        for (int i = trajectoryProgramPointer + 1;
             i < static_cast<int>(trajectoryProgram_.size()) && i < trajectoryProgramPointer + kPointerLookahead; ++i) {
            if (trajectoryProgram_[static_cast<size_t>(i)].InsideEvent(timeMs)) {
                trajectoryProgramPointer = i;
                break;
            }
        }
    }

    const Trajectory& tevent = trajectoryProgram_[static_cast<size_t>(trajectoryProgramPointer)];
    float t = tevent.durationMs > 0.0 ? static_cast<float>((timeMs - tevent.startTimeMs) / tevent.durationMs) : 0.0f;
    return tevent.Evaluate(t);
}

PyroEvent ShowFile::GetPyroAtRealTime(std::chrono::system_clock::time_point now) {
    double elapsedMs = std::chrono::duration<double, std::milli>(now - showStartTime).count();
    return GetPyroAtTime(elapsedMs);
}

PyroEvent ShowFile::GetPyroAtTime(double timeMs) {
    if (pyroProgram_.empty()) return PyroEvent{};
    if (timeMs < pyroProgram_.front().startMs) return PyroEvent{};
    if (timeMs > pyroProgram_.back().endMs) return PyroEvent{};

    // "More expensive but pyro drone count is small" - matches the C# reference's own
    // comment: linear scan for the latest-starting event whose window contains `timeMs`.
    PyroEvent best;
    for (const auto& ev : pyroProgram_) {
        if (ev.InsideEvent(timeMs) && ev.startMs > best.startMs) {
            best = ev;
        }
    }
    return best;
}

RGBA8 ShowFile::GetColorAtRealTime(std::chrono::system_clock::time_point now) {
    double elapsedMs = std::chrono::duration<double, std::milli>(now - showStartTime).count();
    return GetColorAtTime(elapsedMs);
}

RGBA8 ShowFile::GetColorAtTime(double timeMs) {
    if (lightProgram_.empty()) return RGBA8{0, 0, 0, 255}; // defensive, see GetPositionAtTime's comment

    if (timeMs < lightProgram_.front().startTimeMs) return RGBA8{0, 0, 0, 255};
    if (timeMs > lightProgram_.back().endTimeMs) return RGBA8{0, 0, 0, 255};

    if (!lightProgram_[static_cast<size_t>(lightProgramPointer)].InsideEvent(timeMs)) {
        for (int i = lightProgramPointer + 1;
             i < static_cast<int>(lightProgram_.size()) && i < lightProgramPointer + kPointerLookahead; ++i) {
            if (lightProgram_[static_cast<size_t>(i)].InsideEvent(timeMs)) {
                lightProgramPointer = i;
                break;
            }
        }
    }

    LightEvent& startEvent = lightProgram_[static_cast<size_t>(lightProgramPointer)];

    if (startEvent.IsFade() && startEvent.SetsColor()) {
        float t = 0.0f;
        if (startEvent.endTimeMs > startEvent.startTimeMs) {
            t = static_cast<float>((timeMs - startEvent.startTimeMs) / (startEvent.endTimeMs - startEvent.startTimeMs));
        }
        return LerpColor(startEvent.previousEventColor, startEvent.color, t);
    }

    if (startEvent.SetsColor()) return startEvent.color;
    return startEvent.previousEventColor;
}
