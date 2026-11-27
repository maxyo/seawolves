#pragma once

// The colours of a ship's relative speed by heading, as the scripts give them (ReInitSpeedColors), for whatever draws
// a speed ring. Free of the engine: tests/speed_colors runs it as it is.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>

namespace storm
{
struct SpeedColorStop
{
    float speed;    // 0..1, the stops in increasing order
    uint32_t color; // ARGB
};

inline constexpr size_t kSpeedColors = 1000;
using SpeedColorTable = std::array<uint32_t, kSpeedColors>;

// Each channel interpolated between the two stops around the speed; below the first stop its colour, above the last
// its colour. Fewer than two stops or a negative speed: white.
inline SpeedColorTable BuildSpeedColors(std::span<const SpeedColorStop> stops)
{
    SpeedColorTable table;
    table.fill(0xFFFFFFFFu);
    if (stops.size() < 2 || std::any_of(stops.begin(), stops.end(), [](const auto &s) { return s.speed < 0.0f; }))
        return table;
    for (size_t i = 0; i < kSpeedColors; ++i)
    {
        const float t = static_cast<float>(i) / (kSpeedColors - 1);
        const auto upper = std::find_if(stops.begin(), stops.end(), [&](const auto &s) { return s.speed >= t; });
        if (upper == stops.begin() || upper == stops.end())
        {
            table[i] = (upper == stops.end() ? stops.back() : stops.front()).color;
            continue;
        }
        const SpeedColorStop &a = *(upper - 1), &b = *upper;
        const float f = b.speed > a.speed ? (t - a.speed) / (b.speed - a.speed) : 1.0f;
        uint32_t color = 0;
        for (int shift = 0; shift < 32; shift += 8)
        {
            const float from = static_cast<float>((a.color >> shift) & 0xFF),
                        to = static_cast<float>((b.color >> shift) & 0xFF);
            color |= static_cast<uint32_t>(std::lround(std::lerp(from, to, f))) << shift;
        }
        table[i] = color;
    }
    return table;
}

// The colour at relative speed `t` (clamped to 0..1) of the table ReInitSpeedColors built last; white before.
uint32_t SpeedColor(float t);
} // namespace storm
