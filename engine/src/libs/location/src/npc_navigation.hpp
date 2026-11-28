// Where an NPC's move ends: the stop radius a script may give "Goto point", and the arrival in an area of
// "Goto area" / "Runto area" (Caribbean Legend's scripts send both; To Each His Own's send neither).
#pragma once

#include <glm/vec3.hpp>

#include <cmath>

namespace storm::location
{

inline bool PositiveFinite(float value)
{
    return std::isfinite(value) && value > 0.0f;
}

// The scripts pass a non-positive radius to mean "the engine's".
inline float GotoStopRadius(float requested, float standard)
{
    return PositiveFinite(requested) ? requested : standard;
}

// Inside the area on the horizontal plane and on its level; whether the patch connects the two places is the
// caller's question, asked only when this holds.
inline bool InsideArea(const glm::vec3 &position, const glm::vec3 &centre, float radius, float heightTolerance)
{
    const float dx = position.x - centre.x;
    const float dz = position.z - centre.z;
    return PositiveFinite(radius) && std::sqrt(dx * dx + dz * dz) <= radius &&
           std::fabs(position.y - centre.y) < heightTolerance;
}

} // namespace storm::location
