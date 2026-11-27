#include "storm/environment/evaluate.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace storm::environment
{
namespace
{
using scene_data::LightingHour;
using scene_data::Rgb;
using scene_data::TimeOfDayPeriod;

// Where no sky is drawn (an interior, a place without the time of day) an hourly preset's fill multiplies this
// irradiance; ours, set by eye on the interiors. Outdoors the fill multiplies the drawn sky's (State::sky_fill).
constexpr float kPresetFillIrradiance = 0.1f;
// The half sizes of the sun's and the moon's sprites in Caribbean Legend's sky (its sky shader's own).
constexpr float kSunSize = 0.12f, kMoonSize = 0.075f;

glm::vec3 Vec(const Rgb &c)
{
    return {c[0], c[1], c[2]};
}

Rgb Mix(const Rgb &a, const Rgb &b, float t)
{
    return {a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t};
}

float Mix(float a, float b, float t)
{
    return a + (b - a) * t;
}

float Smooth01(float x)
{
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

float Wrap24(float hour)
{
    const float h = std::fmod(std::fmod(hour, 24.0f) + 24.0f, 24.0f);
    return h < 24.0f ? h : 0.0f; // fmod of a value just below 0 can round up to 24
}

// Every field linear but the exposure, which blends in log2 (a day at twice the exposure of the night is half-way
// at the square root of two). The camera's grade blends field by field.
TimeOfDayPeriod Blend(const TimeOfDayPeriod &a, const TimeOfDayPeriod &b, float t)
{
    TimeOfDayPeriod p = a;
    p.light_color = Mix(a.light_color, b.light_color, t);
    p.light_intensity = Mix(a.light_intensity, b.light_intensity, t);
    p.sky_day_radiance = Mix(a.sky_day_radiance, b.sky_day_radiance, t);
    p.sky_night_radiance = Mix(a.sky_night_radiance, b.sky_night_radiance, t);
    p.sky_fill_color = Mix(a.sky_fill_color, b.sky_fill_color, t);
    p.sky_fill_intensity = Mix(a.sky_fill_intensity, b.sky_fill_intensity, t);
    p.diffuse_sky_tint = Mix(a.diffuse_sky_tint, b.diffuse_sky_tint, t);
    p.ground_color = Mix(a.ground_color, b.ground_color, t);
    p.ground_intensity = Mix(a.ground_intensity, b.ground_intensity, t);
    p.base_exposure = std::exp2(Mix(std::log2(a.base_exposure), std::log2(b.base_exposure), t));
    p.camera.exposure_ev = Mix(a.camera.exposure_ev, b.camera.exposure_ev, t);
    p.camera.contrast = Mix(a.camera.contrast, b.camera.contrast, t);
    p.camera.saturation = Mix(a.camera.saturation, b.camera.saturation, t);
    p.camera.temperature = Mix(a.camera.temperature, b.camera.temperature, t);
    p.camera.tint = Mix(a.camera.tint, b.camera.tint, t);
    p.camera.color_filter = Mix(a.camera.color_filter, b.camera.color_filter, t);
    p.camera.lift = Mix(a.camera.lift, b.camera.lift, t);
    p.camera.gamma = Mix(a.camera.gamma, b.camera.gamma, t);
    p.camera.gain = Mix(a.camera.gain, b.camera.gain, t);
    return p;
}

TimeOfDayPeriod PeriodAt(const scene_data::TimeOfDay &tod, float hour)
{
    const PeriodMix m = MixPeriods(tod.transition_hours, hour);
    return Blend(tod.periods[size_t(m.from)], tod.periods[size_t(m.to)], m.t);
}

// The sun or moon on an arc from `rise` to `set` (hours, across midnight too): above the horizon in between, below
// it for the rest of the day. The arc's plane leans by the noon elevation and turns by the azimuth.
glm::vec3 Arc(float hour, float rise, float set, float elevation, float azimuth)
{
    const float span = Wrap24(set - rise);
    const float x = Wrap24(hour - rise);
    const float t = x <= span ? x / span : 1.0f + (x - span) / (24.0f - span);
    const float theta = t * glm::pi<float>();
    const glm::vec3 d{std::cos(theta), std::sin(theta) * std::sin(elevation), std::sin(theta) * std::cos(elevation)};
    const float ca = std::cos(azimuth), sa = std::sin(azimuth);
    return {d.x * ca + d.z * sa, d.y, -d.x * sa + d.z * ca};
}

LightingHour MixHour(const LightingHour &a, const LightingHour &b, float t)
{
    LightingHour h = a;
    h.ambient_color = Mix(a.ambient_color, b.ambient_color, t);
    h.ambient_intensity = Mix(a.ambient_intensity, b.ambient_intensity, t);
    h.sun_color = Mix(a.sun_color, b.sun_color, t);
    h.sun_intensity = Mix(a.sun_intensity, b.sun_intensity, t);
    h.bloom_intensity = Mix(a.bloom_intensity, b.bloom_intensity, t);
    h.bloom_threshold = Mix(a.bloom_threshold, b.bloom_threshold, t);
    h.bloom_soft_threshold = Mix(a.bloom_soft_threshold, b.bloom_soft_threshold, t);
    h.emission = Mix(a.emission, b.emission, t);
    h.shadow_ao = Mix(a.shadow_ao, b.shadow_ao, t);
    if (a.fog || b.fog)
    {
        // An hour without fog counts as fog of no density in the other's colour.
        const scene_data::Fog fa = a.fog.value_or(scene_data::Fog{b.fog ? b.fog->color : Rgb{}, 0.0f});
        const scene_data::Fog fb = b.fog.value_or(scene_data::Fog{fa.color, 0.0f});
        h.fog = scene_data::Fog{Mix(fa.color, fb.color, t), Mix(fa.density, fb.density, t)};
    }
    if (a.sea && b.sea)
    {
        h.sea->color = Mix(a.sea->color, b.sea->color, t);
        h.sea->sky_color = Mix(a.sea->sky_color, b.sea->sky_color, t);
        h.sea->params = Mix(a.sea->params, b.sea->params, t);
    }
    for (const auto &[group, light] : b.point_lights)
    {
        const scene_data::PointLightGroup from = a.point_lights.contains(group) ? a.point_lights.at(group) : light;
        h.point_lights[group] = {Mix(from.color, light.color, t), Mix(from.intensity, light.intensity, t)};
    }
    return h;
}

CameraGrade Grade(const scene_data::ToneMapping &camera)
{
    CameraGrade g;
    g.contrast = camera.contrast;
    g.saturation = camera.saturation;
    g.temperature = camera.temperature;
    g.tint = camera.tint;
    g.color_filter = Vec(camera.color_filter);
    g.lift = Vec(camera.lift);
    g.gamma = Vec(camera.gamma);
    g.gain = Vec(camera.gain);
    return g;
}

scene_data::CloudPeriod BlendClouds(const scene_data::CloudPeriod &a, const scene_data::CloudPeriod &b, float t)
{
    scene_data::CloudPeriod p;
    const auto mix = [t](float x, float y) { return Mix(x, y, t); };
    p.intensity = mix(a.intensity, b.intensity);
    p.opacity = mix(a.opacity, b.opacity);
    p.edge_lighting_strength = mix(a.edge_lighting_strength, b.edge_lighting_strength);
    p.reflection_strength = mix(a.reflection_strength, b.reflection_strength);
    p.moon_intensity = mix(a.moon_intensity, b.moon_intensity);
    p.sun_transmission_strength = mix(a.sun_transmission_strength, b.sun_transmission_strength);
    p.moon_transmission_strength = mix(a.moon_transmission_strength, b.moon_transmission_strength);
    p.sun_transmission_radius = mix(a.sun_transmission_radius, b.sun_transmission_radius);
    p.moon_transmission_radius = mix(a.moon_transmission_radius, b.moon_transmission_radius);
    p.sun_intensity = mix(a.sun_intensity, b.sun_intensity);
    p.ambient_intensity = mix(a.ambient_intensity, b.ambient_intensity);
    p.sky_tint = mix(a.sky_tint, b.sky_tint);
    p.lighting_contrast = mix(a.lighting_contrast, b.lighting_contrast);
    p.twilight_sun_multiplier = mix(a.twilight_sun_multiplier, b.twilight_sun_multiplier);
    p.twilight_base_lighting = mix(a.twilight_base_lighting, b.twilight_base_lighting);
    p.twilight_warmth = mix(a.twilight_warmth, b.twilight_warmth);
    p.twilight_edge_color = mix(a.twilight_edge_color, b.twilight_edge_color);
    p.twilight_opposite_strength = mix(a.twilight_opposite_strength, b.twilight_opposite_strength);
    p.twilight_transition_width = mix(a.twilight_transition_width, b.twilight_transition_width);
    p.twilight_ambient_intensity = mix(a.twilight_ambient_intensity, b.twilight_ambient_intensity);
    p.twilight_tint = Mix(a.twilight_tint, b.twilight_tint, t);
    return p;
}
} // namespace

PeriodMix MixPeriods(const scene_data::TransitionHours &hours, float hour)
{
    const float h = Wrap24(hour);
    int previous = scene_data::kNight;
    for (int i = 0; i < scene_data::kPeriods; ++i)
    {
        const float begin = hours[size_t(2 * i)], end = hours[size_t(2 * i + 1)];
        if (!(h >= begin))
            return {previous, previous, 0.0f};
        if (h < end)
            return {previous, i, Smooth01((h - begin) / std::max(end - begin, 1e-3f))};
        previous = i;
    }
    return {scene_data::kNight, scene_data::kNight, 0.0f};
}

Celestial EvaluateCelestial(const scene_data::CelestialTimes &times, float hour, float noon_elevation_deg,
                            float azimuth_deg)
{
    constexpr float kSunrise = 6.0f, kSunset = 20.0f;
    const float h = Wrap24(hour);
    const float elevation =
        glm::radians(std::isnan(noon_elevation_deg) ? 60.0f : std::clamp(noon_elevation_deg, 1.0f, 179.0f));
    const float azimuth = glm::radians(std::isnan(azimuth_deg) ? 0.0f : azimuth_deg);
    Celestial c;
    c.sun_dir = Arc(h, kSunrise, kSunset, elevation, azimuth);
    c.moon_dir = Arc(h, kSunset - times.moon_overlap_hours, kSunrise + times.moon_overlap_hours, elevation, azimuth);
    const float handoff = times.handoff_hours, dusk = times.twilight_hours;
    c.sun_weight = Smooth01((h - kSunrise) / handoff) * (1.0f - Smooth01((h - (kSunset - handoff)) / handoff));
    c.twilight =
        Smooth01((h - (kSunrise - dusk)) / (2.0f * dusk)) * (1.0f - Smooth01((h - (kSunset - dusk)) / (2.0f * dusk)));
    c.moon_shadows = h < times.shadow_morning_hour || h >= times.shadow_evening_hour;
    // Around the hours the shadows pass between the moon and the sun they fade out and in over the fade's minutes.
    const auto fade = [&](float at, float minutes) {
        return minutes > 0.0f ? Smooth01(std::fabs(h - at) * 120.0f / minutes) : 1.0f;
    };
    c.shadow_fade = std::min(fade(times.shadow_evening_hour, times.shadow_evening_fade_minutes),
                             fade(times.shadow_morning_hour, times.shadow_morning_fade_minutes));
    return c;
}

const std::array<scene_data::CloudPeriod, scene_data::kPeriods> &EngineCloudPeriods()
{
    // Its engine's built-in textured_clouds: one look for every period but the intensity, opacity, sunlight and
    // ambient of dawn, sunset and night.
    static const std::array<scene_data::CloudPeriod, scene_data::kPeriods> periods = [] {
        scene_data::CloudPeriod day;
        day.intensity = 2.09f;
        day.opacity = 0.28f;
        day.edge_lighting_strength = 0.96f;
        day.reflection_strength = 0.24f;
        day.moon_intensity = 1.0f;
        day.sun_transmission_strength = 1.96f;
        day.moon_transmission_strength = 5.13f;
        day.sun_transmission_radius = 19.1f;
        day.moon_transmission_radius = 15.1f;
        day.sun_intensity = 1.05f;
        day.ambient_intensity = 3.22f;
        day.sky_tint = 0.63f;
        day.lighting_contrast = 1.59f;
        day.twilight_sun_multiplier = 0.98f;
        day.twilight_base_lighting = 2.12f;
        day.twilight_warmth = 1.0f;
        day.twilight_edge_color = 0.22f;
        day.twilight_opposite_strength = 0.76f;
        day.twilight_transition_width = 130.9f;
        day.twilight_ambient_intensity = 1.42f;
        day.twilight_tint = {0.4196f, 0.5098f, 0.6824f};
        std::array<scene_data::CloudPeriod, scene_data::kPeriods> p{day, day, day, day};
        for (const int twilight : {scene_data::kDawn, scene_data::kSunset})
        {
            p[size_t(twilight)].intensity = 2.1f;
            p[size_t(twilight)].opacity = 0.95f;
            p[size_t(twilight)].sun_intensity = 1.0f;
            p[size_t(twilight)].ambient_intensity = 1.0f;
        }
        p[scene_data::kNight].intensity = 0.35f;
        p[scene_data::kNight].opacity = 1.0f;
        p[scene_data::kNight].ambient_intensity = 0.08f;
        return p;
    }();
    return periods;
}

CloudLook EvaluateCloudLook(const std::array<scene_data::CloudPeriod, scene_data::kPeriods> &periods,
                            const scene_data::TransitionHours &hours, float hour)
{
    const PeriodMix m = MixPeriods(hours, hour);
    const scene_data::CloudPeriod p = BlendClouds(periods[size_t(m.from)], periods[size_t(m.to)], m.t);
    const auto twilight = [](int period) { return period == scene_data::kDawn || period == scene_data::kSunset; };
    CloudLook look;
    look.intensity = p.intensity;
    look.opacity = p.opacity;
    look.edge = p.edge_lighting_strength;
    look.twilight = (twilight(m.from) ? 1.0f - m.t : 0.0f) + (twilight(m.to) ? m.t : 0.0f);
    look.sun_transmission = p.sun_transmission_strength;
    look.moon_transmission = p.moon_transmission_strength;
    look.sun_radius = p.sun_transmission_radius;
    look.moon_radius = p.moon_transmission_radius;
    look.sun_intensity = p.sun_intensity;
    look.ambient_intensity = p.ambient_intensity;
    look.sky_tint = p.sky_tint;
    look.contrast = p.lighting_contrast;
    look.twilight_sun = p.twilight_sun_multiplier;
    look.twilight_base = p.twilight_base_lighting;
    look.twilight_warmth = p.twilight_warmth;
    look.twilight_edge = p.twilight_edge_color;
    look.twilight_opposite = p.twilight_opposite_strength;
    look.twilight_width = p.twilight_transition_width;
    look.twilight_ambient = p.twilight_ambient_intensity;
    look.twilight_tint = Vec(p.twilight_tint);
    look.moon_intensity = p.moon_intensity;
    return look;
}

std::optional<Hours> CompleteHours(const scene_data::LightingPreset &preset)
{
    std::array<int, 24> present{};
    int count = 0;
    for (int h = 0; h < 24; ++h)
        if (preset.hours[size_t(h)])
            present[size_t(count++)] = h;
    if (count == 0)
        return std::nullopt;
    Hours out;
    for (int h = 0; h < 24; ++h)
    {
        if (preset.hours[size_t(h)])
        {
            out[size_t(h)] = *preset.hours[size_t(h)];
            continue;
        }
        // The nearest present hours before and after on the 24-hour circle.
        int before = present[size_t(count - 1)], after = present[0];
        for (int i = 0; i < count; ++i)
            if (present[size_t(i)] < h)
                before = present[size_t(i)];
        for (int i = count - 1; i >= 0; --i)
            if (present[size_t(i)] > h)
                after = present[size_t(i)];
        const int gap = (after - before + 24) % 24;
        const float t = gap == 0 ? 0.0f : float((h - before + 24) % 24) / float(gap);
        LightingHour mixed = MixHour(*preset.hours[size_t(before)], *preset.hours[size_t(after)], t);
        // Not interpolated: the exposure, the image-based light (the last one the file gave before this hour) and the
        // world map's sea colours.
        mixed.exposure = LightingHour{}.exposure;
        mixed.hdr_texture.clear();
        mixed.night_texture.clear();
        for (int p = h - 1; p >= 0; --p)
            if (preset.hours[size_t(p)])
            {
                mixed.hdr_texture = preset.hours[size_t(p)]->hdr_texture;
                mixed.night_texture = preset.hours[size_t(p)]->night_texture;
                break;
            }
        if (mixed.sea)
            mixed.sea->deep_color.reset(), mixed.sea->shallow_color.reset();
        out[size_t(h)] = std::move(mixed);
    }
    return out;
}

WeatherDay ParseWeatherDay(std::span<const std::array<std::string, 16>, 25> rows, const WeatherFields &fields,
                           float pre_rain, const std::function<void(const std::string &)> &error)
{
    WeatherDay day;
    day.pre_rain = pre_rain;
    for (size_t h = 0; h < rows.size(); ++h)
    {
        const auto &row = rows[h];
        WeatherHour &out = day.hours[h];
        const auto number = [&](int field, float &value) {
            const std::string &text = row[size_t(field)];
            const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (ec != std::errc() || end != text.data() + text.size())
                error("hour " + std::to_string(h) + " field " + std::to_string(field) + ": not a number: '" + text +
                      "'");
        };
        const auto flag = [&](int field) {
            float value = 0.0f;
            number(field, value);
            return value != 0.0f;
        };
        number(fields.cloud_density, out.cloud_density);
        number(fields.rain_intensity, out.rain_intensity);
        out.rain = flag(fields.rain);
        out.storm = flag(fields.storm);
        out.tornado = flag(fields.tornado);
        out.lightning = flag(fields.lightning);
        out.rainbow = flag(fields.rainbow);
        const std::string &preset = row[size_t(fields.preset)];
        float none = 0.0f;
        if (const auto [end, ec] = std::from_chars(preset.data(), preset.data() + preset.size(), none);
            ec != std::errc() || end != preset.data() + preset.size())
            out.preset = preset;
    }
    for (size_t h = 0; h < 24; ++h)
        if (!day.hours[h].rain)
            day.hours[h].post_rain = h == 0 ? pre_rain : day.hours[h - 1].rain_intensity;
    return day;
}

HourTable BuildHourTable(const Hours &base, const WeatherDay *weather, bool sea, bool rain_allowed,
                         const std::function<const Hours *(const std::string &)> &rain_preset)
{
    HourTable table;
    table.hours = base;
    if (!weather || !rain_allowed)
        return table;
    for (size_t h = 0; h < 24; ++h)
    {
        const std::string &name = weather->hours[h].preset;
        if (name.empty())
            continue;
        const Hours *rain = rain_preset(sea ? name + "_sea" : name);
        if (!rain)
            continue;
        const LightingHour &over = (*rain)[h];
        LightingHour &hour = table.hours[h];
        hour.sun_color = over.sun_color;
        hour.sun_intensity = over.sun_intensity;
        hour.ambient_color = over.ambient_color;
        hour.ambient_intensity = over.ambient_intensity;
        hour.shadow_ao = over.shadow_ao;
        // The rain gives the fog's density; the place keeps its fog colour (the rain's where the place has none).
        if (over.fog)
            hour.fog = scene_data::Fog{hour.fog ? hour.fog->color : over.fog->color, over.fog->density};
        table.rain[h] = true;
    }
    return table;
}

State Evaluate(const Inputs &in)
{
    State s;
    const float h = Wrap24(in.hour);
    const int h0 = std::min(int(h), 23), h1 = (h0 + 1) % 24;
    const float f = h - float(h0);
    s.hour = h;
    s.sky = in.sky;
    s.sea = in.sea;

    // The hour table: what the place's presets give between this hour and the next.
    glm::vec3 hour_sun{0.0f}, hour_fill{0.0f};
    float hour_fill_intensity = 0.0f;
    float hour_exposure = 1.0f, rain_weight = 0.0f, shadow = 1.0f;
    float fog_weight = 0.0f; // the share of the fog the hour table gives
    glm::vec3 hour_fog_color{0.0f};
    float hour_fog_density = 0.0f;
    if (in.table)
    {
        const LightingHour &a = in.table->hours[size_t(h0)], &b = in.table->hours[size_t(h1)];
        const LightingHour m = MixHour(a, b, f);
        hour_sun = Vec(m.sun_color) * m.sun_intensity;
        hour_fill = Vec(m.ambient_color);
        hour_fill_intensity = m.ambient_intensity;
        hour_exposure = Mix(a.exposure, b.exposure, f);
        shadow = m.shadow_ao;
        rain_weight = Mix(in.table->rain[size_t(h0)] ? 1.0f : 0.0f, in.table->rain[size_t(h1)] ? 1.0f : 0.0f, f);
        s.bloom_intensity = m.bloom_intensity;
        s.bloom_threshold = m.bloom_threshold;
        s.bloom_soft = m.bloom_soft_threshold;
        s.emission = a.emission;
        // Fog densities are per 100 m in the presets.
        const float wa = a.fog ? 1.0f - f : 0.0f, wb = b.fog ? f : 0.0f;
        fog_weight = wa + wb;
        if (fog_weight > 0.0f)
        {
            hour_fog_density = (wa * (a.fog ? a.fog->density : 0.0f) + wb * (b.fog ? b.fog->density : 0.0f)) * 0.01f;
            hour_fog_color = (wa * (a.fog ? Vec(a.fog->color) : glm::vec3(0.0f)) +
                              wb * (b.fog ? Vec(b.fog->color) : glm::vec3(0.0f))) /
                             fog_weight;
        }
    }

    // The sun and moon, the periods of the day.
    const scene_data::TimeOfDay &tod = in.time_of_day;
    const Celestial c = EvaluateCelestial(tod.celestial, h, in.noon_elevation_deg, in.azimuth_deg);
    s.sun_dir = c.sun_dir;
    s.moon_dir = c.moon_dir;
    s.twilight = c.twilight;
    s.light_dir = c.moon_shadows ? c.moon_dir : c.sun_dir;
    s.light_is_sun = !c.moon_shadows && c.sun_dir.y > 0.0f;
    s.shadow_strength = shadow * c.shadow_fade;
    if (tod.enabled && in.sky)
    {
        // The weather keeps the direct light and the fill while it rains. The fill multiplies the drawn sky's light.
        const float w = std::clamp(1.0f - rain_weight, 0.0f, 1.0f);
        const TimeOfDayPeriod p = PeriodAt(tod, h);
        s.fill_color = glm::mix(hour_fill, Vec(p.sky_fill_color), w);
        s.fill_intensity = Mix(hour_fill_intensity, p.sky_fill_intensity, w);
        s.sky_fill = s.fill_color * s.fill_intensity;
        s.sky_tint = Vec(p.diffuse_sky_tint);
        s.ground_fill = Vec(p.ground_color) * p.ground_intensity;
        s.exposure = p.base_exposure * std::exp2(p.camera.exposure_ev);
        s.grade = Grade(p.camera);
        s.sky_day = Vec(p.sky_day_radiance);
        s.sky_night = Vec(p.sky_night_radiance);
        // The sun's colour is the periods' between the end of dawn and the start of the night, never the night's;
        // the moon's is the night's. While it rains both are the rain's light, which then also lights the clouds and
        // tints the discs.
        const TimeOfDayPeriod day = PeriodAt(tod, std::clamp(h, tod.transition_hours[1], tod.transition_hours[6]));
        const TimeOfDayPeriod &night = tod.periods[scene_data::kNight];
        s.sun_light = glm::mix(hour_sun, Vec(day.light_color) * day.light_intensity, w) * c.sun_weight;
        s.moon_light = glm::mix(hour_sun, Vec(night.light_color) * night.light_intensity, w) * (1.0f - c.sun_weight);
        s.light_color = s.sun_light + s.moon_light;
        s.sun_size = kSunSize;
        s.moon_size = kMoonSize;
        const scene_data::TexturedClouds *clouds =
            in.atmosphere && in.atmosphere->textured_clouds && in.atmosphere->textured_clouds->enabled
                ? &*in.atmosphere->textured_clouds
                : nullptr;
        s.cloud_look = EvaluateCloudLook(clouds ? clouds->periods : EngineCloudPeriods(), tod.transition_hours, h);
        const std::array<float, 3> &speeds = clouds ? clouds->rotation_speeds : kEngineCloudSpeeds;
        for (size_t i = 0; i < speeds.size(); ++i)
            s.cloud_spin[i] = speeds[i] * kCloudSpinPerSpeed;
    }
    else
    {
        s.ambient = hour_fill * hour_fill_intensity * kPresetFillIrradiance;
        s.fill_color = hour_fill;
        s.fill_intensity = hour_fill_intensity;
        s.light_color = hour_sun;
        s.exposure = hour_exposure * std::exp2(in.camera ? in.camera->exposure_ev : 0.0f);
        if (in.camera)
            s.grade = Grade(*in.camera);
    }

    // Fog: the hour table's where it has fog, the atmosphere's height fog for the rest.
    glm::vec3 period_fog_color{0.0f};
    float period_density = 0.0f;
    if (in.atmosphere && in.atmosphere->height_fog)
        if (const auto &profile = in.sea ? in.atmosphere->sea : in.atmosphere->land; profile && profile->enabled)
        {
            const PeriodMix m = MixPeriods(profile->transition_hours, h);
            const scene_data::HeightFogPeriod &a = profile->periods[size_t(m.from)],
                                              &b = profile->periods[size_t(m.to)];
            period_density = Mix(1.0f / a.attenuation_distance, 1.0f / b.attenuation_distance, m.t);
            period_fog_color = glm::mix(Vec(a.color) * Vec(a.tint), Vec(b.color) * Vec(b.tint), m.t);
            s.fog_base = Mix(a.base_height, b.base_height, m.t);
            s.fog_layer = Mix(a.maximum_height - a.base_height, b.maximum_height - b.base_height, m.t);
            s.fog_start = Mix(a.start_distance, b.start_distance, m.t);
            s.fog_end = Mix(a.max_distance, b.max_distance, m.t);
            s.fog_max = Mix(a.max_opacity, b.max_opacity, m.t);
            s.fog_from_sky = (m.t < 0.5f ? a.sky_color : b.sky_color) && fog_weight < 0.5f;
        }
    s.fog_density = hour_fog_density + (1.0f - fog_weight) * period_density;
    s.fog_color = fog_weight * hour_fog_color + (1.0f - fog_weight) * period_fog_color;

    // The haze band at the horizon.
    if (in.atmosphere && in.atmosphere->horizon_haze && in.atmosphere->horizon_haze->enabled && in.sky)
    {
        const scene_data::HorizonHaze &haze = *in.atmosphere->horizon_haze;
        const PeriodMix m = MixPeriods(haze.transition_hours, h);
        const scene_data::HazeColor &a = haze.colors[size_t(m.from)], &b = haze.colors[size_t(m.to)];
        s.haze_from_sky = haze.sky_color;
        s.haze_color = haze.sky_color ? glm::mix(Vec(a.tint) * a.brightness, Vec(b.tint) * b.brightness, m.t)
                                      : glm::mix(Vec(a.radiance) * a.brightness, Vec(b.radiance) * b.brightness, m.t);
        s.haze = haze.intensity;
        s.haze_height = std::sin(glm::radians(haze.sky_width_degrees));
        s.haze_edge = haze.edge_fade;
        s.haze_sample = glm::radians(haze.sample_elevation_degrees);
    }

    // The scripts' weather: clouds, rain, the rain clouds' cover and the wetness after a rain.
    if (in.weather)
    {
        const WeatherHour &a = in.weather->hours[size_t(h0)], &b = in.weather->hours[size_t(h0 + 1)];
        // The scripts' own scale: they make a rain's intensity a fifth of its cloud density (WhrRain.c).
        s.clouds = std::clamp(Mix(a.cloud_density, b.cloud_density, f) / 5.0f, 0.0f, 1.0f);
        s.rain = Mix(a.rain_intensity, b.rain_intensity, f);
        const auto wet = [](const WeatherHour &x) { return x.rain || x.storm; };
        const bool previous = h0 == 0 ? in.weather->pre_rain > 0.0f : wet(in.weather->hours[size_t(h0 - 1)]);
        const float rise = Smooth01(f);
        s.rain_clouds = wet(a) || (previous && wet(b)) ? 1.0f : wet(b) ? rise : previous ? 1.0f - rise : 0.0f;
        s.overcast = s.rain_clouds;
        for (int p = h0; p >= 0; --p)
            if (const float left = in.weather->hours[size_t(p)].post_rain; left > 0.0f)
            {
                // A third of the rain's wetness dries per hour, within the hour after it.
                s.wetness = h < float(p) + left ? std::max(left - (h - float(p)) / 3.0f, 0.0f) : 0.0f;
                break;
            }
        s.lightning = a.lightning;
        s.tornado = a.tornado;
        s.rainbow = a.rainbow;
    }
    return s;
}

void LightningFlash::Start(float seconds, int pulses)
{
    if (duration_ > 0.0f || seconds <= 0.0f)
        return;
    time_ = 0.0f;
    duration_ = seconds;
    pulses_ = std::max(pulses, 1);
}

float LightningFlash::Advance(float seconds, float hour)
{
    if (duration_ <= 0.0f)
        return 1.0f;
    time_ = std::min(time_ + seconds, duration_);
    if (time_ >= duration_)
    {
        duration_ = 0.0f;
        return 1.0f;
    }
    const float p = std::fmod(time_ / duration_ * float(pulses_), 1.0f);
    const float pulse = std::min((1.0f - p) / 0.9f, p * 10.0f);
    const int h = int(Wrap24(hour));
    const float peak = h >= 6 && h <= 19 ? 0.5f : 40.0f;
    return 1.0f + pulse * (peak - 1.0f);
}

void ApplyFlash(State &state, float multiplier)
{
    state.sky_day *= multiplier;
    state.sky_night *= multiplier;
    state.fill_intensity *= multiplier;
    state.ambient *= multiplier;
}

} // namespace storm::environment
