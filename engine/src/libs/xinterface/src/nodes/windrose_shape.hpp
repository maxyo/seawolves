#pragma once

// The shapes of a WINDROSE node: a ship's speed by the wind's direction as a closed polygon around a centre (heading
// 0 up, clockwise; a speed of 1 at the radius), and a ring of segments coloured by the speed of their headings.

#include <cmath>
#include <cstdint>
#include <numbers>
#include <span>
#include <vector>

namespace storm::xi
{
struct RosePoint
{
    float x, y;
};

// one point per speed, the first repeated at the end to close the outline
inline std::vector<RosePoint> WindRoseOutline(float cx, float cy, float radius, std::span<const float> speeds)
{
    std::vector<RosePoint> points;
    if (speeds.empty())
        return points;
    const float step = 2.0f * std::numbers::pi_v<float> / static_cast<float>(speeds.size());
    for (size_t i = 0; i <= speeds.size(); ++i)
    {
        const float angle = step * static_cast<float>(i % speeds.size());
        const float r = speeds[i % speeds.size()] * radius;
        points.push_back({cx + r * std::sin(angle), cy - r * std::cos(angle)});
    }
    return points;
}

struct RingVertex
{
    float x, y;
    uint32_t color;
};

// two triangles per heading between the inner and the outer radius, each edge in the colour of its heading's speed
template <typename Color>
std::vector<RingVertex> WindRoseRing(float cx, float cy, float inner, float outer, std::span<const float> speeds,
                                     Color color)
{
    std::vector<RingVertex> vertices;
    const size_t n = speeds.size();
    const float step = n ? 2.0f * std::numbers::pi_v<float> / static_cast<float>(n) : 0.0f;
    const auto at = [&](size_t i, float r) {
        const float angle = step * static_cast<float>(i);
        return RingVertex{cx + r * std::sin(angle), cy - r * std::cos(angle), color(speeds[i % n])};
    };
    for (size_t i = 0; i < n; ++i)
    {
        const RingVertex a = at(i, inner), b = at(i, outer), c = at(i + 1, inner), d = at(i + 1, outer);
        vertices.insert(vertices.end(), {a, b, c, c, b, d});
    }
    return vertices;
}
} // namespace storm::xi
