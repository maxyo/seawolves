#include "storm/environment/legacy.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <string>
#include <utility>

namespace storm::environment
{

glm::vec3 LegacyDirection(float elevation, float azimuth)
{
    return {std::cos(elevation) * std::cos(azimuth), std::sin(elevation), -std::cos(elevation) * std::sin(azimuth)};
}

std::optional<glm::vec3> LegacySunAt(const LegacyArc &sun, float hour)
{
    const float k = (hour - sun.beg_time) / (sun.end_time - sun.beg_time);
    if (!(k >= 0.0f && k <= 1.0f))
        return std::nullopt;
    // The arc dips 0.2 rad below the horizon at its ends: the sun sets before its arc ends.
    const float elevation = (sun.height + 0.2f) * std::sin(k * glm::pi<float>()) - 0.2f;
    return LegacyDirection(elevation, sun.beg_angle + k * (sun.end_angle - sun.beg_angle));
}

std::optional<glm::vec3> LegacyMoonAt(const LegacyArc &moon, float hour)
{
    float k;
    if (moon.beg_time < moon.end_time)
        k = (hour - moon.beg_time) / (moon.end_time - moon.beg_time);
    else
    {
        k = hour - moon.beg_time;
        if (k < 0.0f)
            k += 24.0f;
        k /= 24.0f + moon.end_time - moon.beg_time;
    }
    if (!(k >= 0.0f && k <= 1.0f))
        return std::nullopt;
    const float elevation = moon.height * 2.0f * std::min(k, 1.0f - k);
    return LegacyDirection(elevation, moon.beg_angle + k * (moon.end_angle - moon.beg_angle));
}

std::optional<LegacySky> LegacySkyFolder(std::string_view folder)
{
    // «Каждому своё»'s weather\skies folders, by what their painted skies show (clear night, scattered cumulus, the
    // overcast of the late afternoon, the storms' rain clouds).
    static constexpr std::array<std::pair<std::string_view, LegacySky>, 28> kFolders = {{
        {"01", {0.4f, 0.0f}},      {"02", {0.4f, 0.0f}},      {"03", {0.2f, 0.0f}},      {"04", {0.1f, 0.0f}},
        {"05", {0.1f, 0.0f}},      {"06", {0.15f, 0.0f}},     {"07", {0.1f, 0.0f}},      {"08", {0.1f, 0.0f}},
        {"09", {0.25f, 0.0f}},     {"10", {0.3f, 0.0f}},      {"11", {0.15f, 0.0f}},     {"12", {0.1f, 0.0f}},
        {"13", {0.35f, 0.0f}},     {"14", {0.5f, 0.0f}},      {"15", {0.45f, 0.0f}},     {"16", {0.65f, 0.0f}},
        {"17", {0.8f, 0.0f}},      {"18", {0.95f, 0.0f}},     {"19", {0.5f, 0.0f}},      {"20", {0.2f, 0.0f}},
        {"21", {0.6f, 0.0f}},      {"22", {0.4f, 0.0f}},      {"23", {0.5f, 0.0f}},      {"24", {0.3f, 0.0f}},
        {"storm01", {1.0f, 1.0f}}, {"storm02", {1.0f, 1.0f}}, {"storm03", {1.0f, 1.0f}}, {"storm04", {1.0f, 1.0f}},
    }};
    while (!folder.empty() && (folder.back() == '\\' || folder.back() == '/'))
        folder.remove_suffix(1);
    if (const size_t slash = folder.find_last_of("\\/"); slash != std::string_view::npos)
        folder.remove_prefix(slash + 1);
    std::string name(folder);
    std::ranges::transform(name, name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    for (const auto &[key, sky] : kFolders)
        if (key == name)
            return sky;
    return std::nullopt;
}

State EvaluateLegacy(const LegacyInputs &in)
{
    State s;
    s.hour = in.hour;
    const std::optional<glm::vec3> sun = LegacySunAt(in.sun, in.hour);
    const std::optional<glm::vec3> moon = LegacyMoonAt(in.moon, in.hour);
    const glm::vec3 down{0.0f, -1.0f, 0.0f};
    s.sun_dir = sun.value_or(down);
    s.moon_dir = moon.value_or(down);
    s.twilight = glm::smoothstep(-0.1f, 0.05f, s.sun_dir.y);
    // The moon lights the scene while it is up, the sun otherwise; with the clock stopped the scripts place the light.
    s.light_dir = in.fixed_light ? LegacyDirection(in.light_elevation, in.light_azimuth) : moon.value_or(s.sun_dir);
    s.light_is_sun = sun && !moon;
    s.light_color = in.sun_color * in.sun_color;
    s.ambient = in.ambient * in.ambient;
    s.fog_color = in.fog_color * in.fog_color;
    s.fog_density = in.fog ? in.fog_density : 0.0f;
    if (in.fog && in.haze_height > 0.0f)
    {
        s.haze_color = s.fog_color;
        s.haze = 1.0f;
        s.haze_height = in.haze_height;
    }
    // A full rain at the day rains' drop count (3500..5000; a light evening rain 1500, storms 25000+).
    s.rain = std::min(in.rain_drops / 5000.0f, 1.0f);
    s.clouds = in.sky.clouds;
    s.rain_clouds = s.overcast = std::max(in.sky.rain_clouds, s.rain);
    s.shadow_strength = 1.0f - s.rain_clouds; // the rain clouds hide the sun
    s.wind_dir = {std::cos(in.wind_angle), std::sin(in.wind_angle)};
    s.wind_speed = in.wind_speed;
    return s;
}

} // namespace storm::environment
