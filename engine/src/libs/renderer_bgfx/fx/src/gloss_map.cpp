#include "storm/fx/gloss_map.hpp"
#include "storm/fx/model_digests.hpp"
#include "storm/fx/upscale.hpp"

#include "image_field.hpp"
#include "name_rules.hpp"
#include "storm/fx/relief_model.hpp"
#include "fnv_hash.hpp"

#include <algorithm>
#include <cmath>

namespace storm::fx
{
namespace
{
using namespace img;

const char *const kClassNames[] = {"none",    "matte",     "dry wood", "painted wood", "lacquered wood",
                                   "stone",   "wet stone", "building", "interior",     "tile",
                                   "leather", "metal",     "glass",    "character"};

// The class caps: the matte classes stay matte whatever the network reads (its smooth patches on cloth, ground and
// plaster are baked light, not polish).
float ClassCap(GlossClass gloss)
{
    switch (gloss)
    {
    case GlossClass::Matte:
        return 0.1f;
    case GlossClass::Stone:
        return 0.35f;
    case GlossClass::Character:
        return 0.3f; // fxGlossSpec(0.3): a faint, broad sheen, a fifth of lacquer's
    default:
        return 1.0f;
    }
}

float Smooth(float edge0, float edge1, float x)
{
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Hue in degrees (0 red, 60 yellow, 120 green, 240 blue) of an RGB texel, 0..1 channels.
float Hue(float r, float g, float b, float max, float min)
{
    const float c = max - min;
    if (c < 1e-4f)
        return 0.0f;
    float h;
    if (max == r)
        h = std::fmod((g - b) / c + 6.0f, 6.0f);
    else if (max == g)
        h = (b - r) / c + 2.0f;
    else
        h = (r - g) / c + 4.0f;
    return h * 60.0f;
}
} // namespace

const char *GlossClassName(GlossClass gloss)
{
    const auto index = static_cast<size_t>(gloss);
    return index < std::size(kClassNames) ? kClassNames[index] : "?";
}

GlossVerdict ClassifyGlossName(std::string_view path)
{
    for (const rules::Rule &rule : rules::kRules)
        if (rule.gloss && rules::Matches(path, rule.folder, rule.match, rule.pattern))
            return {*rule.gloss, rule.text};
    return {};
}

uint64_t GlossMethod(GlossClass gloss)
{
    // The roughness network runs on the texture's ×2 when it is small enough (ReliefMapsFromUpscale): the upscaler's
    // digest too.
    return storm::hash::fnvRound(((uint64_t{kGlossMethod} << 8) | static_cast<uint64_t>(gloss)),
                                 kReliefRoughnessModelDigest) ^
           kUpscaleModelDigest;
}

float GlossMetalTint(GlossClass gloss)
{
    return gloss == GlossClass::Metal ? 0.7f : 0.0f;
}

namespace
{
Field Plane(int width, int height)
{
    Field f{width, height, true, {}};
    f.values.resize(static_cast<size_t>(width) * height);
    return f;
}
} // namespace

std::vector<float> GlossMetalMask(std::span<const uint8_t> rgba, int width, int height)
{
    if (width <= 0 || height <= 0 || rgba.size() < static_cast<size_t>(width) * height * 4)
        return {};
    Field r = Plane(width, height), g = r, b = r, lum = r;
    for (size_t i = 0; i < r.values.size(); ++i)
    {
        r.values[i] = rgba[i * 4] / 255.0f;
        g.values[i] = rgba[i * 4 + 1] / 255.0f;
        b.values[i] = rgba[i * 4 + 2] / 255.0f;
        lum.values[i] = 0.299f * r.values[i] + 0.587f * g.values[i] + 0.114f * b.values[i];
    }
    // The colour blurred a little (single noisy texels of grime must not flip between materials); the local contrast
    // over sigma 3 texels of the map (rust is grainy, polished metal flat).
    const Field cr = Gaussian(r, 1.5f), cg = Gaussian(g, 1.5f), cb = Gaussian(b, 1.5f), mean = Gaussian(lum, 3.0f);
    Field square = lum;
    for (size_t i = 0; i < square.values.size(); ++i)
        square.values[i] = (lum.values[i] - mean.values[i]) * (lum.values[i] - mean.values[i]);
    const Field contrast = Gaussian(square, 3.0f);
    std::vector<float> mask(r.values.size());
    for (size_t i = 0; i < mask.size(); ++i)
    {
        const float hi = std::max({cr.values[i], cg.values[i], cb.values[i]}),
                    lo = std::min({cr.values[i], cg.values[i], cb.values[i]});
        const float sat = hi > 1e-3f ? (hi - lo) / hi : 0.0f, l = lum.values[i];
        const float hue = Hue(cr.values[i], cg.values[i], cb.values[i], hi, lo);
        const float flat = 1.0f - Smooth(0.02f, 0.10f, std::sqrt(std::max(contrast.values[i], 0.0f)));
        // Rust: orange-brown, saturated, not bright, grainy (brass and gold are brighter and more yellow).
        const float rust = hue > 8.0f && hue < 40.0f
                               ? Smooth(0.4f, 0.65f, sat) * (1.0f - Smooth(0.2f, 0.34f, l)) * (1.0f - 0.7f * flat)
                               : 0.0f;
        // The gun atlases hold their carriages too: brown, grainy wood, less saturated than brass.
        const float wood = hue > 12.0f && hue < 42.0f ? Smooth(0.25f, 0.4f, sat) * (1.0f - Smooth(0.6f, 0.75f, sat)) *
                                                            (1.0f - Smooth(0.4f, 0.55f, l)) * (1.0f - 0.3f * flat)
                                                      : 0.0f;
        mask[i] = std::clamp(1.0f - rust - wood, 0.0f, 1.0f);
    }
    return mask;
}

GlossField GlossOfRoughness(std::span<const float> roughness, std::span<const float> height,
                            std::span<const uint8_t> rgba, int width, int height_texels, GlossClass gloss)
{
    GlossField field;
    const int w = width, h = height_texels;
    const size_t texels = static_cast<size_t>(std::max(w, 0)) * std::max(h, 0);
    if (gloss == GlossClass::None || w <= 0 || h <= 0 || roughness.size() != texels || height.size() != texels ||
        rgba.size() < texels * 4)
        return field;
    // The filters' sigmas are in texels of a 512 map (where they were tuned): halved at 256.
    const float scale = static_cast<float>(std::max(w, h)) / 512.0f;
    // The grooves: texels below their surroundings in the height, by how many of the texture's own spreads.
    Field hf = Plane(w, h);
    std::copy(height.begin(), height.end(), hf.values.begin());
    const Field around = Gaussian(hf, 4.0f * scale);
    double sum = 0.0;
    for (size_t i = 0; i < texels; ++i)
        sum += (hf.values[i] - around.values[i]) * (hf.values[i] - around.values[i]);
    const float spread = std::max(static_cast<float>(std::sqrt(sum / static_cast<double>(texels))), 1e-6f);
    Field lum = Plane(w, h);
    for (size_t i = 0; i < texels; ++i)
        lum.values[i] = (0.299f * rgba[i * 4] + 0.587f * rgba[i * 4 + 1] + 0.114f * rgba[i * 4 + 2]) / 255.0f;
    lum = Gaussian(lum, 1.5f * scale);
    const bool metal = gloss == GlossClass::Metal;
    if (metal)
        field.metal = GlossMetalMask(rgba, w, h);
    const float cap = ClassCap(gloss);
    field.width = w;
    field.height = h;
    field.gloss.resize(texels);
    for (size_t i = 0; i < texels; ++i)
    {
        // GGX's perceptual roughness r (alpha r^2) as the Blinn-Phong power that matches it, on the highlight's scale.
        const float alpha = std::clamp(roughness[i], 0.05f, 1.0f) * std::clamp(roughness[i], 0.05f, 1.0f);
        const float power = std::max(2.0f / (alpha * alpha) - 2.0f, 1.0f);
        float g = std::clamp((std::log2(power) - 3.0f) / 4.5f, 0.0f, 1.0f);
        const float t = std::clamp(((hf.values[i] - around.values[i]) / spread + 1.5f) / 1.5f, 0.0f, 1.0f);
        g *= 0.25f + 0.75f * t * t * (3.0f - 2.0f * t);
        if (metal)
            g = std::max(g, 0.5f * field.metal[i]);
        else
            g *= Smooth(0.06f, 0.25f, lum.values[i]);
        field.gloss[i] = std::min(g, cap);
    }
    return field;
}

NormalMap GlossMaterial(const GlossField &field)
{
    NormalMap map;
    if (field.width <= 0 || field.height <= 0 || field.gloss.size() != static_cast<size_t>(field.width) * field.height)
        return map;
    Field g{field.width, field.height, true, field.gloss}, m = g;
    m.values = field.metal.empty() ? std::vector<float>(g.values.size(), 0.0f) : field.metal;
    map.width = field.width;
    map.height = field.height;
    map.format = NormalMapFormat::Rg8;
    map.rg.reserve(NormalMapBytes(map.width, map.height, NormalMapFormat::Rg8));
    for (;;)
    {
        ++map.mip_count;
        for (size_t i = 0; i < g.values.size(); ++i)
        {
            map.rg.push_back(static_cast<uint8_t>(std::lround(std::clamp(m.values[i], 0.0f, 1.0f) * 255.0f)));
            map.rg.push_back(static_cast<uint8_t>(std::lround(std::clamp(g.values[i], 0.0f, 1.0f) * 255.0f)));
        }
        if (g.width == 1 && g.height == 1)
            break;
        g = Halve(g);
        m = Halve(m);
    }
    return map;
}

} // namespace storm::fx
