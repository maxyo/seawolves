#include "storm/fx/cloud_cubes.hpp"

#include "storm/fx/sky_model.hpp"

#include "parallel_for.hpp"

#include <storm_glm.h>

#include <glm/gtc/noise.hpp>

#include <algorithm>
#include <cmath>

namespace storm::fx
{
namespace
{
// A cube of one float per texel, faces in bgfx's order.
struct Field
{
    int size = 0;
    std::vector<float> v;
    float peak = 0.0f; // the largest value

    float at(int face, int x, int y) const
    {
        return v[(size_t(face) * size_t(size) + size_t(y)) * size_t(size) + size_t(x)];
    }
    // Bilinear within the face `dir` falls on.
    float sample(glm::vec3 dir) const;
};

// The direction through texel (x, y) of `face` (unnormalised: its length grows towards the face's corners).
glm::vec3 TexelDirection(int face, int x, int y, int size)
{
    const float s = 2.0f * (float(x) + 0.5f) / float(size) - 1.0f;
    const float t = 2.0f * (float(y) + 0.5f) / float(size) - 1.0f;
    switch (face)
    {
    case 0:
        return {1.0f, -t, -s};
    case 1:
        return {-1.0f, -t, s};
    case 2:
        return {s, 1.0f, t};
    case 3:
        return {s, -1.0f, -t};
    case 4:
        return {s, -t, 1.0f};
    default:
        return {-s, -t, -1.0f};
    }
}

float Field::sample(glm::vec3 d) const
{
    const glm::vec3 a = glm::abs(d);
    int face;
    float s, t, m;
    if (a.x >= a.y && a.x >= a.z)
    {
        m = a.x;
        face = d.x > 0.0f ? 0 : 1;
        s = d.x > 0.0f ? -d.z : d.z;
        t = -d.y;
    }
    else if (a.y >= a.z)
    {
        m = a.y;
        face = d.y > 0.0f ? 2 : 3;
        s = d.x;
        t = d.y > 0.0f ? d.z : -d.z;
    }
    else
    {
        m = a.z;
        face = d.z > 0.0f ? 4 : 5;
        s = d.z > 0.0f ? d.x : -d.x;
        t = -d.y;
    }
    const float fx = std::clamp((s / m * 0.5f + 0.5f) * float(size) - 0.5f, 0.0f, float(size - 1));
    const float fy = std::clamp((t / m * 0.5f + 0.5f) * float(size) - 0.5f, 0.0f, float(size - 1));
    const int x0 = int(fx), y0 = int(fy);
    const int x1 = std::min(x0 + 1, size - 1), y1 = std::min(y0 + 1, size - 1);
    const float u = fx - float(x0), w = fy - float(y0);
    return glm::mix(glm::mix(at(face, x0, y0), at(face, x1, y0), u), glm::mix(at(face, x0, y1), at(face, x1, y1), u),
                    w);
}

// Octaves of simplex noise from `wavelength` down, each half as long and as strong; an octave shorter than about
// four times the texel's footprint on the layer fades out (its mean, 0), so the horizon does not alias.
float Octaves(glm::vec2 p, float wavelength, int octaves, float footprint, bool billow)
{
    float sum = 0.0f, weight = 0.0f, amplitude = 1.0f;
    for (int i = 0; i < octaves; ++i, wavelength *= 0.5f, amplitude *= 0.5f)
    {
        const float keep = 1.0f - glm::smoothstep(0.125f, 0.25f, footprint / wavelength);
        weight += amplitude;
        if (keep <= 0.0f)
            continue;
        // Offsets keep the octaves from lining up at the origin.
        const float n = glm::simplex(p / wavelength + glm::vec2(float(i) * 17.3f, float(i) * -9.1f));
        sum += amplitude * keep * (billow ? 2.0f * std::fabs(n) - 1.0f : n);
    }
    return sum / weight;
}

// Round puffs: domes over the nearest of one jittered point per cell, a cell wide (Worley, "A cellular texture basis
// function", SIGGRAPH 1996). GLM has gradient noises only.
float Puffs(glm::vec2 p)
{
    const glm::vec2 cell = glm::floor(p);
    float nearest = 2.0f;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
        {
            const glm::vec2 c = cell + glm::vec2(float(x), float(y));
            uint32_t h = uint32_t(int32_t(c.x)) * 73856093u ^ uint32_t(int32_t(c.y)) * 19349663u;
            h = (h ^ (h >> 13)) * 0x5bd1e995u;
            h ^= h >> 15;
            const glm::vec2 jitter{float(h & 0xffffu) / 65536.0f, float(h >> 16) / 65536.0f};
            nearest = std::min(nearest, glm::length(c + jitter - p));
        }
    return 1.0f - std::min(nearest * nearest, 1.0f);
}

// The fair-weather clouds' density on the layer at `p` (metres): large masses, and on them heaped puffs on puffs,
// warped so they do not sit on a grid, eroded at their thin edges.
float CumulusDensity(glm::vec2 p, float footprint)
{
    const glm::vec2 warp{Octaves(p, 900.0f, 2, footprint, false),
                         Octaves(p + glm::vec2(5200.0f), 900.0f, 2, footprint, false)};
    const glm::vec2 q = p + 220.0f * warp;
    const float masses = 0.5f + 0.5f * Octaves(q, 9000.0f, 3, footprint, false);
    // A puff's mean where it is smaller than the texel's footprint.
    const auto puffs = [&](float size) { return glm::mix(Puffs(q / size), 0.5f, glm::smoothstep(0.25f, 0.5f, footprint / size)); };
    const float heaps = 0.6f * puffs(1100.0f) + 0.4f * puffs(430.0f);
    const float shape = 0.45f * masses + 0.55f * heaps;
    const float erosion = 0.5f + 0.5f * Octaves(q, 170.0f, 2, footprint, false);
    return shape - 0.25f * erosion * (1.0f - glm::smoothstep(0.35f, 0.75f, shape));
}

float RainDensity(glm::vec2 p, float footprint)
{
    const glm::vec2 warp{Octaves(p, 3000.0f, 2, footprint, false),
                         Octaves(p + glm::vec2(9100.0f), 3000.0f, 2, footprint, false)};
    return 0.5f + 0.5f * Octaves(p + 900.0f * warp, 7000.0f, 5, footprint, false);
}

// A layer's clouds in space: above the base the layer's density less the square of the height over `rise` metres,
// so a cloud the cover's threshold keeps has a flat base, steep sides and a round top that rises the higher the
// denser it is; a rain deck is one cloud about `rise` metres deep, its base lower where it is denser.
struct LayerShape
{
    float rise;
    bool deck;
    float (*density)(glm::vec2, float);
};

// The layer's density at `p` on it and `height` over its base.
float Density(const LayerShape &shape, float d, float height)
{
    const float h = height / shape.rise;
    return shape.deck ? (h >= 0.35f * (1.0f - d) && h <= 0.45f + 0.55f * d ? 1.0f : 0.0f) : d - h * h;
}

// Light through `path` metres of cloud: single scattering and two broader terms for the light scattered many times
// (Wrenninge's octaves, "Production volume rendering", SIGGRAPH 2017).
float Response(float path)
{
    constexpr float kExtinction = 1.0f / 120.0f; // per metre
    const float tau = path * kExtinction;
    return 0.6f * std::exp(-tau) + 0.3f * std::exp(-tau * 0.25f) + 0.1f * std::exp(-tau * 0.0625f);
}

// Per texel of a layer: the alpha the cover thresholds (the largest density the view ray meets: the pixel is cloud
// where that passes the threshold, at any cover), its solid angle above the horizon and the six responses where the
// eye meets the cloud.
struct Texel
{
    float alpha = 0.0f, weight = 0.0f;
    std::array<float, 6> response{}; // +x, +y, +z, -x, -y, -z
};

glm::vec2 OnLayer(glm::vec3 dir)
{
    return glm::vec2(dir.x, dir.z) * (kCloudBase / dir.y);
}

float LayerAt(const Field &field, glm::vec2 p)
{
    return field.sample({p.x, kCloudBase, p.y});
}

// `reference`: the threshold the responses are worked out at (the clouds of a middle cover).
Texel MarchTexel(const LayerShape &shape, const Field &field, glm::vec3 raw, int size, float reference)
{
    Texel out;
    const glm::vec3 v = glm::normalize(raw);
    if (v.y <= 0.0f)
        return out;
    const float len2 = glm::dot(raw, raw);
    out.weight = 4.0f / float(size * size) / (len2 * std::sqrt(len2));
    const float angle = 2.0f / float(size) / std::sqrt(len2);
    // Along the view ray from the base up to where no density can pass: steps no finer than the texel's width there.
    const float near = kCloudBase / v.y;
    const float far = near + std::min(shape.rise / v.y, 40000.0f);
    const float peak = shape.deck ? 1.0f : field.peak;
    struct Step
    {
        float along, height, density;
    };
    std::vector<Step> steps;
    steps.reserve(64);
    float densest = -1.0f;
    for (float s = near; s <= far; s += std::max(30.0f, 0.5f * s * angle))
    {
        const glm::vec3 p = v * s;
        const float height = p.y - kCloudBase;
        // Higher up nothing denser can be met.
        if (!shape.deck && peak - (height / shape.rise) * (height / shape.rise) < densest)
            break;
        steps.push_back({s, height, Density(shape, LayerAt(field, {p.x, p.z}), height)});
        densest = std::max(densest, steps.back().density);
    }
    out.alpha = shape.deck ? 1.0f : densest;
    // The eye meets the first point dense enough for the reference cover (or, for a cloud only a larger cover keeps,
    // nearly its densest).
    const float cut = shape.deck ? 0.5f : std::min(reference, densest - 0.02f);
    const Step *seen = &steps.front();
    for (const Step &step : steps)
        if (step.density >= cut)
        {
            seen = &step;
            break;
        }
    const glm::vec2 at = glm::vec2(v.x, v.z) * seen->along;
    const float y = std::max(seen->height, 0.0f);
    const auto inside = [&](glm::vec2 p, float height) {
        return Density(shape, LayerAt(field, p), height) >= cut;
    };
    // Up to the cloud's top, down to its base, and through its neighbours along the four horizontal axes.
    constexpr int kSteps = 10;
    constexpr float kStep = 60.0f;
    float up = 0.0f;
    for (int i = 1; i <= kSteps * 2 && inside(at, y + kStep * float(i)); ++i)
        up += kStep;
    out.response[1] = Response(up);
    out.response[4] = Response(y);
    const std::array<glm::vec2, 4> sides = {{{1.0f, 0.0f}, {0.0f, 1.0f}, {-1.0f, 0.0f}, {0.0f, -1.0f}}};
    const std::array<size_t, 4> slot = {0, 2, 3, 5};
    for (size_t k = 0; k < sides.size(); ++k)
    {
        float path = 0.0f;
        for (int i = 1; i <= kSteps; ++i)
            path += inside(at + sides[k] * (kStep * float(i)), y) ? kStep : 0.0f;
        out.response[slot[k]] = Response(path);
    }
    return out;
}

// The alpha at or above which the share `cover` of the weighted texels lies, for each step of the cover.
std::array<float, kCoverSteps + 1> CoverThresholds(const std::vector<Texel> &texels)
{
    std::vector<std::pair<float, float>> sorted; // alpha, weight; densest first
    float total = 0.0f;
    for (const Texel &t : texels)
        if (t.weight > 0.0f)
        {
            sorted.emplace_back(t.alpha, t.weight);
            total += t.weight;
        }
    std::ranges::sort(sorted, std::greater<>{});
    std::array<float, kCoverSteps + 1> threshold{};
    float above = 0.0f;
    size_t i = 0;
    for (int step = 0; step <= kCoverSteps; ++step)
    {
        const float want = total * float(step) / float(kCoverSteps);
        while (i < sorted.size() && above + sorted[i].second < want)
            above += sorted[i++].second;
        threshold[size_t(step)] = sorted[std::min(i, sorted.size() - 1)].first;
    }
    // Nothing covered: above the densest texel.
    threshold[0] = sorted.front().first + kCloudEdgeWidth;
    return threshold;
}

float Opacity(float alpha, GeneratedClouds::Remap remap)
{
    return std::clamp((alpha - remap.from) * remap.scale, 0.0f, 1.0f);
}

// The responses scaled so their means over the clouds (opacity-weighted) are Caribbean Legend's cubes' (`positive`,
// `negative`), written premultiplied by the alpha.
CloudCube Pack(const std::vector<Texel> &texels, int size, glm::vec3 positive, glm::vec3 negative,
               GeneratedClouds::Remap mean_cover)
{
    std::array<double, 6> sum{};
    double weight = 0.0;
    for (const Texel &t : texels)
    {
        const double w = double(t.weight) * Opacity(t.alpha, mean_cover);
        for (size_t c = 0; c < 6; ++c)
            sum[c] += w * t.response[c];
        weight += w;
    }
    const std::array<float, 6> target = {positive.x, positive.y, positive.z, negative.x, negative.y, negative.z};
    std::array<float, 6> gain{};
    std::vector<float> values;
    for (size_t c = 0; c < 6; ++c)
    {
        gain[c] = target[c] / float(std::max(sum[c] / weight, 1e-6));
        for (const Texel &t : texels)
            if (t.weight > 0.0f)
                values.push_back(t.response[c] * gain[c]);
    }
    // The scale leaves out the brightest few texels (clamped): the 8 bits go to the rest.
    const auto top = values.begin() + std::ptrdiff_t(values.size() * 995 / 1000);
    std::ranges::nth_element(values, top);
    CloudCube cube;
    cube.size = size;
    cube.radiance_scale = *top;
    cube.a.resize(texels.size() * 4);
    cube.b.resize(texels.size() * 4);
    const auto byte = [](float v) { return uint8_t(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    // sRGB-encoded: the dim responses inside the clouds keep their steps small.
    const auto srgb = [&](float v) {
        v = std::clamp(v, 0.0f, 1.0f);
        return byte(v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f);
    };
    for (size_t i = 0; i < texels.size(); ++i)
    {
        const Texel &t = texels[i];
        const float k = t.alpha / cube.radiance_scale;
        for (size_t c = 0; c < 3; ++c)
        {
            cube.a[i * 4 + c] = srgb(t.response[c] * gain[c] * k);
            cube.b[i * 4 + c] = srgb(t.response[c + 3] * gain[c + 3] * k);
        }
        cube.a[i * 4 + 3] = byte(t.alpha);
        cube.b[i * 4 + 3] = 255;
    }
    return cube;
}

std::vector<Texel> MakeLayer(const LayerShape &shape, int size, ParallelFor &workers)
{
    Field field;
    field.size = size;
    field.v.assign(size_t(6) * size_t(size) * size_t(size), 0.0f);
    const auto rows = size_t(6 * size);
    workers.run(rows, [&](size_t row) {
        const int face = int(row) / size, y = int(row) % size;
        for (int x = 0; x < size; ++x)
        {
            const glm::vec3 raw = TexelDirection(face, x, y, size);
            const glm::vec3 v = glm::normalize(raw);
            // Below the horizon the layer's field continues from a little above it.
            const glm::vec3 up = glm::normalize(glm::vec3(v.x, std::max(v.y, 0.01f), v.z));
            const float angle = 2.0f / float(size) / std::sqrt(glm::dot(raw, raw));
            const float footprint = kCloudBase * angle / (up.y * up.y);
            field.v[row * size_t(size) + size_t(x)] = shape.density(OnLayer(up), footprint);
        }
    });
    field.peak = *std::ranges::max_element(field.v);
    // The responses are worked out for the clouds of half the sky's cover on the layer itself.
    std::vector<Texel> plain(field.v.size());
    for (size_t i = 0; i < plain.size(); ++i)
    {
        const glm::vec3 raw = TexelDirection(int(i / size_t(size * size)), int(i % size_t(size)),
                                             int(i / size_t(size)) % size, size);
        if (raw.y > 0.0f)
            plain[i] = {field.v[i], 1.0f / std::pow(glm::dot(raw, raw), 1.5f), {}};
    }
    const float reference = CoverThresholds(plain)[kCoverSteps / 2];
    std::vector<Texel> texels(field.v.size());
    workers.run(rows, [&](size_t row) {
        const int face = int(row) / size, y = int(row) % size;
        for (int x = 0; x < size; ++x)
            texels[row * size_t(size) + size_t(x)] =
                MarchTexel(shape, field, TexelDirection(face, x, y, size), size, reference);
    });
    return texels;
}
} // namespace

GeneratedClouds::Remap GeneratedClouds::ForCover(float cover) const
{
    const float at = std::clamp(cover, 0.0f, 1.0f) * float(kCoverSteps);
    const int i = std::min(int(at), kCoverSteps - 1);
    const float t = glm::mix(threshold[size_t(i)], threshold[size_t(i + 1)], at - float(i));
    // Half opaque at the threshold.
    return {t - 0.5f * kCloudEdgeWidth, 1.0f / kCloudEdgeWidth};
}

GeneratedClouds GenerateClouds()
{
    ParallelFor workers;
    GeneratedClouds out;
    const std::vector<Texel> base = MakeLayer({kCloudRise, false, CumulusDensity}, kBaseCloudSize, workers);
    out.threshold = CoverThresholds(base);
    out.base = Pack(base, kBaseCloudSize, kCumulusPositive, kCumulusNegative, out.ForCover(0.5f));
    const std::vector<Texel> rain = MakeLayer({kRainDepth, true, RainDensity}, kRainCloudSize, workers);
    out.rain = Pack(rain, kRainCloudSize, kRainPositive, kRainNegative, {0.0f, 1.0f});
    return out;
}

} // namespace storm::fx
