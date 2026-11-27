// The scene environment of one frame: light, sky, haze, fog, clouds and rain (README.md). The weather fills it, the
// renderer draws with it.
#pragma once

#include <storm_glm.h>

namespace storm::environment
{

// Colours are linear. A light's colour carries its intensity in scene units; `exposure` brings the scene to the
// display.
struct State
{
    float hour = 12.0f; // 0..24
    bool sky = true;    // open to the sky: a sky is drawn and the sun or moon lights the place (false: an interior)
    bool sea = false;   // the open sea (its fog profile)

    // The sun and the moon, also below the horizon (world, y up, towards them).
    glm::vec3 sun_dir{0.0f, 1.0f, 0.0f};
    glm::vec3 moon_dir{0.0f, -1.0f, 0.0f};
    float twilight = 1.0f; // daylight in the sky: 1 day .. 0 night (stars)

    // The direct light: the sun, or the moon at night.
    glm::vec3 light_dir{0.0f, 1.0f, 0.0f};
    glm::vec3 light_color{0.0f};
    bool light_is_sun = true;     // the sun is the direct light and stands above the horizon
    float shadow_strength = 1.0f; // 0 none .. 1 full

    glm::vec3 ambient{0.0f}; // the sky's fill light
    glm::vec3 ground{0.0f};  // light from the ground onto downward faces (0: none)
    float exposure = 1.0f;   // scene -> display

    // The visible sky relative to the renderer's own sky model (1: its look): the day's radiance, the night's colour.
    glm::vec3 sky_day{1.0f};
    glm::vec3 sky_night{1.0f};

    // The haze band where sky meets sea.
    glm::vec3 haze_color{0.0f};
    float haze = 0.0f;          // its opacity at the horizon, 0 none
    float haze_height = 0.15f;  // the sine of the band's height above the horizon
    bool haze_from_sky = false; // the band takes the sky's colour at the horizon times haze_color

    // Exponential fog, thinning with height: density per metre at fog_base, falling by 1/e per fog_layer metres above
    // it (0: the same at every height). The fog begins fog_start metres from the eye and covers at most fog_max.
    glm::vec3 fog_color{0.0f};
    float fog_density = 0.0f;
    float fog_base = 0.0f, fog_layer = 0.0f;
    float fog_start = 0.0f, fog_max = 1.0f;
    bool fog_from_sky = false; // the fog takes the sky's colour at the horizon

    // Clouds: how much of the sky the fair-weather clouds and the rain clouds cover, 0..1; how far the wind has
    // carried them (metres, x and z).
    float clouds = 0.0f;
    float rain_clouds = 0.0f;
    glm::vec2 cloud_drift{0.0f};

    float overcast = 0.0f; // 0 clear .. 1 storm
    float rain = 0.0f;     // falling now, 0..1
    float wetness = 0.0f;  // left by a rain that stopped, 0..1
    bool lightning = false, tornado = false, rainbow = false;

    glm::vec2 wind_dir{1.0f, 0.0f}; // the direction the wind blows to (x, z), unit
    float wind_speed = 0.0f;

    // The hour's bloom and emission (the lit materials and the post chain read these).
    float bloom_intensity = 0.0f, bloom_threshold = 0.0f, bloom_soft = 0.0f;
    float emission = 1.0f;
};

// A light for the legacy fixed-function pipeline, whose colours stop at 1: exposed, then scaled down until no
// channel exceeds 1, so its hue stays.
inline glm::vec3 DisplayLight(const glm::vec3 &linear, float exposure)
{
    const glm::vec3 c = glm::max(linear * exposure, glm::vec3(0.0f));
    const float peak = glm::max(c.x, glm::max(c.y, c.z));
    return peak > 1.0f ? c / peak : c;
}

} // namespace storm::environment
