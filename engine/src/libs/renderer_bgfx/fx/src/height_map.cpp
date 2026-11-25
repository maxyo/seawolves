#include "storm/fx/height_map.hpp"

#include "image_field.hpp"

#include "storm/fx/parallax_trust.hpp"
#include "storm/fx/relief_store.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace storm::fx
{
bool ReliefHasHeight(ReliefClass relief)
{
    switch (relief)
    {
    case ReliefClass::Stone:
    case ReliefClass::Wood:
    case ReliefClass::Building:
    case ReliefClass::Plaster:
    case ReliefClass::Metal:
    case ReliefClass::Prop:
    case ReliefClass::HeightMap:
    case ReliefClass::Character:
        return true;
    case ReliefClass::None:
        return false;
    }
    return false;
}

float ReliefDepthTexels(ReliefClass relief)
{
    switch (relief)
    {
    case ReliefClass::Stone:
        return 6.0f;
    case ReliefClass::Building:
        return 4.0f;
    case ReliefClass::HeightMap: // the game's own detail heights, tiled small over their surface
        return 1.5f;
    case ReliefClass::Plaster:
        return 1.25f;
    case ReliefClass::Wood:
        return 1.0f;
    case ReliefClass::Character: // the shallow character depth
        return 2.0f;
    case ReliefClass::Prop:
        return 0.75f;
    case ReliefClass::Metal: // cannons, fittings: curved small parts, a shallow relief
        return 0.375f;
    case ReliefClass::None:
        return 0.0f;
    }
    return 0.0f;
}

void RefreshReliefDepth(NormalMap &map, ReliefClass relief)
{
    // A material without a height (AddGlossMaterial: depth 0) keeps none.
    map.depth = map.material.empty() || !(map.depth > 0.0f)
                    ? 0.0f
                    : ReliefDepthTexels(relief) * static_cast<float>(map.width) / static_cast<float>(kReliefMapSize);
}

bool ReliefTakesParallax(ReliefClass relief)
{
    return relief == ReliefClass::Stone || relief == ReliefClass::Building || relief == ReliefClass::Character;
}

bool AddReliefMaterial(NormalMap &map, ReliefClass relief, std::span<const float> height,
                       std::span<const uint8_t> gloss, std::span<const uint8_t> albedo)
{
    map.material.clear();
    map.trust_coverage = 1.0f;
    map.depth = 0.0f;
    const int w = map.width, h = map.height;
    const size_t texels = static_cast<size_t>(w) * h;
    if (map.format != NormalMapFormat::Rg8 || w < 4 || h < 4 || !ReliefHasHeight(relief) || height.size() != texels ||
        (!gloss.empty() && gloss.size() != texels) || (!albedo.empty() && albedo.size() != texels * 4))
        return false;

    // Each region against its own neighbourhood: (h - mean) / spread, the spread floored so that a plain wall's noise
    // is not blown up into relief.
    const float scale = static_cast<float>(w) / static_cast<float>(kReliefMapSize), sigma = kHeightRegionTexels * scale;
    std::vector<float> v(height.begin(), height.end());
    const std::vector<float> mean = img::Gaussian({w, h, true, v}, sigma).values;
    std::vector<float> square(texels);
    for (size_t t = 0; t < texels; ++t)
        square[t] = (v[t] - mean[t]) * (v[t] - mean[t]);
    const std::vector<float> spread = img::Gaussian({w, h, true, std::move(square)}, sigma).values;
    constexpr float kFloor = 0.03f; // of the model's 0..1: below it a region is flat
    for (size_t t = 0; t < texels; ++t)
        v[t] = (v[t] - mean[t]) / std::sqrt(spread[t] + kFloor * kFloor);
    std::vector<float> sorted = v;
    const auto at = [&](double share) {
        const size_t i = std::min(texels - 1, static_cast<size_t>(share * static_cast<double>(texels - 1)));
        std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(i), sorted.end());
        return sorted[i];
    };
    const float low = at(0.02), high = at(0.98);
    if (!(high - low > 1e-3f))
        return false;

    // RG8 mips: each the box average of the one above (the depth stays in texels of the top mip).
    img::Field rh{w, h, true, std::vector<float>(texels)}, rg{w, h, true, std::vector<float>(texels)};
    for (size_t t = 0; t < texels; ++t)
    {
        rh.values[t] = std::clamp((v[t] - low) / (high - low), 0.0f, 1.0f);
        rg.values[t] = gloss.empty() ? 0.0f : gloss[t] / 255.0f;
    }
    // Where the height cannot be trusted (windows, glass: parallax_trust.hpp) it goes to the top, by the trust: the
    // parallax shifts nothing there. A texture with almost no trusted texel keeps its height whole (the cavity reads
    // it) and gets no parallax at all (kParallaxMinCoverage, the draw).
    if (!albedo.empty())
    {
        const std::vector<float> trust =
            ParallaxTrust(albedo, rh.values, w, h, relief == ReliefClass::Stone ? kTrustMasonry : kTrustStrict);
        map.trust_coverage = TrustCoverage(trust);
        if (map.trust_coverage >= kParallaxMinCoverage)
            for (size_t t = 0; t < texels; ++t)
                rh.values[t] = 1.0f - trust[t] * (1.0f - rh.values[t]);
    }
    map.material.reserve(map.rg.size());
    for (int mip = 0; mip < map.mip_count; ++mip)
    {
        for (size_t t = 0; t < rh.values.size(); ++t)
        {
            map.material.push_back(static_cast<uint8_t>(std::lround(rh.values[t] * 255.0f)));
            map.material.push_back(static_cast<uint8_t>(std::lround(rg.values[t] * 255.0f)));
        }
        if (mip + 1 == map.mip_count)
            break;
        rh = img::Halve(rh);
        rg = img::Halve(rg);
    }
    map.depth = 1.0f; // a height: RefreshReliefDepth gives the class's depth
    RefreshReliefDepth(map, relief);
    return true;
}

bool AddGlossMaterial(NormalMap &map, std::span<const uint8_t> gloss)
{
    map.material.clear();
    map.trust_coverage = 1.0f;
    map.depth = 0.0f;
    const int w = map.width, h = map.height;
    if (map.format != NormalMapFormat::Rg8 || w < 4 || h < 4 || gloss.size() != static_cast<size_t>(w) * h)
        return false;
    img::Field g{w, h, true, std::vector<float>(gloss.size())};
    for (size_t t = 0; t < g.values.size(); ++t)
        g.values[t] = gloss[t] / 255.0f;
    map.material.reserve(map.rg.size());
    for (int mip = 0; mip < map.mip_count; ++mip)
    {
        for (const float v : g.values)
        {
            map.material.push_back(255); // R: the top everywhere, no height
            map.material.push_back(static_cast<uint8_t>(std::lround(v * 255.0f)));
        }
        if (mip + 1 == map.mip_count)
            break;
        g = img::Halve(g);
    }
    return true;
}

} // namespace storm::fx
