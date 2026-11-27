// The scene environment of a game whose scripts set the weather through the WEATHER entity's attributes and the sky
// through the sky entity's folders («Каждому своё»; README.md).
#pragma once

#include "storm/environment/state.hpp"

#include <optional>
#include <string_view>

namespace storm::environment
{

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
    // Where the hour's weather puts its light (Weather.Sun.HeightAngle / AzimuthAngle, radians) and whether it is the
    // moon (the sun glow's Moon); the half sizes of the sun glow's sun and moon sprites at their distance.
    float light_elevation = 0.0f, light_azimuth = 0.0f;
    bool moon = false;
    float sun_size = 0.0f, moon_size = 0.0f;
    // 0..1 gamma-encoded, as the scripts give them.
    glm::vec3 sun_color{1.0f}, ambient{0.0f}, fog_color{0.0f};
    bool fog = false;
    float fog_density = 0.0f;
    float rain_drops = 0.0f; // the rain entity's drops
    LegacySky sky;           // the sky folders' cover at this hour
    float wind_angle = 0.0f, wind_speed = 0.0f;
};
State EvaluateLegacy(const LegacyInputs &in);

// The wetness of a game whose weather has no rows of hours: at least the rain's strength while it rains, drying as
// WetnessAfter over the game's hours that passed (a skip of hours dries too), so a rain that thins out leaves the
// place as wet as it made it.
class LegacyWetness
{
  public:
    // `rain` the state's rain now, `hour` the game's hour (0..24).
    float Update(float rain, float hour);

  private:
    float level_ = 0.0f;
    float hour_ = -1.0f; // the previous update's (< 0: none yet)
};

} // namespace storm::environment
