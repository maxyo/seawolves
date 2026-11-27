#include "storm/fx/sky.hpp"

#include "gpu_util.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace storm::fx
{
using namespace detail;

namespace
{
constexpr uint32_t kNoiseSampler = 0;                 // repeat, trilinear
constexpr uint16_t kSkyUniforms = 27 + kHazeAzimuths; // u_fxSky, fs_fx_sky.sc
constexpr uint8_t kFirstCube = 4;                     // s_fxCloud*, fs_fx_sky.sc

// A textured layer's uniform: its turn's cosine and sine, its radiance scale (0: no layer) and its ambient response's
// scale (below 0: the mean of its six responses).
glm::vec4 LayerUniform(const CloudLayer &layer, float turn)
{
    const bool drawn = layer.positive != bgfx::kInvalidHandle && layer.negative != bgfx::kInvalidHandle;
    return {std::cos(turn), std::sin(turn), drawn ? layer.radiance_scale : 0.0f,
            layer.has_ambient ? layer.ambient_scale : -1.0f};
}
} // namespace

Sky::~Sky()
{
    Shutdown();
}

void Sky::Shutdown()
{
    Destroy(u_aerial_);
    Destroy(u_sky_);
    Destroy(s_sun_disc_);
    Destroy(s_moon_disc_);
    Destroy(s_noise_);
    Destroy(noise_);
    for (bgfx::UniformHandle &cube : s_cubes_)
        Destroy(cube);
    Destroy(program_);
    Destroy(program_cubes_);
    resources_ = false;
}

void Sky::Update(const WeatherInput &weather, float camera_height)
{
    camera_height_ = camera_height;
    // The sky moves with the clock, so most frames compute it anew; a frame asks more than once.
    if (state_valid_ && std::memcmp(&weather_, &weather, sizeof weather) == 0)
        return;
    weather_ = weather;
    state_ = ComputeSky(weather);
    state_valid_ = true;
}

void Sky::SetDiscs(uint16_t sun, uint16_t moon)
{
    sun_disc_.idx = sun;
    moon_disc_.idx = moon;
}

void Sky::SetCloudLayers(const CloudLayers &layers)
{
    clouds_ = layers;
    state_valid_ = false;
}

bool Sky::Textured() const
{
    return clouds_.base.positive != bgfx::kInvalidHandle && clouds_.base.negative != bgfx::kInvalidHandle;
}

float Sky::CloudAmbient() const
{
    return Textured() ? clouds_.ambient_strength : kNoiseCloudAmbient;
}

void Sky::EnsureResources()
{
    if (resources_)
        return;
    resources_ = true;
    u_aerial_ = bgfx::createUniform("u_fxAerial", bgfx::UniformType::Vec4, 3);
    u_sky_ = bgfx::createUniform("u_fxSky", bgfx::UniformType::Vec4, kSkyUniforms);
    s_sun_disc_ = bgfx::createUniform("s_fxSunDisc", bgfx::UniformType::Sampler);
    s_moon_disc_ = bgfx::createUniform("s_fxMoonDisc", bgfx::UniformType::Sampler);
    program_ = LoadProgram("vs_fx_sky", "fs_fx_sky");
    program_cubes_ = LoadProgram("vs_fx_sky", "fs_fx_sky_cubes");
    static constexpr std::array<const char *, 6> kCubes = {"s_fxCloudBaseA",  "s_fxCloudBaseB", "s_fxCloudUpperA",
                                                           "s_fxCloudUpperB", "s_fxCloudRainA", "s_fxCloudRainB"};
    for (size_t i = 0; i < kCubes.size(); ++i)
        s_cubes_[i] = bgfx::createUniform(kCubes[i], bgfx::UniformType::Sampler);
    s_noise_ = bgfx::createUniform("s_fxCloudNoise", bgfx::UniformType::Sampler);
    const std::vector<uint8_t> noise = CloudNoise();
    noise_ = bgfx::createTexture2D(kCloudNoiseSize, kCloudNoiseSize, true, 1, bgfx::TextureFormat::R8, kNoiseSampler,
                                   bgfx::copy(noise.data(), static_cast<uint32_t>(noise.size())));
}

void Sky::Draw(bgfx::ViewId view, const std::array<glm::vec3, 3> &corners, bool flip_y, int cloud_octaves)
{
    EnsureResources();
    // The textured layers draw where a base layer's cubes are there; else the clouds come from noise.
    const bool cubes = Textured();
    const bgfx::ProgramHandle program = cubes ? program_cubes_ : program_;
    if (!bgfx::isValid(program))
        return;
    const WeatherInput &w = weather_;
    const bool sun_disc = bgfx::isValid(sun_disc_), moon_disc = bgfx::isValid(moon_disc_);
    // The camera's yaw from the direction through the view's centre (half way between two opposite corners' rays).
    const glm::vec3 forward = corners[1] + corners[2];
    const float yaw = std::atan2(forward.x, forward.z);
    std::array<glm::vec4, kSkyUniforms> u = {{
        {w.sun_dir, w.daylight},
        {w.moon_dir, w.exposure},
        {w.sky_day, kStarDensity},
        {w.sky_night, 0.0f},
        {w.sun_light, sun_disc ? w.sun_size : 0.0f},
        {w.moon_light, moon_disc ? w.moon_size : 0.0f},
        {w.haze, 1.0f / std::max(w.haze_height, 1e-3f), 1.0f / std::max(w.haze_edge, 0.05f), 0.0f},
        {w.fog_density, w.fog_base, w.fog_layer > 0.0f ? 1.0f / w.fog_layer : 0.0f, w.fog_max},
        {w.fog_start, w.fog_end, camera_height_, w.fog_from_sky ? 1.0f : 0.0f},
        {w.fog_color, 0.0f},
        {std::cos(yaw), std::sin(yaw), 0.0f, 0.0f},
        {glm::fract(w.cloud_drift / kCloudHeight * kCloudTilesPerHeight), w.clouds, w.rain_clouds},
        {float(cloud_octaves), kCloudTilesPerHeight, 0.0f, 0.0f},
        w.cloud_look,
        w.cloud_transmission,
        w.cloud_sunset,
        w.cloud_day,
        {glm::vec3(w.cloud_twilight), CloudAmbient()},
        w.cloud_tint,
        {w.fill_color, w.fill_intensity},
        LayerUniform(clouds_.upper, w.cloud_turn[0]),
        LayerUniform(clouds_.base, w.cloud_turn[1]),
        LayerUniform(clouds_.rain, w.cloud_turn[2]),
        {kCumulusPositive, 0.0f},
        {kCumulusNegative, 0.0f},
        {kRainPositive, 0.0f},
        {kRainNegative, 0.0f},
    }};
    for (int i = 0; i < kHazeAzimuths; ++i)
        u[size_t(27 + i)] = {state_.haze[size_t(i)], 0.0f};
    bgfx::setUniform(u_sky_, u.data(), kSkyUniforms);
    bgfx::setTexture(1, s_noise_, noise_, kNoiseSampler);
    // Without a sprite its disc is off (size 0); the sampler still needs a texture.
    bgfx::setTexture(2, s_sun_disc_, sun_disc ? sun_disc_ : noise_, kClampLinear);
    bgfx::setTexture(3, s_moon_disc_, moon_disc ? moon_disc_ : noise_, kClampLinear);
    if (cubes)
    {
        // A layer without cubes (radiance scale 0 above) still needs a cube on its samplers: the base layer's.
        const std::array<const CloudLayer *, 3> layers = {&clouds_.base, &clouds_.upper, &clouds_.rain};
        for (size_t i = 0; i < layers.size(); ++i)
        {
            const bool drawn =
                layers[i]->positive != bgfx::kInvalidHandle && layers[i]->negative != bgfx::kInvalidHandle;
            const CloudLayer &l = drawn ? *layers[i] : clouds_.base;
            bgfx::setTexture(uint8_t(kFirstCube + 2 * i), s_cubes_[2 * i], {l.positive}, kClampLinear);
            bgfx::setTexture(uint8_t(kFirstCube + 2 * i + 1), s_cubes_[2 * i + 1], {l.negative}, kClampLinear);
        }
    }
    SubmitScreenTriangle(view, program, BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LEQUAL,
                         flip_y, &corners);
}

void Sky::BindAerial(const std::array<float, 16> &view)
{
    EnsureResources();
    bgfx::setUniform(u_aerial_, ComputeAerial(state_, camera_height_, view).u_fxAerial.data(), 3);
}

} // namespace storm::fx
