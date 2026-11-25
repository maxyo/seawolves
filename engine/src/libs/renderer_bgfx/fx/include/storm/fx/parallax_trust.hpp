// Where a relief's height can be trusted with the parallax: a per-texel mask made when the relief is generated.
// Design: fx/README.md, "Parallax trust".
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace storm::fx
{

inline constexpr float kTrustClosingTexels = 14.0f;
inline constexpr float kTrustFeatherSigma = 2.5f;

// The detectors' thresholds. Strict (house atlases, everything else: when in doubt, mask it out): a recess
// deeper than 0.15 of the height's range and darker than 0.9 of its surroundings; glass below 0.22 luminance and 0.04
// local deviation; grown by 8 texels. Masonry (class Stone: cobbles, stone and brick walls, where the hollows between
// stones look like recesses to the strict one: measured, it masked 40 % of a plain stone wall): 0.22 / 0.8, glass 0.16
// / 0.025, grown by 5. Both keep recesses to blobs of at least 120 texels (windows and doors; the hollows of stones are
// smaller) that fill at least `pit_extent` of their box.
struct TrustParams
{
    float pit_depth, pit_dark, pit_min_area, pit_extent;
    float glass_luminance, glass_flat, glass_min_area;
    float grow_texels;
};
inline constexpr TrustParams kTrustStrict{0.15f, 0.9f, 120.0f, 0.45f, 0.22f, 0.04f, 40.0f, 8.0f};
inline constexpr TrustParams kTrustMasonry{0.22f, 0.8f, 120.0f, 0.55f, 0.16f, 0.025f, 60.0f, 5.0f};

// A texture whose trusted texels (trust above one half) are fewer than this share of it gets no parallax.
inline constexpr float kParallaxMinCoverage = 0.5f;

// The trust, 0..1 per texel of a w x h map, from `rgba` (the colour texture at the same size, 4 bytes a texel, R first)
// and `height` (0..1, the map's height before the mask). Empty on mismatched sizes.
std::vector<float> ParallaxTrust(std::span<const uint8_t> rgba, std::span<const float> height, int w, int h,
                                 const TrustParams &params = kTrustStrict);

// The share of texels with a trust above one half.
float TrustCoverage(std::span<const float> trust);

} // namespace storm::fx
