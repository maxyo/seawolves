// How MSG_EFFECT_PLAY and MSG_EFFECT_PLAY_FILE place an effect (README.md, "Messages"); checked by tests/effects.
#pragma once

#include "message.h"
#include "storm/render/effekseer.hpp"

#include <optional>

namespace storm::effects
{
struct Placement
{
    render::effekseer::Vec3 position, angles, scale{1, 1, 1};
    int32_t start_frame = 0;
};

inline render::effekseer::Vec3 ReadVec3(MESSAGE &message)
{
    render::effekseer::Vec3 v;
    v.x = message.Float();
    v.y = message.Float();
    v.z = message.Float();
    return v;
}

// After the code and the "s" name: "fff" position, then optionally "fff" angles, "fff" scale and "l" start frame.
// Nothing for a message of another length.
inline std::optional<Placement> ReadPlacement(MESSAGE &message)
{
    const size_t given = message.GetFormat().size();
    if (given != 5 && given != 8 && given != 11 && given != 12)
        return std::nullopt;
    Placement placement;
    placement.position = ReadVec3(message);
    if (given >= 8)
        placement.angles = ReadVec3(message);
    if (given >= 11)
        placement.scale = ReadVec3(message);
    if (given == 12)
        placement.start_frame = message.Long();
    return placement;
}
} // namespace storm::effects
