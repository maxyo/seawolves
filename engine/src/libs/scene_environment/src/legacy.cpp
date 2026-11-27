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
    // The weather's light is the sun, or the moon by night; the other body is below the horizon.
    const glm::vec3 light = LegacyDirection(in.light_elevation, in.light_azimuth);
    const glm::vec3 down{0.0f, -1.0f, 0.0f};
    s.sun_dir = in.moon ? down : light;
    s.moon_dir = in.moon ? light : down;
    s.sun_size = in.sun_size;
    s.moon_size = in.moon_size;
    s.twilight = glm::smoothstep(-0.1f, 0.05f, s.sun_dir.y);
    s.light_dir = light;
    s.light_is_sun = !in.moon && light.y > 0.0f;
    s.light_color = in.sun_color * in.sun_color;
    (in.moon ? s.moon_light : s.sun_light) = s.light_color;
    s.ambient = in.ambient * in.ambient;
    s.fog_color = in.fog_color * in.fog_color;
    s.fog_density = in.fog ? in.fog_density : 0.0f;
    // The haze band of Caribbean Legend's atmosphere (its shape: 8.65 degrees high, fading over all of it, in the
    // sky's colour 1.42 degrees up): the scripts' own band was a fog sphere that reached the zenith at night.
    s.haze = 1.0f;
    s.haze_height = std::sin(glm::radians(8.65f));
    s.haze_sample = glm::radians(1.42f);
    s.haze_from_sky = true;
    s.haze_color = glm::vec3(1.0f);
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
