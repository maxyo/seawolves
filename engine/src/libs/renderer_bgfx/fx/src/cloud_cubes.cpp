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
float Octaves(glm::vec2 p, float wavelength, int octaves, float footprint)
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
        sum += amplitude * keep * n;
    }
    return sum / weight;
}

glm::vec2 OnLayer(glm::vec3 dir)
{
    return glm::vec2(dir.x, dir.z) * (kCloudBase / dir.y);
}

// ---- the cumulus: the density and light of Schneider's clouds ("The Real-time Volumetric Cloudscapes of Horizon
// Zero Dawn", SIGGRAPH 2015), baked once into the cubes instead of marched per frame ----

// A tileable scalar volume of n^3 voxels over `period` metres, with its box-filtered levels down to 8^3.
struct Volume
{
    std::vector<std::vector<float>> levels;
    int n = 0;
    float period = 0.0f;

    float voxel(int level, int x, int y, int z) const
    {
        const int m = n >> level;
        x &= m - 1;
        y &= m - 1;
        z &= m - 1;
        return levels[size_t(level)][(size_t(z) * size_t(m) + size_t(y)) * size_t(m) + size_t(x)];
    }
    float trilinear(int level, glm::vec3 p) const
    {
        const glm::vec3 q = p / period * float(n >> level) - 0.5f;
        const glm::vec3 f = glm::floor(q), t = q - f;
        const int x = int(f.x), y = int(f.y), z = int(f.z);
        const float c00 = glm::mix(voxel(level, x, y, z), voxel(level, x + 1, y, z), t.x);
        const float c10 = glm::mix(voxel(level, x, y + 1, z), voxel(level, x + 1, y + 1, z), t.x);
        const float c01 = glm::mix(voxel(level, x, y, z + 1), voxel(level, x + 1, y, z + 1), t.x);
        const float c11 = glm::mix(voxel(level, x, y + 1, z + 1), voxel(level, x + 1, y + 1, z + 1), t.x);
        return glm::mix(glm::mix(c00, c10, t.y), glm::mix(c01, c11, t.y), t.z);
    }
    // Filtered to `footprint` metres: between the two levels whose voxels bracket it.
    float sample(glm::vec3 p, float footprint) const
    {
        const float lod = std::clamp(std::log2(footprint / (period / float(n))), 0.0f, float(levels.size() - 1));
        const int l0 = int(lod), l1 = std::min(l0 + 1, int(levels.size()) - 1);
        const float a = trilinear(l0, p);
        return l1 == l0 ? a : glm::mix(a, trilinear(l1, p), lod - float(l0));
    }
};

Volume MakeVolume(int n, float period, ParallelFor &workers, float (*value)(glm::vec3))
{
    Volume v;
    v.n = n;
    v.period = period;
    v.levels.emplace_back(size_t(n) * size_t(n) * size_t(n));
    workers.run(size_t(n), [&](size_t z) {
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
                v.levels[0][(z * size_t(n) + size_t(y)) * size_t(n) + size_t(x)] =
                    value((glm::vec3(float(x), float(y), float(z)) + 0.5f) / float(n));
    });
    for (int m = n / 2; m >= 8; m /= 2)
    {
        std::vector<float> next(size_t(m) * size_t(m) * size_t(m));
        const int level = int(v.levels.size()) - 1;
        for (int z = 0; z < m; ++z)
            for (int y = 0; y < m; ++y)
                for (int x = 0; x < m; ++x)
                {
                    float sum = 0.0f;
                    for (int k = 0; k < 8; ++k)
                        sum += v.voxel(level, 2 * x + (k & 1), 2 * y + (k >> 1 & 1), 2 * z + (k >> 2));
                    next[(size_t(z) * size_t(m) + size_t(y)) * size_t(m) + size_t(x)] = sum / 8.0f;
                }
        v.levels.push_back(std::move(next));
    }
    return v;
}

// Inverted Worley noise ("A cellular texture basis function", SIGGRAPH 1996) over the unit cube, `cells` to a side and
// wrapping: 1 at a cell's feature point, falling to 0 a cell away. GLM has gradient noises only.
float Worley(glm::vec3 p, int cells)
{
    const glm::vec3 q = p * float(cells);
    const glm::ivec3 cell = glm::ivec3(glm::floor(q));
    float nearest = 1.0f;
    for (int z = -1; z <= 1; ++z)
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x)
            {
                const glm::ivec3 c = cell + glm::ivec3(x, y, z);
                const glm::ivec3 w = (c % cells + cells) % cells;
                uint32_t h = uint32_t(w.x) * 73856093u ^ uint32_t(w.y) * 19349663u ^ uint32_t(w.z) * 83492791u;
                h = (h ^ (h >> 13)) * 0x5bd1e995u;
                h ^= h >> 15;
                const glm::vec3 jitter{float(h & 0x3ffu) / 1024.0f, float(h >> 10 & 0x3ffu) / 1024.0f,
                                       float(h >> 20 & 0x3ffu) / 1024.0f};
                nearest = std::min(nearest, glm::length(glm::vec3(c) + jitter - q));
            }
    return 1.0f - nearest;
}

// The dilated noise's value below which the shape is empty.
constexpr float kShapeFloor = 0.45f;

// The clouds' shape: Perlin noise dilated by an inverted Worley FBM (Schneider 2015, "Perlin-Worley"), tileable
// (GLM's periodic Perlin noise).
float PerlinWorley(glm::vec3 p)
{
    float perlin = 0.0f, amplitude = 1.0f, weight = 0.0f;
    for (int f = 2; f <= 8; f *= 2, amplitude *= 0.5f)
    {
        perlin += amplitude * glm::perlin(p * float(f), glm::vec3(float(f)));
        weight += amplitude;
    }
    perlin = std::clamp(0.5f + 0.7f * perlin / weight, 0.0f, 1.0f);
    const float worley = 0.625f * Worley(p, 3) + 0.25f * Worley(p, 6) + 0.125f * Worley(p, 12);
    // remap(perlin, 0, 1, worley, 1), then stretched so the densest masses rise as towers over the cut of a middle
    // cover rather than all standing about as high.
    const float dilated = worley + perlin * (1.0f - worley);
    return std::clamp((dilated - kShapeFloor) / (1.0f - kShapeFloor), 0.0f, 1.0f);
}

// The edges' erosion: one higher-frequency Worley octave (Schneider 2015).
float Erosion(glm::vec3 p)
{
    return Worley(p, 8);
}

// The shape repeats every kShapePeriod metres, the erosion every kErosionPeriod; their volumes' sides (voxels).
constexpr float kShapePeriod = 6000.0f, kErosionPeriod = 1600.0f;
constexpr int kShapeVoxels = 64, kErosionVoxels = 32;
// How far the erosion eats into the shape (Schneider 2015: remap(shape, erosion * kErode, 1, 0, 1)).
constexpr float kErode = 0.3f;

struct Cumulus
{
    Volume shape, erosion;

    // The cumulus' height profile at `h` (0 base .. 1 the highest top): a sharp rise over the base (flat bases) and a
    // fall towards the top, so a cut keeps rounded tops that rise the higher the denser the shape below them.
    static float Gradient(float h)
    {
        return glm::smoothstep(0.0f, 0.05f, h) * std::max(1.0f - h * h, 0.0f);
    }
    // Groups of clouds and clear stretches tens of kilometres across, which also keep the shape's repeats from lining
    // up towards the horizon: a factor on the shape, tabulated every kMassCell metres out to kMassReach.
    static constexpr float kMassCell = 1000.0f, kMassReach = 200000.0f;
    static constexpr int kMassSide = int(2.0f * kMassReach / kMassCell) + 1;
    std::vector<float> masses;

    float Masses(glm::vec2 p) const
    {
        const glm::vec2 q = glm::clamp((p + kMassReach) / kMassCell, 0.0f, float(kMassSide - 1) - 0.001f);
        const int x = int(q.x), z = int(q.y);
        const float u = q.x - float(x), w = q.y - float(z);
        const auto at = [&](int i, int j) { return masses[size_t(j) * kMassSide + size_t(i)]; };
        return glm::mix(glm::mix(at(x, z), at(x + 1, z), u), glm::mix(at(x, z + 1), at(x + 1, z + 1), u), w);
    }
    // At `p` (x, height over the base, z; metres), filtered to `footprint` metres.
    float Density(glm::vec3 p, float footprint) const
    {
        const float g = Gradient(p.y / kCloudDepth);
        if (g <= 0.0f)
            return 0.0f;
        const float base = shape.sample(p, footprint) * Masses({p.x, p.z}) * g;
        // Finer than the footprint the erosion is its mean.
        const float e = glm::mix(erosion.sample(p, footprint), 0.5f,
                                 glm::smoothstep(0.25f, 0.5f, footprint / (kErosionPeriod / 8.0f)));
        const float floor = e * kErode;
        return (base - floor) / (1.0f - floor);
    }
};

float RainDensity(glm::vec2 p, float footprint)
{
    const glm::vec2 warp{Octaves(p, 3000.0f, 2, footprint), Octaves(p + glm::vec2(9100.0f), 3000.0f, 2, footprint)};
    return 0.5f + 0.5f * Octaves(p + 900.0f * warp, 7000.0f, 5, footprint);
}

// Per texel of a layer: the alpha the cover thresholds (the pixel is cloud where that passes the threshold, at any
// cover), its solid angle above the horizon and the six responses where the eye meets the cloud.
struct Texel
{
    float alpha = 0.0f, weight = 0.0f;
    std::array<float, 6> response{}; // +x, +y, +z, -x, -y, -z
};

// The texel's solid angle (`weight`) and angular width.
void TexelSize(glm::vec3 raw, int size, Texel &out, float &angle)
{
    const float len2 = glm::dot(raw, raw);
    out.weight = 4.0f / float(size * size) / (len2 * std::sqrt(len2));
    angle = 2.0f / float(size) / std::sqrt(len2);
}

// The six axes' order in Texel::response.
const std::array<glm::vec3, 6> kAxes = {{{1.0f, 0.0f, 0.0f},
                                         {0.0f, 1.0f, 0.0f},
                                         {0.0f, 0.0f, 1.0f},
                                         {-1.0f, 0.0f, 0.0f},
                                         {0.0f, -1.0f, 0.0f},
                                         {0.0f, 0.0f, -1.0f}}};

// ---- the rain deck: one cloud about kRainDepth deep, its base lower where it is denser ----

bool InDeck(float density, float height)
{
    const float h = height / kRainDepth;
    return h >= 0.35f * (1.0f - density) && h <= 0.45f + 0.55f * density;
}

// Light through `path` metres of the deck: single scattering and two broader terms for the light scattered many times
// (Wrenninge's octaves, "Production volume rendering", SIGGRAPH 2017).
float DeckResponse(float path)
{
    constexpr float kExtinction = 1.0f / 120.0f; // per metre
    const float tau = path * kExtinction;
    return 0.6f * std::exp(-tau) + 0.3f * std::exp(-tau * 0.25f) + 0.1f * std::exp(-tau * 0.0625f);
}

Texel MarchDeck(const Field &field, glm::vec3 raw, int size)
{
    Texel out;
    const glm::vec3 v = glm::normalize(raw);
    if (v.y <= 0.0f)
        return out;
    float angle;
    TexelSize(raw, size, out, angle);
    out.alpha = 1.0f;
    const auto inside = [&](glm::vec2 p, float height) { return InDeck(field.sample({p.x, kCloudBase, p.y}), height); };
    // The eye meets the deck's base.
    const float near = kCloudBase / v.y, far = near + std::min(kRainDepth / v.y, 40000.0f);
    float s = near;
    while (s < far && !inside(glm::vec2(v.x, v.z) * s, v.y * s - kCloudBase))
        s += std::max(30.0f, 0.5f * s * angle);
    const glm::vec2 at = glm::vec2(v.x, v.z) * s;
    const float y = std::max(v.y * s - kCloudBase, 0.0f);
    // Up to its top, down to its base, and through it along the four horizontal axes.
    constexpr int kSteps = 10;
    constexpr float kStep = 60.0f;
    float up = 0.0f;
    for (int i = 1; i <= kSteps * 2 && inside(at, y + kStep * float(i)); ++i)
        up += kStep;
    out.response[1] = DeckResponse(up);
    out.response[4] = DeckResponse(y);
    for (const size_t k : {size_t(0), size_t(2), size_t(3), size_t(5)})
    {
        float path = 0.0f;
        for (int i = 1; i <= kSteps; ++i)
            path += inside(at + glm::vec2(kAxes[k].x, kAxes[k].z) * (kStep * float(i)), y) ? kStep : 0.0f;
        out.response[k] = DeckResponse(path);
    }
    return out;
}

// ---- the cumulus ----

// Along the view ray from the base to the highest top, in steps no finer than the texel's width there.
template <typename Visit> void ViewRay(glm::vec3 v, float angle, Visit &&visit)
{
    const float near = kCloudBase / v.y, far = near + std::min(kCloudDepth / v.y, 40000.0f);
    for (float s = near; s <= far; s += std::max(40.0f, 0.5f * s * angle))
    {
        const glm::vec3 p = v * s - glm::vec3(0.0f, kCloudBase, 0.0f);
        if (!visit(p, std::max(s * angle, 40.0f)))
            break;
    }
}

// The texel's alpha: the largest density along its view ray, so a cut keeps the pixel where any cloud on the ray
// passes it (low clouds stand up from the horizon, seen from the side).
Texel CumulusAlpha(const Cumulus &clouds, glm::vec3 raw, int size)
{
    Texel out;
    const glm::vec3 v = glm::normalize(raw);
    if (v.y <= 0.0f)
        return out;
    float angle;
    TexelSize(raw, size, out, angle);
    float densest = 0.0f;
    ViewRay(v, angle, [&](glm::vec3 p, float footprint) {
        // Higher up nothing denser can be met.
        if (Cumulus::Gradient(p.y / kCloudDepth) < densest)
            return false;
        densest = std::max(densest, clouds.Density(p, footprint));
        return true;
    });
    out.alpha = densest;
    return out;
}

// Where the eye meets the cloud the `cut` keeps (or, for a cloud only a larger cover keeps, nearly its densest), the
// light from each axis through it by Beer's law, the density above the cut being the extinction (Schneider 2015).
void CumulusLight(const Cumulus &clouds, glm::vec3 raw, int size, float cut, Texel &out)
{
    const glm::vec3 v = glm::normalize(raw);
    if (v.y <= 0.0f)
        return;
    const float angle = 2.0f / float(size) / std::sqrt(glm::dot(raw, raw));
    const float seen_cut = std::min(cut, out.alpha - 0.02f);
    glm::vec3 at{0.0f};
    float footprint = 40.0f;
    int steps = 0;
    bool met = false, outside = false; // the eye has met the cloud; a step outside it came before
    glm::vec3 before{0.0f};
    ViewRay(v, angle, [&](glm::vec3 p, float f) {
        outside = steps++ > 0;
        before = at;
        at = p;
        footprint = f;
        met = clouds.Density(p, f) >= seen_cut;
        return !met;
    });
    // Between the last step outside and the first inside, so the light does not step with the view ray's steps.
    for (int i = 0; met && outside && i < 6; ++i)
    {
        const glm::vec3 mid = 0.5f * (before + at);
        (clouds.Density(mid, footprint) >= seen_cut ? at : before) = mid;
    }
    constexpr int kLightSteps = 8;
    constexpr float kLightStep = 90.0f;         // metres
    constexpr float kExtinction = 1.0f / 40.0f; // per metre at a density one above the cut
    for (size_t k = 0; k < kAxes.size(); ++k)
    {
        float depth = 0.0f;
        for (int i = 1; i <= kLightSteps; ++i)
        {
            const glm::vec3 p = at + kAxes[k] * (kLightStep * (float(i) - 0.5f));
            if (p.y < 0.0f || p.y > kCloudDepth)
                break;
            depth += std::max(clouds.Density(p, footprint) - cut, 0.0f) / (1.0f - cut) * kLightStep;
        }
        out.response[k] = std::exp(-kExtinction * depth);
    }
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

// The cube's texels by face and row, from `texel(raw direction)`.
template <typename Make> std::vector<Texel> ForTexels(int size, ParallelFor &workers, Make &&texel)
{
    std::vector<Texel> texels(size_t(6) * size_t(size) * size_t(size));
    workers.run(size_t(6 * size), [&](size_t row) {
        const int face = int(row) / size, y = int(row) % size;
        for (int x = 0; x < size; ++x)
            texels[row * size_t(size) + size_t(x)] = texel(TexelDirection(face, x, y, size));
    });
    return texels;
}

std::vector<Texel> MakeCumulus(ParallelFor &workers)
{
    Cumulus clouds{MakeVolume(kShapeVoxels, kShapePeriod, workers, PerlinWorley),
                   MakeVolume(kErosionVoxels, kErosionPeriod, workers, Erosion),
                   std::vector<float>(size_t(Cumulus::kMassSide) * Cumulus::kMassSide)};
    workers.run(size_t(Cumulus::kMassSide), [&](size_t z) {
        for (int x = 0; x < Cumulus::kMassSide; ++x)
        {
            const glm::vec2 p = glm::vec2(float(x), float(z)) * Cumulus::kMassCell - Cumulus::kMassReach;
            clouds.masses[z * Cumulus::kMassSide + size_t(x)] =
                glm::mix(0.55f, 1.15f, 0.5f + 0.5f * Octaves(p, 24000.0f, 2, Cumulus::kMassCell));
        }
    });
    std::vector<Texel> texels =
        ForTexels(kBaseCloudSize, workers, [&](glm::vec3 raw) { return CumulusAlpha(clouds, raw, kBaseCloudSize); });
    // The responses are worked out for the clouds of half the sky's cover.
    const float reference = CoverThresholds(texels)[kCoverSteps / 2];
    workers.run(texels.size(), [&](size_t i) {
        CumulusLight(clouds,
                     TexelDirection(int(i / size_t(kBaseCloudSize * kBaseCloudSize)), int(i % kBaseCloudSize),
                                    int(i / kBaseCloudSize) % kBaseCloudSize, kBaseCloudSize),
                     kBaseCloudSize, reference, texels[i]);
    });
    return texels;
}

std::vector<Texel> MakeDeck(ParallelFor &workers)
{
    const int size = kRainCloudSize;
    Field field;
    field.size = size;
    field.v.assign(size_t(6) * size_t(size) * size_t(size), 0.0f);
    workers.run(size_t(6 * size), [&](size_t row) {
        const int face = int(row) / size, y = int(row) % size;
        for (int x = 0; x < size; ++x)
        {
            const glm::vec3 raw = TexelDirection(face, x, y, size);
            const glm::vec3 v = glm::normalize(raw);
            // Below the horizon the layer's field continues from a little above it.
            const glm::vec3 up = glm::normalize(glm::vec3(v.x, std::max(v.y, 0.01f), v.z));
            const float angle = 2.0f / float(size) / std::sqrt(glm::dot(raw, raw));
            const float footprint = kCloudBase * angle / (up.y * up.y);
            field.v[row * size_t(size) + size_t(x)] = RainDensity(OnLayer(up), footprint);
        }
    });
    return ForTexels(size, workers, [&](glm::vec3 raw) { return MarchDeck(field, raw, size); });
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
    const std::vector<Texel> base = MakeCumulus(workers);
    out.threshold = CoverThresholds(base);
    out.base = Pack(base, kBaseCloudSize, kCumulusPositive, kCumulusNegative, out.ForCover(0.5f));
    out.rain = Pack(MakeDeck(workers), kRainCloudSize, kRainPositive, kRainNegative, {0.0f, 1.0f});
    return out;
}

} // namespace storm::fx
