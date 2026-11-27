#include "storm/fx/sky_model.hpp"

#include "storm/fx/color_lut.hpp"

#include <algorithm>
#include <cmath>

namespace storm::fx
{
namespace
{
constexpr float kPi = 3.14159265358979f;
constexpr float kEarthRadius = 6371000.0f;
constexpr float kRayleighDepth = 8228.0f, kMieDepth = 1800.0f, kOzoneHeight = 30000.0f;
constexpr glm::vec3 kRayleigh{5.6e-6f, 1.25e-5f, 2.9e-5f};
constexpr glm::vec3 kOzone{3.426f * 6e-7f, 8.298f * 6e-7f, 0.356f * 6e-7f};
constexpr float kMie = 16e-6f;
constexpr float kLn2 = 0.6931472f;

// The CPU side of fs_fx_sky.sc's fxShellPath, fxThickness and fxOpticalDepth.
float ShellPath(float y, float depth)
{
    const float yr = y * kEarthRadius;
    const float k = depth * (2.0f * kEarthRadius + depth);
    const float root = std::sqrt(yr * yr + k);
    return yr > 0.0f ? k / (yr + root) : root - yr;
}

glm::vec3 Thickness(float y)
{
    const float yr = y * kEarthRadius;
    const float low = kOzoneHeight * (2.0f * kEarthRadius + kOzoneHeight);
    const float high = (kOzoneHeight + kRayleighDepth) * (2.0f * kEarthRadius + kOzoneHeight + kRayleighDepth);
    const float ozone = (high - low) / (std::sqrt(yr * yr + high) + std::sqrt(yr * yr + low));
    return {ShellPath(y, kRayleighDepth), ShellPath(y, kMieDepth), ozone};
}

glm::vec3 OpticalDepth(glm::vec3 t)
{
    return (kRayleigh * t.x + glm::vec3(kMie * t.y) + kOzone * t.z) * (1.0f / kLn2);
}

float MeanTransmission(float d)
{
    return d < 0.01f ? 1.0f - d * 0.5f + d * d / 6.0f : (1.0f - std::exp(-d)) / d;
}

float Smooth(float a, float b, float x)
{
    return glm::smoothstep(a, b, x);
}
} // namespace

glm::vec4 SkyAtmosphere(const WeatherInput &w, glm::vec3 dir, glm::vec3 light, bool solar)
{
    const glm::vec3 thickness = Thickness(dir.y);
    const float mu = std::max(glm::dot(dir, light), 0.0f);
    glm::vec3 scatter = thickness.x * (0.4f * mu * mu + 1.12f) * kRayleigh;
    const float g = 0.8f - light.y * 0.15f - kMie * 1500.0f;
    const float g2 = g * g;
    const float a = 1.0f / std::sqrt(1.0f + g2 - 2.0f * g * mu);
    scatter += glm::vec3(thickness.y * (0.4f + light.y * 0.1f + kMie * 7000.0f) * (1.0f - g2) * a * a * a * kMie);
    const glm::vec3 to_light = OpticalDepth(Thickness(light.y));
    const glm::vec3 to_eye = OpticalDepth(thickness);
    const glm::vec3 d = glm::abs(to_light - to_eye) * kLn2;
    const glm::vec3 mean{MeanTransmission(d.x), MeanTransmission(d.y), MeanTransmission(d.z)};
    const glm::vec3 lit = scatter * glm::exp2(-glm::min(to_eye, to_light)) * mean * (0.6f + light.y * 0.3f);
    const glm::vec3 night = glm::clamp(w.sky_night * lit, 0.0f, 100.5f);
    if (!solar)
        return {night, to_light.x};
    glm::vec3 sky = glm::clamp(w.sky_day * lit, 0.0f, 100.5f);
    const float up = std::max(dir.y, 0.0f);
    const float low = 1.0f - Smooth(0.08f, 0.35f, light.y);
    const float lowest = 1.0f - Smooth(0.025f, 0.10f, light.y);
    const float red = 1.0f - Smooth(0.02f, 0.22f, light.y);
    const float band = 1.0f - Smooth(0.0f, 0.45f, up);
    const float wide = 1.0f - Smooth(0.0f, 0.72f, up);
    const glm::vec2 dh{dir.x, dir.z}, lh{light.x, light.z};
    const float side = Smooth(-0.60f, 0.70f, glm::dot(dh, lh) / std::max(glm::length(dh) * glm::length(lh), 0.0001f));
    const glm::vec3 sunset = std::max(sky.r, std::max(sky.g, sky.b)) *
                             glm::mix(glm::vec3(1.1f, 0.30f, 0.025f), glm::vec3(1.1f, 0.10f, 0.008f), red);
    sky = glm::mix(sky, night, low * (1.0f - side) * glm::mix(0.35f, 0.70f, wide));
    sky = glm::mix(sky, sunset, std::max(low * band * 0.85f, lowest * wide * 0.95f) * glm::mix(0.08f, 1.0f, side));
    return {sky, to_light.x};
}

glm::vec3 SkyBackground(const WeatherInput &w, glm::vec3 dir)
{
    const glm::vec4 solar = w.daylight > 0.0f ? SkyAtmosphere(w, dir, w.sun_dir, true) : glm::vec4(0.0f);
    const glm::vec4 lunar = w.daylight < 1.0f ? SkyAtmosphere(w, dir, w.moon_dir, false) : glm::vec4(0.0f);
    return glm::vec3(glm::mix(lunar, solar, w.daylight));
}

float SkyHazeAmount(const WeatherInput &w, float dir_y)
{
    if (w.haze <= 0.0f)
        return 0.0f;
    const float t = glm::clamp(
        (1.0f - std::fabs(dir_y) / std::max(w.haze_height, 1e-3f)) / std::max(w.haze_edge, 0.05f), 0.0f, 1.0f);
    return w.haze * t * t * (3.0f - 2.0f * t);
}

namespace
{
float SixWay(glm::vec3 positive, glm::vec3 negative, glm::vec3 l)
{
    const glm::vec3 r{l.x >= 0.0f ? positive.x : negative.x, l.y >= 0.0f ? positive.y : negative.y,
                      l.z >= 0.0f ? positive.z : negative.z};
    return glm::dot(r, l * l);
}

float ShapeResponse(float response, float peak, float contrast)
{
    const float top = std::max(peak, 0.0001f);
    return top * std::pow(glm::clamp(response / top, 0.0f, 1.0f), glm::clamp(contrast, 0.5f, 2.0f));
}

float Transmission(float opacity, float alignment, float height, float strength, float radius_deg, float solar)
{
    const float spread = std::max(1.0f - std::cos(glm::radians(glm::clamp(radius_deg, 0.5f, 45.0f))), 0.000001f);
    const float distance = std::max(1.0f - glm::clamp(alignment, -1.0f, 1.0f), 0.0f) / spread;
    const float mask = glm::mix(0.75f, 0.20f, solar) * std::exp2(-4.0f * distance) +
                       glm::mix(1.25f, 3.80f, solar) * std::exp2(-glm::mix(24.0f, 80.0f, solar) * distance);
    const float o = glm::clamp(opacity, 0.0f, 1.0f);
    return std::max(strength, 0.0f) * mask * o * (2.0f - o) * Smooth(-0.02f, 0.02f, height);
}

float BlendTransmission(float response, float transmission)
{
    const float width = transmission * 0.5f;
    const float overlap = std::max(width - std::fabs(response - transmission), 0.0f) / std::max(width, 0.000001f);
    return std::max(response, transmission) + overlap * overlap * width * 0.25f;
}

float Max3(glm::vec3 v)
{
    return std::max(v.r, std::max(v.g, v.b));
}
} // namespace

glm::vec4 CloudShade(const WeatherInput &w, float cloud_ambient, glm::vec3 positive, glm::vec3 negative, float opacity,
                     float ambient, glm::vec3 view, glm::vec3 sky)
{
    const glm::vec4 &look = w.cloud_look, &low = w.cloud_sunset, &day = w.cloud_day, &dusk = w.cloud_twilight,
                    &tint = w.cloud_tint, &tr = w.cloud_transmission;
    const glm::vec3 sun = w.sun_dir, source = w.sun_light;
    const float twilight = glm::clamp(look.w, 0.0f, 1.0f);
    const float edge =
        glm::mix(1.0f, glm::clamp(look.z, 0.0f, 4.0f), 1.0f - Smooth(0.20f, 0.95f, glm::clamp(opacity, 0.0f, 1.0f)));
    const float peak = Max3(glm::max(positive, negative));
    const float baked = SixWay(positive, negative, sun);
    float response = ShapeResponse(baked, peak, day.w);
    response = BlendTransmission(response, Transmission(opacity, glm::dot(view, sun), sun.y, tr.x, tr.z, 1.0f));

    const float sunlight = std::max(day.x, 0.0f) * glm::mix(1.0f, std::max(low.x, 0.0f), twilight);
    const float brightness = Max3(source);
    const glm::vec3 sunset_colour =
        glm::mix(glm::vec3(1.0f, 0.52f, 0.16f), glm::vec3(1.0f, 0.26f, 0.025f), 1.0f - Smooth(0.02f, 0.22f, sun.y)) *
        brightness;
    const glm::vec3 direct = glm::mix(source, glm::mix(source, sunset_colour, twilight), low.z);
    const float base = std::max(negative.g, 0.0f) * std::max(low.y, 0.0f) * Smooth(-0.35f, 1.0f, glm::dot(view, sun)) *
                       Smooth(-0.02f, 0.02f, sun.y) * twilight * (1.0f - Smooth(0.0f, 0.18f, sun.y));
    const glm::vec3 base_colour =
        glm::mix(source, glm::vec3(1.0f, 0.10f, 0.015f) * brightness, glm::clamp(low.z, 0.0f, 1.0f));
    const glm::vec3 sun_radiance = response * direct * sunlight * edge;

    const glm::vec3 luma = kLumaRec709;
    const float sky_luma = glm::dot(glm::max(sky, 0.0f), luma);
    const glm::vec3 sky_hue = glm::max(sky, 0.0f) * (glm::dot(w.fill_color, luma) / std::max(sky_luma, 0.0001f));
    const glm::vec3 ambient_colour =
        glm::mix(w.fill_color, sky_hue, glm::clamp(day.z, 0.0f, 1.0f) * Smooth(0.0f, 0.01f, sky_luma));
    const float strength = cloud_ambient * std::max(day.y, 0.0f);
    glm::vec3 radiance = sun_radiance + ambient * ambient_colour * w.fill_intensity * strength;

    if (const float sun_luma = glm::dot(glm::max(sun_radiance, 0.0f), luma); sun_luma > 0.00001f)
    {
        const float total = glm::dot(glm::max(radiance, 0.0f), luma);
        const float share = glm::clamp(sun_luma / std::max(total, 0.00001f), 0.0f, 1.0f);
        const float lit = glm::clamp(baked / std::max(peak, 0.0001f), 0.0f, 1.0f);
        radiance = glm::mix(radiance, sun_radiance * (total / sun_luma),
                            glm::clamp(low.w, 0.0f, 1.0f) * lit * share * twilight);
    }
    radiance += base * base_colour * sunlight * edge;

    const float across = std::sqrt(std::max(sun.x * sun.x + sun.z * sun.z, 0.000001f));
    const float towards = (view.x * sun.x + view.z * sun.z) / across;
    const float side = std::sin(glm::radians(glm::clamp(dusk.y, 15.0f, 180.0f) * 0.5f));
    const float away = (1.0f - Smooth(-side, side, towards)) * twilight * glm::clamp(dusk.x, 0.0f, 1.0f);
    glm::vec3 dim = glm::clamp(glm::vec3(tint), 0.0f, 1.0f);
    dim *= std::min(glm::dot(glm::max(ambient_colour, 0.0f), luma), sky_luma) * std::max(dusk.z, 0.0f) /
           std::max(glm::dot(dim, luma), 0.0001f);
    radiance = glm::mix(radiance, ambient * dim * w.fill_intensity * strength, away);
    radiance = glm::mix(radiance, sky * opacity, 0.25f * away * (1.0f - Smooth(0.0f, 0.18f, std::fabs(view.y))));

    if (Max3(w.moon_light) > 0.0f)
    {
        float lunar = ShapeResponse(SixWay(positive, negative, w.moon_dir), peak, day.w);
        lunar =
            BlendTransmission(lunar, Transmission(opacity, glm::dot(view, w.moon_dir), w.moon_dir.y, tr.y, tr.w, 0.0f));
        radiance += lunar * w.moon_light * edge * std::max(tint.w, 0.0f);
    }
    return {radiance * std::max(look.x, 0.0f), opacity};
}

glm::vec3 SkyUnderRain(const WeatherInput &w, float cloud_ambient, glm::vec3 dir)
{
    const glm::vec3 sky = SkyBackground(w, dir);
    if (w.rain_clouds <= 0.0f)
        return sky;
    const float ambient = glm::dot(kRainPositive + kRainNegative, glm::vec3(1.0f / 6.0f));
    const glm::vec3 rain =
        glm::vec3(CloudShade(w, cloud_ambient, kRainPositive, kRainNegative, 1.0f, ambient, dir, sky));
    return glm::mix(sky, rain, glm::clamp(w.rain_clouds, 0.0f, 1.0f));
}

SkyState ComputeSky(const WeatherInput &weather, float cloud_ambient)
{
    SkyState sky;
    sky.sun_dir = weather.sun_dir;
    sky.fog_density = weather.fog_density;
    sky.fog_layer = weather.fog_layer;
    sky.night = 1.0f - glm::clamp(weather.daylight, 0.0f, 1.0f);
    // Under the rain clouds' cover: the haze band's colour round the horizon; the horizon under its haze, on the
    // display's scale; the dome: the cosine-weighted mean of the sky above (linear), the light the sky gives.
    for (int i = 0; i < kHazeAzimuths; ++i)
    {
        const float angle = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(kHazeAzimuths);
        sky.haze[size_t(i)] =
            weather.haze_from_sky
                ? weather.haze_color *
                      SkyUnderRain(weather, cloud_ambient,
                                   glm::normalize(glm::vec3(std::cos(angle), weather.haze_sample, std::sin(angle))))
                : weather.haze_color;
    }
    constexpr int kAzimuths = 16;
    const float haze = SkyHazeAmount(weather, 0.0f);
    glm::vec3 sum{0.0f}, dome{0.0f};
    float dome_weight = 0.0f;
    for (int i = 0; i < kAzimuths; ++i)
    {
        const float angle = 2.0f * kPi * (static_cast<float>(i) + 0.5f) / static_cast<float>(kAzimuths);
        const glm::vec3 dir{std::cos(angle), 0.0f, std::sin(angle)};
        // Between two of the band's colours, as the drawn sky takes them.
        const float at = (static_cast<float>(i) + 0.5f) * static_cast<float>(kHazeAzimuths) / kAzimuths;
        const int k = static_cast<int>(at);
        const glm::vec3 band = glm::mix(sky.haze[size_t(k)], sky.haze[size_t((k + 1) % kHazeAzimuths)], at - float(k));
        const glm::vec3 c = glm::mix(SkyUnderRain(weather, cloud_ambient, dir), band, haze);
        sum += c;
        if (i % 2 == 0)
            for (const float y : {0.3f, 0.65f, 0.95f})
            {
                const float r = std::sqrt(1.0f - y * y);
                dome += SkyUnderRain(weather, cloud_ambient, {dir.x * r, y, dir.z * r}) * y;
                dome_weight += y;
            }
    }
    const glm::vec3 hdr = glm::max(sum / static_cast<float>(kAzimuths), 0.0f) * weather.exposure;
    sky.horizon = hdr / (1.0f + 0.6666667f * glm::dot(hdr, kLumaRec709)); // fxCompressHdr (fx_tonemap.sh)
    sky.dome = dome / dome_weight;
    return sky;
}

std::vector<uint8_t> CloudNoise()
{
    // Perlin's gradient noise on a lattice that wraps at the tile's edge; the gradients from an integer hash.
    const auto gradient = [](int x, int y) {
        uint32_t h = uint32_t(x % kCloudNoiseCells) * 73856093u ^ uint32_t(y % kCloudNoiseCells) * 19349663u;
        h = (h ^ (h >> 13)) * 0x5bd1e995u;
        h ^= h >> 15;
        const float angle = float(h & 0xffffu) / 65536.0f * 2.0f * kPi;
        return glm::vec2(std::cos(angle), std::sin(angle));
    };
    const auto fade = [](float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); };
    std::vector<uint8_t> out;
    out.reserve(size_t(kCloudNoiseSize) * kCloudNoiseSize * 4 / 3 + 1);
    for (int y = 0; y < kCloudNoiseSize; ++y)
        for (int x = 0; x < kCloudNoiseSize; ++x)
        {
            const glm::vec2 p =
                (glm::vec2(float(x), float(y)) + 0.5f) * float(kCloudNoiseCells) / float(kCloudNoiseSize);
            const glm::ivec2 i = glm::ivec2(glm::floor(p));
            const glm::vec2 f = p - glm::floor(p);
            const float a = glm::dot(gradient(i.x, i.y), f);
            const float b = glm::dot(gradient(i.x + 1, i.y), f - glm::vec2(1.0f, 0.0f));
            const float c = glm::dot(gradient(i.x, i.y + 1), f - glm::vec2(0.0f, 1.0f));
            const float d = glm::dot(gradient(i.x + 1, i.y + 1), f - glm::vec2(1.0f, 1.0f));
            const float v = glm::mix(glm::mix(a, b, fade(f.x)), glm::mix(c, d, fade(f.x)), fade(f.y));
            // The noise stays within about ±0.7.
            out.push_back(uint8_t(std::lround(std::clamp(0.5f + 0.7f * v, 0.0f, 1.0f) * 255.0f)));
        }
    // Mips: 2 x 2 averages, which tile as the level above does.
    size_t level = 0;
    for (int size = kCloudNoiseSize; size > 1; size /= 2)
    {
        const int half = size / 2;
        const size_t next = out.size();
        for (int y = 0; y < half; ++y)
            for (int x = 0; x < half; ++x)
            {
                const auto at = [&](int u, int v) { return int(out[level + size_t(v) * size_t(size) + size_t(u)]); };
                out.push_back(uint8_t(
                    (at(2 * x, 2 * y) + at(2 * x + 1, 2 * y) + at(2 * x, 2 * y + 1) + at(2 * x + 1, 2 * y + 1) + 2) /
                    4));
            }
        level = next;
    }
    return out;
}

AerialUniforms ComputeAerial(const SkyState &sky, float camera_height, const std::array<float, 16> &view)
{
    AerialUniforms u;
    for (int i = 0; i < 3; ++i)
        u.u_fxAerial[0][i] =
            std::sqrt(glm::clamp(sky.horizon[i], 0.0f, 1.0f)); // the emulator's colours are gamma encoded
    u.u_fxAerial[0][3] = sky.fog_density;
    u.u_fxAerial[1] = {sky.fog_density * kHazeDensityFactor *
                           std::exp(-std::max(camera_height, 0.0f) / kHazeLayerHeight),
                       1.0f / kHazeLayerHeight, 0.55f, 0.94f};
    u.u_fxAerial[2] = {view[4], view[5], view[6], 0.0f}; // world up (0, 1, 0) in view space
    return u;
}

std::array<float, 2> ComputeAerialFogHaze(float camera_height, float fog_layer)
{
    // (1 + h layer) / (1 + h) is the layer's share alone when h is far above 1.
    if (fog_layer > 0.0f)
        return {1e6f, 1.0f / fog_layer};
    // A sixth of the fx sky's haze: the emulated geometry keeps the game's fog, the layer only lifts it a little off
    // what rises above the eye.
    return {kHazeDensityFactor / 6.0f * std::exp(-std::max(camera_height, 0.0f) / kHazeLayerHeight),
            1.0f / kHazeLayerHeight};
}

} // namespace storm::fx
