#pragma once

// A PICTURE drawn as a fan (FanPrimitive = 1): the part of the circle inscribed in the node between two angles, the
// texture mapped the same way, in steps of FanAngleStep (a circular progress bar). Angles in radians, 0 along +x,
// growing clockwise on the screen.

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace storm::xi
{
struct FanVertex
{
    float x, y, u, v;
};

inline constexpr float kFullTurn = 2.0f * std::numbers::pi_v<float>;

// FanAngleStep in degrees (3 when missing or not positive) as radians, between 1/4096 of a turn and a whole turn
inline float FanStep(float degrees)
{
    const float step = std::isfinite(degrees) && degrees > 0.0f ? degrees : 3.0f;
    return std::clamp(step * std::numbers::pi_v<float> / 180.0f, kFullTurn / 4096.0f, kFullTurn);
}

// the end of an arc from `start`: not before it, at most a whole turn after it
inline float FanEnd(float start, float end)
{
    return std::clamp(end, start, start + kFullTurn);
}

// the centre, then the arc's points; nothing for an empty arc
inline std::vector<FanVertex> FanVertices(float left, float top, float right, float bottom, float u0, float v0,
                                          float u1, float v1, float start, float end, float step)
{
    const float arc = std::min(end - start, kFullTurn);
    if (!(arc > 0.0f))
        return {};
    const int segments = std::max(1, static_cast<int>(std::ceil(arc / step)));
    const float cx = (left + right) / 2, cy = (top + bottom) / 2;
    const float radius = std::min(right - left, bottom - top) / 2;
    const float cu = (u0 + u1) / 2, cv = (v0 + v1) / 2, ru = (u1 - u0) / 2, rv = (v1 - v0) / 2;
    std::vector<FanVertex> vertices{{cx, cy, cu, cv}};
    for (int i = 0; i <= segments; ++i)
    {
        const float angle = start + std::min(static_cast<float>(i) * step, arc);
        const float c = std::cos(angle), s = std::sin(angle);
        vertices.push_back({cx + c * radius, cy + s * radius, cu + c * ru, cv + s * rv});
    }
    return vertices;
}
} // namespace storm::xi
