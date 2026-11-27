// The scene environment of a game whose scripts set the weather through the WEATHER entity's attributes and the sky
// through the sky entity's folders («Каждому своё»; README.md).
#pragma once

#include "storm/environment/state.hpp"

#include <optional>
#include <string_view>

namespace storm::environment
{

// A body's arc over the day as the scripts give it (Weather.Sun.* / Weather.Moon.*): it rises at `beg_time` at azimuth
// `beg_angle` and sets at `end_time` at `end_angle` (radians; an arc may run through midnight).
struct LegacyArc
{
    float height = 0.0f;
    float beg_angle = 0.0f, beg_time = 0.0f;
    float end_angle = 0.0f, end_time = 0.0f;
};
// Towards the sun at `hour` (its elevation peaks at `height` at mid-arc); nullopt outside its arc.
std::optional<glm::vec3> LegacySunAt(const LegacyArc &sun, float hour);
// Towards the moon at `hour` (its elevation rises evenly to `height` at mid-arc and falls back); nullopt outside.
std::optional<glm::vec3> LegacyMoonAt(const LegacyArc &moon, float hour);
// Towards a body at `elevation` and `azimuth` (radians) as the scripts place it.
glm::vec3 LegacyDirection(float elevation, float azimuth);

// What a sky folder of the scripts' weather shows: the cover of its fair-weather clouds and of rain clouds.
struct LegacySky
{
    float clouds = 0.0f, rain_clouds = 0.0f;
};
// By the folder's name (its last path part, any case); nullopt for a folder the table does not know.
std::optional<LegacySky> LegacySkyFolder(std::string_view folder);

struct LegacyInputs
{
    float hour = 12.0f;
    LegacyArc sun, moon;
    // The clock stands: the light comes from where the scripts put it (Sun.HeightAngle / AzimuthAngle).
    bool fixed_light = false;
    float light_elevation = 0.0f, light_azimuth = 0.0f;
    // 0..1 gamma-encoded, as the scripts give them.
    glm::vec3 sun_color{1.0f}, ambient{0.0f}, fog_color{0.0f};
    bool fog = false;
    float fog_density = 0.0f;
    float haze_height = 0.0f; // the sine of the fog band's height over the horizon (0: none)
    float rain_drops = 0.0f;  // the rain entity's drops
    LegacySky sky;            // the sky folders' cover at this hour
    float wind_angle = 0.0f, wind_speed = 0.0f;
};
State EvaluateLegacy(const LegacyInputs &in);

} // namespace storm::environment
