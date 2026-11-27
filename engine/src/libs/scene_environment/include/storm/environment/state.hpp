// The scene environment of one frame: light, sky, haze, fog, clouds and rain (README.md). The weather fills it, the
// renderer draws with it.
#pragma once

#include <storm_glm.h>

#include <array>
#include <optional>

namespace storm::environment
{

// How the clouds look and are lit in this hour (the periods of Caribbean Legend's textured clouds, blended; the
// defaults are its engine's own day). Radii and widths in degrees.
struct CloudLook
{
    float intensity = 2.09f; // their radiance multiplier
    float opacity = 0.28f;   // how much of the base and upper layers shows
    float edge = 0.96f;      // the thin edges' light multiplier
    float twilight = 0.0f;   // the share of dawn and sunset in the hour's blend
    float sun_transmission = 1.96f, moon_transmission = 5.13f;
    float sun_radius = 19.1f, moon_radius = 15.1f;
    float sun_intensity = 1.05f, ambient_intensity = 3.22f, sky_tint = 0.63f, contrast = 1.59f;
    float twilight_sun = 0.98f, twilight_base = 2.12f, twilight_warmth = 1.0f, twilight_edge = 0.22f;
    float twilight_opposite = 0.76f, twilight_width = 130.9f, twilight_ambient = 1.42f;
    glm::vec3 twilight_tint{0.4196f, 0.5098f, 0.6824f};
    float moon_intensity = 1.0f;
};

// The camera's grade (a game's data: the period's or the game's own); its exposure is in State::exposure.
struct CameraGrade
{
    float contrast = 1.0f, saturation = 1.0f, temperature = 0.0f, tint = 0.0f;
    glm::vec3 color_filter{1.0f}, lift{0.0f}, gamma{1.0f}, gain{1.0f};
};

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
    // Each body's own light (its disc's tint), whether or not it casts; and the half size of its disc's sprite (the
    // tangent of the angle from its centre to its edge; 0: no disc).
    glm::vec3 sun_light{0.0f}, moon_light{0.0f};
    float sun_size = 0.0f, moon_size = 0.0f;

    // The direct light: the sun, or the moon at night.
    glm::vec3 light_dir{0.0f, 1.0f, 0.0f};
    glm::vec3 light_color{0.0f};
    bool light_is_sun = true;     // the sun is the direct light and stands above the horizon
    float shadow_strength = 1.0f; // 0 none .. 1 full

    glm::vec3 ambient{0.0f}; // the sky's fill light
    glm::vec3 ground{0.0f};  // light from the ground onto downward faces (0: none)
    // The fill the drawn sky gives on top (renderer: ResolveSkyFill): sky_fill times the cosine-weighted mean of the
    // sky above, tinted by sky_tint, onto upward faces; ground_fill times that mean's luminance onto downward ones.
    glm::vec3 sky_fill{0.0f}, sky_tint{1.0f}, ground_fill{0.0f};
    // The fill light's colour and intensity apart (the clouds' ambient light takes them so).
    glm::vec3 fill_color{0.0f};
    float fill_intensity = 0.0f;
    float exposure = 1.0f;            // scene -> display
    std::optional<CameraGrade> grade; // none: the renderer grades the place by its own moods

    // The radiance the sky's air scatters by day and by night; the defaults are Caribbean Legend's engine's own,
    // for a game whose data has none.
    glm::vec3 sky_day{2.0f};
    glm::vec3 sky_night{0.01f, 0.02f, 0.025f};

    // The haze band where sky meets sea.
    glm::vec3 haze_color{0.0f};
    float haze = 0.0f;          // its opacity at the horizon, 0 none
    float haze_height = 0.15f;  // the sine of the band's height above the horizon
    float haze_edge = 1.0f;     // the share of that height its edge fades over
    float haze_sample = 0.0f;   // radians: the elevation the sky's colour is taken at
    bool haze_from_sky = false; // the band takes the sky's colour there times haze_color

    // Exponential fog, thinning with height: density per metre at fog_base, falling by 1/e per fog_layer metres above
    // it (0: the same at every height). The fog begins fog_start metres from the eye and covers at most fog_max; the
    // sky stands fog_end metres away in it (0: the fog leaves the sky clear).
    glm::vec3 fog_color{0.0f};
    float fog_density = 0.0f;
    float fog_base = 0.0f, fog_layer = 0.0f;
    float fog_start = 0.0f, fog_end = 0.0f, fog_max = 1.0f;
    bool fog_from_sky = false; // the fog takes the sky's colour at the horizon

    // Clouds: how much of the sky the fair-weather clouds and the rain clouds cover, 0..1 (with cloud textures the
    // base layer's own opacity stands for `clouds`); how fast the layers turn about the vertical (degrees per second:
    // upper, base, rain) and how far they have (radians, WEATHER adds it up).
    float clouds = 0.0f;
    float rain_clouds = 0.0f;
    std::array<float, 3> cloud_spin{}, cloud_turn{};
    CloudLook cloud_look;

    float overcast = 0.0f; // 0 clear .. 1 storm
    float rain = 0.0f;     // falling now, 0..1
    float wetness = 0.0f;  // how wet the place outdoors is, 0..1 (WetnessAfter)
    bool lightning = false, tornado = false, rainbow = false;

    glm::vec2 wind_dir{1.0f, 0.0f}; // the direction the wind blows to (x, z), unit
    float wind_speed = 0.0f;

    // The hour's bloom and emission (the lit materials and the post chain read these).
    float bloom_intensity = 0.0f, bloom_threshold = 0.0f, bloom_soft = 0.0f;
    float emission = 1.0f;
};

// Caribbean Legend's engine's wetness: while an hour rains it is the rain's strength; once the rain stopped it dries
// by a third per game hour from where it was. `left` the wetness when the rain stopped, `hours` since then.
inline float WetnessAfter(float left, float hours)
{
    return glm::max(left - hours / 3.0f, 0.0f);
}

// Adds the drawn sky's fill to `state`'s ambient and ground: `dome` is the cosine-weighted mean of the sky above
// (linear).
inline void ResolveSkyFill(State &state, const glm::vec3 &dome)
{
    const glm::vec3 sky = dome * state.sky_tint;
    state.ambient += state.sky_fill * sky;
    state.ground += state.ground_fill * state.sky_fill * glm::dot(sky, glm::vec3(0.2126f, 0.7152f, 0.0722f));
}

// A light for the legacy fixed-function pipeline, whose colours stop at 1: exposed, then scaled down until no
// channel exceeds 1, so its hue stays.
inline glm::vec3 DisplayLight(const glm::vec3 &linear, float exposure)
{
    const glm::vec3 c = glm::max(linear * exposure, glm::vec3(0.0f));
    const float peak = glm::max(c.x, glm::max(c.y, c.z));
    return peak > 1.0f ? c / peak : c;
}

} // namespace storm::environment
