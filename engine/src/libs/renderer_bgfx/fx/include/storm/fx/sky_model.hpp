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
    // The clouds: the cover of the fair-weather and the rain clouds (0..1); they are lit by the sun's and the moon's
    // lights above and the fill.
    float clouds = 0.0f;
    float rain_clouds = 0.0f;
    // How the clouds look and are lit (environment::CloudLook): x their radiance multiplier, y how much of the
    // textured base and upper layers shows, z the thin edges' light multiplier, w the share of dawn and sunset in
    // the hour; the sun's and the moon's transmission strengths and radii (degrees); the low sun's light multiplier,
    // its light on the clouds' bases, its warmth and its colour on their edges; the sun's and the sky's light on
    // them, the sky's tint of it and the contrast of the sun's; the twilight's strength on the side away from the
    // sun, the width of that side (degrees) and its ambient light; the twilight's tint and the moon's light on them.
    glm::vec4 cloud_look{0.0f}, cloud_transmission{0.0f}, cloud_sunset{0.0f}, cloud_day{0.0f}, cloud_twilight{0.0f},
        cloud_tint{0.0f};
    glm::vec3 fill_color{0.0f}; // the fill light: its colour and intensity apart
    float fill_intensity = 0.0f;
    std::array<float, 3> cloud_turn{}; // the textured layers' turn about the vertical (radians: upper, base, rain)
};

// One textured cloud layer (texture_library.json's cloud_preset): two cubes (bgfx::TextureHandle::idx, 0xffff: no
// layer) holding its response to light from +x, +y, +z with its opacity, and from -x, -y, -z with its ambient
// response.
struct CloudLayer
{
    uint16_t positive = 0xffff, negative = 0xffff;
    float radiance_scale = 1.0f;
    float ambient_scale = 1.0f;
    bool has_ambient = false; // the negative cube's alpha is the ambient response; else the mean of the six
};
struct CloudLayers
{
    CloudLayer base, upper, rain;
    float ambient_strength = 0.0f; // the sky's light on them
};

// The share of a pixel's cells of 1/2500 of a direction that hold no star (fs_fx_sky.sc fxStars): Caribbean Legend's
// engine's own setting.
inline constexpr float kStarDensity = 0.96f;

// The means of Caribbean Legend's cumulus and rain cubes' six-way responses where they are opaque, times their
// radiance scale (measured over their texels above the horizon): the generated layers' (cloud_cubes.hpp) and, under
// a rain, the sky's state's; and the strength of the sky's light on the generated layers, its cloud presets'.
inline constexpr glm::vec3 kCumulusPositive{0.59f, 1.34f, 0.60f}, kCumulusNegative{0.54f, 1.40f, 0.56f};
inline constexpr glm::vec3 kRainPositive{0.12f, 1.02f, 0.10f}, kRainNegative{0.12f, 1.44f, 0.10f};
inline constexpr float kGeneratedCloudAmbient = 0.15f;

// The haze band's colour by azimuth: kHazeAzimuths colours round the horizon from +x towards +z (fs_fx_sky.sc).
inline constexpr int kHazeAzimuths = 8;

struct SkyState
{
    glm::vec3 sun_dir{0.0f, 1.0f, 0.0f};
    glm::vec3 horizon{0.5f, 0.6f, 0.7f}; // the sky round the horizon under its haze, on the display's scale (0..1)
    glm::vec3 dome{0.3f, 0.45f, 0.7f};   // linear, the cosine-weighted mean of the sky above
    float fog_density = 0.003f;
    float fog_layer = 0.0f; // WeatherInput::fog_layer
    float night = 0.0f;     // 0 day .. 1 night
    // The haze band's colour (linear) at each azimuth, under the rain clouds' cover.
    std::array<glm::vec3, kHazeAzimuths> haze{};
};

// `cloud_ambient`: the strength of the sky's light on the clouds (the game's cloud preset's).
SkyState ComputeSky(const WeatherInput &weather, float cloud_ambient);

// A cloud lit as fs_fx_sky.sc's fxCloudShade lights it (no layer turned, all its light reaching it): its six-way
// responses, opacity and ambient response, seen along `view` against the atmosphere `sky` there. rgb linear, a opacity.
glm::vec4 CloudShade(const WeatherInput &weather, float cloud_ambient, glm::vec3 positive, glm::vec3 negative,
                     float opacity, float ambient, glm::vec3 view, glm::vec3 sky);
// The sky along `dir` under the rain clouds' cover (their mean over the reveal), linear.
glm::vec3 SkyUnderRain(const WeatherInput &weather, float cloud_ambient, glm::vec3 dir);

// The atmosphere along a unit `dir` lit from `light`: the day's sky (`solar`) or the night's; w the optical depth
// towards the light (red, in powers of two). fs_fx_sky.sc fxAtmosphere.
glm::vec4 SkyAtmosphere(const WeatherInput &weather, glm::vec3 dir, glm::vec3 light, bool solar);
// The sky about the sun and about the moon blended by the daylight (fxSkyAtmosphere), linear.
glm::vec3 SkyBackground(const WeatherInput &weather, glm::vec3 dir);
// The haze band's opacity at an elevation sine (fs_fx_sky.sc main).
float SkyHazeAmount(const WeatherInput &weather, float dir_y);

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
