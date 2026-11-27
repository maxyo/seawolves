// How two characters that overlap share the push that separates them (Supervisor::Update).
#pragma once

#include <utility>

namespace storm::location
{

struct PushParty
{
    bool moving;     // walking, or pushed by an impulse toward the other
    float mass;      // MSG_CHARACTER_EX_MSG SetMass; equal masses share evenly
    bool unpushable; // MakeUnpushable
};

// The shares of the separation the first and the second character move, summing to 1. A moving character takes
// almost all of it from one that stands (in a fight even more: a standing fighter is not shoved); among equals the
// lighter one moves more; an unpushable one does not move unless both are.
inline std::pair<float, float> PushShares(bool fight, const PushParty &first, const PushParty &second)
{
    if (first.unpushable != second.unpushable)
        return first.unpushable ? std::pair{0.0f, 1.0f} : std::pair{1.0f, 0.0f};
    if (first.moving == second.moving)
    {
        const float share = second.mass / (first.mass + second.mass);
        return {share, 1.0f - share};
    }
    const auto [mover, stander] = fight ? std::pair{0.999f, 0.001f} : std::pair{0.9f, 0.1f};
    return first.moving ? std::pair{mover, stander} : std::pair{stander, mover};
}

} // namespace storm::location
