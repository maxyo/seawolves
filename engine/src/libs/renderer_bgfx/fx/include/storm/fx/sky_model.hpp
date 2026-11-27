// CPU side of the drawn sky (fs_fx_sky.sc): the atmosphere and its haze band at the horizon, from which the fog
// colour of the scene and the scene grade are taken.
#pragma once

#include <storm_glm.h>

#include <array>
#include <cstdint>
#include <vector>

namespace storm::fx
{

// The scene environment's sky (storm/environment/state.hpp), linear, in the scene's light units.
struct WeatherInput
{
    glm::vec3 sun_dir{0.0f, 1.0f, 0.0f}; // towards the sun, y up, also below the horizon
    glm::vec3 moon_dir{0.0f, -1.0f, 0.0f};
    float daylight = 1.0f; // 1 the sky lit by the sun .. 0 by the moon (stars)
    float exposure = 1.0f; // scene -> display
    glm::vec3 sky_day{2.0f};
    glm::vec3 sky_night{0.01f, 0.02f, 0.025f};
    // The discs: each body's own light and its sprite's half size (the tangent of the angle to its edge; 0: none).
    glm::vec3 sun_light{0.0f}, moon_light{0.0f};
    float sun_size = 0.0f, moon_size = 0.0f;
    // The haze band: opacity at the horizon, the sine of its height, its edge's fade, the tangent of the elevation its
    // colour is taken at; with haze_from_sky haze_color tints the atmosphere's there.
    float haze = 0.0f, haze_height = 0.15f, haze_edge = 1.0f, haze_sample = 0.0f;
    glm::vec3 haze_color{0.0f};
    bool haze_from_sky = false;
    // Height fog (environment::State's fog fields); fog_end: how far the sky stands in it (0: the fog leaves the sky).
    glm::vec3 fog_color{0.0f};
    float fog_density = 0.0f, fog_base = 0.0f, fog_layer = 0.0f, fog_start = 0.0f, fog_end = 0.0f, fog_max = 1.0f;
    bool fog_from_sky = false;
    // The clouds: the cover of the fair-weather and the rain clouds (0..1), how far the wind carried them (metres, x
    // and z), and the light on them (towards it; linear, exposed: the sun, or the moon at night).
    float clouds = 0.0f;
    float rain_clouds = 0.0f;
    glm::vec2 cloud_drift{0.0f};
    glm::vec3 cloud_light_dir{0.0f, 1.0f, 0.0f};
    glm::vec3 cloud_light{0.0f};
};

// The share of a pixel's cells of 1/2500 of a direction that hold no star (fs_fx_sky.sc fxStars): Caribbean Legend's
// engine's own setting.
inline constexpr float kStarDensity = 0.96f;

struct SkyState
{
    glm::vec3 sun_dir{0.0f, 1.0f, 0.0f};
    glm::vec3 horizon{0.5f, 0.6f, 0.7f}; // the sky round the horizon under its haze, on the display's scale (0..1)
    glm::vec3 dome{0.3f, 0.45f, 0.7f};   // linear, the cosine-weighted mean of the sky above
    float fog_density = 0.003f;
    float fog_layer = 0.0f; // WeatherInput::fog_layer
    float night = 0.0f;     // 0 day .. 1 night
};

SkyState ComputeSky(const WeatherInput &weather);

// The atmosphere along a unit `dir` lit from `light`: the day's sky (`solar`) or the night's; w the optical depth
// towards the light (red, in powers of two). fs_fx_sky.sc fxAtmosphere.
glm::vec4 SkyAtmosphere(const WeatherInput &weather, glm::vec3 dir, glm::vec3 light, bool solar);
// The sky about the sun and about the moon blended by the daylight (fxSkyAtmosphere), linear.
glm::vec3 SkyBackground(const WeatherInput &weather, glm::vec3 dir);
// The haze band's opacity at an elevation sine (fs_fx_sky.sc main).
float SkyHazeAmount(const WeatherInput &weather, float dir_y);

// The clouds' noise (fs_fx_sky.sc fxCloudNoise): kCloudNoiseSize² texels of one octave of gradient noise that tiles,
// R8 around 0.5, with its mips down to 1 x 1 (each level after the other).
inline constexpr int kCloudNoiseSize = 128;
inline constexpr int kCloudNoiseCells = 16; // lattice cells across the tile
std::vector<uint8_t> CloudNoise();
// The cloud plane's height (metres) and how many noise tiles one height spans: the wind's drift in tiles.
inline constexpr float kCloudHeight = 1500.0f;
inline constexpr float kCloudTilesPerHeight = 0.2f;

// ---- aerial perspective and height haze (shaders/include/fx_aerial.sh) ----
inline constexpr float kHazeLayerHeight = 60.0f;  // metres: the haze over the sea thins out by 1/e per this height
inline constexpr float kHazeDensityFactor = 3.0f; // density of the haze at sea level relative to the game's fog density

struct AerialUniforms
{
    std::array<std::array<float, 4>, 3> u_fxAerial{};
};
// `view` is the world -> view matrix (row-vector convention), `camera_height` the camera's y above the sea.
AerialUniforms ComputeAerial(const SkyState &sky, float camera_height, const std::array<float, 16> &view);
// fxAerialFog (fx_aerial.sh) for an emulator that keeps each draw's own fog colour and density: {haze density relative
// to the fog density at the eye's height (the layer only redistributes the fog with height), 1 / layer thickness}.
// A fog with a layer of its own (`fog_layer` > 0, its density the eye's) is all layer.
// Its desaturation is milder than fxAerial's (FX_AERIAL_FOG_DESATURATION in fx_aerial.sh): the game's palette stays.
inline constexpr float kAerialFogDesaturation = 0.3f;
std::array<float, 2> ComputeAerialFogHaze(float camera_height, float fog_layer);

} // namespace storm::fx
