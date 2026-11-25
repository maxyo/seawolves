// Distance-based foam emission and age-based width, independent of rendering/frame rate.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace storm::ship
{
// Keep the stock near/far lift ramp; the observer and emitter are world positions.
template <class Position>
inline float TrackElevation(const Position &camera, const Position &ship, float near_height, float far_height)
{
    const float dx = camera.x - ship.x, dy = camera.y - ship.y, dz = camera.z - ship.z;
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    const float blend = std::clamp(distance / 10000.0f, 0.0f, 1.0f);
    return near_height + blend * (far_height - near_height);
}

struct TrackSpacing
{
    double remainder = 0;
    void Reset()
    {
        remainder = 0;
    }

    template <class Emit> void Advance(float distance, float spacing, float previous_yaw, float yaw, Emit emit)
    {
        if (!(spacing > 0) || !std::isfinite(spacing) || !std::isfinite(distance) || distance < 0)
        {
            Reset();
            return;
        }
        if (distance == 0)
            return;
        const float turn = std::remainder(yaw - previous_yaw, 6.283185307179586f);
        double next = spacing - remainder;
        std::size_t count = 0;
        while (next <= distance + 1e-6 * spacing)
        {
            const float fraction = std::clamp(static_cast<float>(next / distance), 0.0f, 1.0f);
            emit(static_cast<float>(std::min(next, double(distance))), fraction, previous_yaw + turn * fraction);
            next += spacing;
            ++count;
        }
        remainder = std::max(0.0, remainder + distance - count * double(spacing));
    }
};

// The foam pattern of a trail strip at `across` (0..1 over its width) on the strip at `along` (its texture v, metres
// of track / 20): patches that differ edge to edge and strip to strip instead of every strip repeating the texture's
// lines. Smooth in both directions; `seed` gives each ship and track its own pattern. Fixed for the strip's life.
inline float TrackFoamPattern(float across, float along, float seed)
{
    const float n = 0.5f + 0.5f * std::sin(11.0f * across + 9.3f * along + seed) *
                               std::sin(4.7f * across - 3.9f * along + 1.7f * seed);
    return std::clamp((n - 0.3f) / 0.45f, 0.0f, 1.0f);
}

// How much of a strip's foam with `pattern` (TrackFoamPattern) stays when it is `age` (0..1 of its life) old: the
// patches open more the older it gets. 1 = all of it.
inline float TrackFoamKeep(float pattern, float age)
{
    const float breakup = std::clamp(0.45f + 0.55f * age, 0.0f, 1.0f);
    const float t = pattern;
    return 1.0f - breakup * t * t * (3.0f - 2.0f * t);
}

// Where a trail strip's cross-section point at `across` (0..1) lies, as a fraction of the strip's width from its
// centre: the strip's edges wander by up to `wobble` of the width along the trail, so its foam lines are not ruled.
inline float TrackFoamOffset(float across, float along, float seed, float wobble)
{
    return (across - 0.5f) * (1.0f + wobble * std::sin(5.3f * along + seed + 2.0f * across));
}

// A strip's cross-section point: what of it depends only on where it was laid, worked out once when it is laid.
struct TrackFoamColumn
{
    float offset;  // TrackFoamOffset
    float pattern; // TrackFoamPattern
};

// The `count` cross-section points of a strip laid at `along`, from edge to edge (across = i / (count - 1)).
template <class Out> void TrackFoamColumns(int count, float along, float seed, float wobble, Out out)
{
    for (int i = 0; i < count; ++i)
    {
        const float across = static_cast<float>(i) / (static_cast<float>(count) - 1.0f);
        *out++ = TrackFoamColumn{TrackFoamOffset(across, along, seed, wobble), TrackFoamPattern(across, along, seed)};
    }
}

// Whether the box `lo`..`hi` lies wholly behind one of the view's side planes (inside: N . p >= D), so nothing in
// it can reach the picture.
template <class Plane, class Vec> bool TrackBoxOutside(const Plane *planes, int count, const Vec &lo, const Vec &hi)
{
    for (int i = 0; i < count; ++i)
    {
        const Plane &p = planes[i];
        // The box's corner farthest along the plane's normal.
        const float x = p.Nx >= 0 ? hi.x : lo.x, y = p.Ny >= 0 ? hi.y : lo.y, z = p.Nz >= 0 ? hi.z : lo.z;
        if (p.Nx * x + p.Ny * y + p.Nz * z < p.D)
            return true;
    }
    return false;
}

inline float TrackWidth(float initial, float spread_speed, float age, float lifetime)
{
    if (!(lifetime > 0))
        return 0;
    const float time = std::clamp(age, 0.0f, lifetime);
    // Integral of spread_speed * (1 - time/lifetime), not a frame-size-dependent Euler update.
    return std::max(0.0f, initial + spread_speed * time * (1 - time / (2 * lifetime)));
}
} // namespace storm::ship
