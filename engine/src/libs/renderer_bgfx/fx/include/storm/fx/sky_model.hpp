// CPU side of the sky: single scattering (Rayleigh + Mie) integrated into a
// small panorama that the sky and fog shaders sample with one fetch.
// The panorama is rebuilt only when the weather input changes noticeably.
#pragma once

#include <storm_glm.h>

#include <array>
#include <cstdint>
#include <vector>

namespace storm::fx
{

// The scene environment's sky (storm/environment/state.hpp). Colours are 0..1 gamma-encoded like the game's.
struct WeatherInput
{
    glm::vec3 sun_dir{0.0f, 1.0f, 0.0f};       // unit vector towards the sun (whv_sun_pos normalised), y up
    glm::vec3 sun_color{1.0f, 1.0f, 1.0f};     // whc_sun_color
    glm::vec3 ambient_color{0.4f, 0.4f, 0.4f}; // whc_sun_ambient
    glm::vec3 fog_color{0.5f, 0.6f, 0.7f};     // whc_fog_color
    float fog_density = 0.003f;                // whf_fog_density
    float fog_layer = 0.0f; // metres over which the fog thins by 1/e with height; 0: the game's fog has no layer
    float overcast = 0.0f;  // 0 clear sky .. 1 storm (rain / storm weathers)
    std::array<float, 2> wind{1.0f, 0.0f}; // direction the clouds drift to (x, z), unit
    float wind_speed = 5.0f;               // whf_wind_speed
    glm::vec3 sky_radiance{1.0f};          // the day sky's radiance relative to this model's, exposed
    glm::vec3 night_sky{1.0f};             // the night sky's colour relative to this model's, exposed
    // The drawn sky only (Sky::Draw): the moon and the haze band at the horizon (linear colour; with
    // haze_from_sky a tint of the sky's own colour there), haze_height the sine of its height.
    glm::vec3 moon_dir{0.0f, -1.0f, 0.0f};
    glm::vec3 haze_color{0.0f};
    float haze = 0.0f;
    float haze_height = 0.15f;
    bool haze_from_sky = false;
    // The clouds (Sky::Draw): the cover of the fair-weather and the rain clouds (0..1), how far the wind carried them
    // (metres, x and z), and the light on them (towards it; linear, exposed: the sun, or the moon at night).
    float clouds = 0.0f;
    float rain_clouds = 0.0f;
    glm::vec2 cloud_drift{0.0f};
    glm::vec3 cloud_light_dir{0.0f, 1.0f, 0.0f};
    glm::vec3 cloud_light{0.0f};
};

inline constexpr int kSkyLutWidth = 64;  // azimuth relative to the sun: u = (1 - cos) / 2
inline constexpr int kSkyLutHeight = 32; // elevation: v = sqrt(sin(elevation))

struct SkyState
{
    // kSkyLutWidth * kSkyLutHeight RGBA8, row 0 = horizon. RGB = sqrt(linear colour).
    std::vector<uint8_t> lut;
    glm::vec3 sun_dir{0.0f, 1.0f, 0.0f};
    glm::vec3 sun_light{1.0f, 1.0f, 1.0f}; // linear direct light colour for surfaces
    glm::vec3 dome{0.3f, 0.45f, 0.7f};     // linear, cosine-weighted average of the sky dome
    glm::vec3 horizon{0.5f, 0.6f, 0.7f};   // linear, azimuth average of the horizon row: the fog colour
    float fog_density = 0.003f;
    float fog_layer = 0.0f; // WeatherInput::fog_layer
    float night = 0.0f;     // 0 day .. 1 night
};

SkyState ComputeSky(const WeatherInput &weather);

// The clouds' noise (fs_fx_sky.sc fxCloudNoise): kCloudNoiseSize² texels of one octave of gradient noise that tiles,
// R8 around 0.5, with its mips down to 1 x 1 (each level after the other).
inline constexpr int kCloudNoiseSize = 128;
inline constexpr int kCloudNoiseCells = 16; // lattice cells across the tile
std::vector<uint8_t> CloudNoise();
// The cloud plane's height (metres) and how many noise tiles one height spans: the wind's drift in tiles.
inline constexpr float kCloudHeight = 1500.0f;
inline constexpr float kCloudTilesPerHeight = 0.2f;

// True when `b` differs enough from `a` to justify rebuilding the panorama (what Sky::Draw reads each frame does
// not count).
bool SkyNeedsRebuild(const WeatherInput &a, const WeatherInput &b);

// The panorama's linear sky colour in a unit direction (no sun disc): what `dome` and `horizon` average.
glm::vec3 SkyColor(const SkyState &sky, glm::vec3 dir);

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
