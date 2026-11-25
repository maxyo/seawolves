#include "storm/fx/shadow_model.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

namespace storm::fx
{
namespace
{
float Snap(float value, float texel)
{
    return std::floor(value / texel + 0.5f) * texel;
}
// glm::orthoLH_ZO is the D3D matrix (the bytes of glm::mat4 are D3D's row-vector layout).
std::array<float, 16> Ortho(float cx, float cy, float half, float near_plane, float far_plane)
{
    return std::bit_cast<std::array<float, 16>>(
        glm::orthoLH_ZO(cx - half, cx + half, cy - half, cy + half, near_plane, far_plane));
}
} // namespace

SunShadowBudget SunShadowBudgetFor(ShadowQuality quality)
{
    // Budget per device class. Both layers are slices of one array texture
    // (the lit programs have one sampler unit for the sun), so the dynamic layer has the static one's size.
    switch (quality)
    {
    case ShadowQuality::Low:
        return {1024, 1, 1};
    case ShadowQuality::Soft:
        return {2048, 4, 1};
    default:
        return {};
    }
}

float SunShadowStrength(float sun_dir_y)
{
    return std::clamp(sun_dir_y * 8.0f, 0.0f, 1.0f);
}

std::array<float, 16> SunLayers::View(glm::vec3 origin) const
{
    // Light-space position of a caller's point p: basis * (p + origin) - anchor.
    const float tx = glm::dot(origin, right) - anchor[0], ty = glm::dot(origin, up) - anchor[1],
                tz = glm::dot(origin, forward) - anchor[2];
    return {right[0], up[0], forward[0], 0.0f, right[1], up[1], forward[1], 0.0f,
            right[2], up[2], forward[2], 0.0f, tx,       ty,    tz,         1.0f};
}

std::array<float, 16> SunLayers::StaticProjection() const
{
    return Ortho(0.0f, 0.0f, static_half, near_plane, far_plane);
}

std::array<float, 16> SunLayers::DynamicProjection() const
{
    return Ortho(dynamic_center[0] - anchor[0], dynamic_center[1] - anchor[1], dynamic_half, near_plane, far_plane);
}

glm::vec3 SunLayers::StaticCoord(glm::vec3 world) const
{
    const float x = glm::dot(world, right) - anchor[0], y = glm::dot(world, up) - anchor[1],
                z = glm::dot(world, forward) - anchor[2];
    return {0.5f + 0.5f * x / static_half, 0.5f - 0.5f * y / static_half, (z - near_plane) / (far_plane - near_plane)};
}

glm::vec3 SunLayers::DynamicFromStatic() const
{
    const float k = static_half / dynamic_half;
    const float dx = (dynamic_center[0] - anchor[0]) / dynamic_half,
                dy = (dynamic_center[1] - anchor[1]) / dynamic_half;
    return {k, 0.5f * (1.0f - k) - 0.5f * dx, 0.5f * (1.0f - k) + 0.5f * dy};
}

SunLayerTracker::Frame SunLayerTracker::Advance(glm::vec3 sun, glm::vec3 focus, int size, float radius)
{
    Frame frame;
    frame.redraw = Redraw::None;
    sun = glm::normalize(
        sun); // the step is a fraction of a degree: a direction a little off unit length must not read as one
    if (!valid_ || drawn_.size != size || drawn_.dynamic_half != radius)
        frame.redraw = Redraw::First;
    else if (glm::dot(sun, drawn_.sun) < std::cos(kSunStepDegrees * 0.01745329f))
        frame.redraw = Redraw::Sun;
    else
    {
        const float d[3] = {glm::dot(focus, drawn_.right) - drawn_.anchor[0],
                            glm::dot(focus, drawn_.up) - drawn_.anchor[1],
                            glm::dot(focus, drawn_.forward) - drawn_.anchor[2]};
        if (std::max({std::fabs(d[0]), std::fabs(d[1]), std::fabs(d[2])}) > kSunStaticBand)
            frame.redraw = Redraw::Band;
    }
    if (frame.redraw != Redraw::None)
    {
        SunLayers &s = drawn_;
        s.size = size;
        s.static_half = radius + kSunStaticBand;
        s.dynamic_half = radius;
        s.static_texel = 2.0f * s.static_half / static_cast<float>(std::max(size, 1));
        s.dynamic_texel = 2.0f * s.dynamic_half / static_cast<float>(std::max(size, 1));
        // The depth range holds the region wherever the band lets it go, and the casters towards the sun.
        s.near_plane = -(radius + kSunStaticBand + kSunCasterReach);
        s.far_plane = radius + kSunStaticBand;
        if (frame.redraw != Redraw::Band)
        {
            s.sun = sun;
            s.forward = glm::normalize(-sun);
            s.right = glm::normalize(
                glm::cross(std::fabs(s.forward[1]) > 0.99f ? glm::vec3{0.0f, 0.0f, 1.0f} : glm::vec3{0.0f, 1.0f, 0.0f},
                           s.forward));
            s.up = glm::cross(s.forward, s.right);
        }
        s.anchor = {Snap(glm::dot(focus, s.right), s.static_texel), Snap(glm::dot(focus, s.up), s.static_texel),
                    glm::dot(focus, s.forward)};
        valid_ = true;
    }
    drawn_.dynamic_center = {Snap(glm::dot(focus, drawn_.right), drawn_.dynamic_texel),
                             Snap(glm::dot(focus, drawn_.up), drawn_.dynamic_texel)};
    frame.layers = drawn_;
    return frame;
}

std::array<float, 16> SunPlace::View(glm::vec3 origin) const
{
    const float tx = glm::dot(origin, right) - center[0], ty = glm::dot(origin, up) - center[1],
                tz = glm::dot(origin, forward) - near_plane;
    return {right[0], up[0], forward[0], 0.0f, right[1], up[1], forward[1], 0.0f,
            right[2], up[2], forward[2], 0.0f, tx,       ty,    tz,         1.0f};
}

std::array<float, 16> SunPlace::Projection() const
{
    const float depth = far_plane - near_plane;
    return {1.0f / half[0], 0.0f, 0.0f,         0.0f, 0.0f, 1.0f / half[1], 0.0f, 0.0f,
            0.0f,           0.0f, 1.0f / depth, 0.0f, 0.0f, 0.0f,           0.0f, 1.0f};
}

glm::vec3 SunPlace::Coord(glm::vec3 world) const
{
    return {0.5f + 0.5f * (glm::dot(world, right) - center[0]) / half[0],
            0.5f - 0.5f * (glm::dot(world, up) - center[1]) / half[1],
            (glm::dot(world, forward) - near_plane) / (far_plane - near_plane)};
}

bool SunPlace::Reaches(glm::vec3 center_world, float radius) const
{
    const float x = glm::dot(center_world, right) - center[0], y = glm::dot(center_world, up) - center[1],
                z = glm::dot(center_world, forward);
    return std::fabs(x) <= half[0] + radius && std::fabs(y) <= half[1] + radius && z >= near_plane - radius &&
           z <= far_plane + radius;
}

SunPlace ComputeSunPlace(glm::vec3 sun, std::span<const glm::vec3> ground, float reach, int size)
{
    SunPlace place;
    place.size = size;
    place.sun = sun;
    place.forward = glm::normalize(-sun);
    place.right = glm::normalize(
        glm::cross(std::fabs(place.forward[1]) > 0.99f ? glm::vec3{0.0f, 0.0f, 1.0f} : glm::vec3{0.0f, 1.0f, 0.0f},
                   place.forward));
    place.up = glm::cross(place.forward, place.right);
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (const glm::vec3 &p : ground)
    {
        const float l[3] = {glm::dot(p, place.right), glm::dot(p, place.up), glm::dot(p, place.forward)};
        for (int k = 0; k < 3; ++k)
        {
            lo[k] = std::min(lo[k], l[k]);
            hi[k] = std::max(hi[k], l[k]);
        }
    }
    if (ground.empty())
        lo[0] = lo[1] = lo[2] = hi[0] = hi[1] = hi[2] = 0.0f;
    // A margin of a metre and four texels: the texels' own footprint, their offset off the surface, the filter.
    for (int k = 0; k < 2; ++k)
    {
        const float span = std::max(hi[k] - lo[k], 1.0f);
        place.center[k] = 0.5f * (lo[k] + hi[k]);
        place.half[k] = (0.5f * span + 1.0f) * (1.0f + 8.0f / static_cast<float>(std::max(size, 16)));
        place.texel[k] = 2.0f * place.half[k] / static_cast<float>(std::max(size, 1));
    }
    place.near_plane = lo[2] - reach;
    place.far_plane = hi[2] + 2.0f;
    return place;
}

} // namespace storm::fx
