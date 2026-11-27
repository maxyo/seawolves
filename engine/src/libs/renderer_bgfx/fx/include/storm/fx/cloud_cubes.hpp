// Cloud layers for a game that ships no cloud textures, made at load in the textured layers' form (renderer_bgfx/fx
// README, "Sky"): opacity and the response to light from the six axes, drawn by the same path.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace storm::fx
{

// One layer's two cubes, faces in bgfx's order (+x, -x, +y, -y, +z, -z), rows from the top, RGBA8. A: the response to
// light from +x, +y, +z times the opacity, the opacity in alpha; B: the same from -x, -y, -z, alpha unused (the
// ambient response is the mean of the six). Responses are shown times `radiance_scale`.
struct CloudCube
{
    int size = 0;
    std::vector<uint8_t> a, b;
    float radiance_scale = 1.0f;
};

// The cover a threshold on the fair-weather layer's alpha leaves: `threshold[i]` keeps the share i / kCoverSteps of
// the sky above the horizon (by solid angle) at or above it.
inline constexpr int kCoverSteps = 16;
// The opacity rises from 0 to 1 over this much of the alpha about the threshold.
inline constexpr float kCloudEdgeWidth = 0.08f;

struct GeneratedClouds
{
    CloudCube base, rain;
    std::array<float, kCoverSteps + 1> threshold{};
    // The base layer's opacity at alpha `a` is clamp((a - from) * scale, 0, 1) for this cover (0..1).
    struct Remap
    {
        float from = 0.0f, scale = 1.0f;
    };
    Remap ForCover(float cover) const;
};

inline constexpr int kBaseCloudSize = 256, kRainCloudSize = 128;
// The layers' base (metres); the height over which the fair-weather layer's density falls by one; the rain deck's
// depth.
inline constexpr float kCloudBase = 1500.0f, kCloudRise = 1100.0f, kRainDepth = 2000.0f;

GeneratedClouds GenerateClouds();

} // namespace storm::fx
