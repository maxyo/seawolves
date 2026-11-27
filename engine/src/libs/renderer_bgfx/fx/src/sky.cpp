#include "storm/fx/sky.hpp"

#include "gpu_util.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace storm::fx
{
using namespace detail;

namespace
{
constexpr uint32_t kNoiseSampler = 0; // repeat, trilinear
constexpr uint16_t kSkyUniforms = 9;  // u_fxSky, fs_fx_sky.sc
} // namespace

Sky::~Sky()
{
    Shutdown();
}

void Sky::Shutdown()
{
    Destroy(u_aerial_);
    Destroy(u_sky_);
    Destroy(s_lut_);
    Destroy(lut_);
    Destroy(s_noise_);
    Destroy(noise_);
    Destroy(program_);
    lut_current_ = false;
    resources_ = false;
}

void Sky::Update(const WeatherInput &weather, float camera_height)
{
    camera_height_ = camera_height;
    if (next_.valid() && next_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        state_ = next_.get();
        weather_ = next_weather_;
        lut_current_ = false;
    }
    if (!state_valid_)
    {
        weather_ = weather;
        state_ = ComputeSky(weather);
        state_valid_ = true;
        lut_current_ = false;
    }
    else if (!next_.valid() && SkyNeedsRebuild(weather_, weather))
    {
        next_weather_ = weather;
        next_ = std::async(std::launch::async, [weather] { return ComputeSky(weather); });
    }
    // The drawn sky's moon and haze change every frame without a new panorama.
    weather_.moon_dir = weather.moon_dir;
    weather_.haze_color = weather.haze_color;
    weather_.haze = weather.haze;
    weather_.haze_height = weather.haze_height;
    weather_.haze_from_sky = weather.haze_from_sky;
    weather_.clouds = weather.clouds;
    weather_.rain_clouds = weather.rain_clouds;
    weather_.cloud_drift = weather.cloud_drift;
    weather_.cloud_light_dir = weather.cloud_light_dir;
    weather_.cloud_light = weather.cloud_light;
}

void Sky::EnsureResources()
{
    if (resources_)
        return;
    resources_ = true;
    u_aerial_ = bgfx::createUniform("u_fxAerial", bgfx::UniformType::Vec4, 3);
    u_sky_ = bgfx::createUniform("u_fxSky", bgfx::UniformType::Vec4, kSkyUniforms);
    s_lut_ = bgfx::createUniform("s_fxSkyLut", bgfx::UniformType::Sampler);
    program_ = LoadProgram("vs_fx_sky", "fs_fx_sky");
    lut_ = bgfx::createTexture2D(kSkyLutWidth, kSkyLutHeight, false, 1, bgfx::TextureFormat::RGBA8, kClampLinear);
    s_noise_ = bgfx::createUniform("s_fxCloudNoise", bgfx::UniformType::Sampler);
    const std::vector<uint8_t> noise = CloudNoise();
    noise_ = bgfx::createTexture2D(kCloudNoiseSize, kCloudNoiseSize, true, 1, bgfx::TextureFormat::R8, kNoiseSampler,
                                   bgfx::copy(noise.data(), static_cast<uint32_t>(noise.size())));
}

void Sky::UploadLut()
{
    if (lut_current_ || state_.lut.size() != size_t(kSkyLutWidth) * kSkyLutHeight * 4)
        return;
    bgfx::updateTexture2D(lut_, 0, 0, 0, 0, kSkyLutWidth, kSkyLutHeight,
                          bgfx::copy(state_.lut.data(), static_cast<uint32_t>(state_.lut.size())));
    lut_current_ = true;
}

void Sky::Draw(bgfx::ViewId view, const std::array<glm::vec3, 3> &corners, bool flip_y, int cloud_octaves)
{
    EnsureResources();
    if (!bgfx::isValid(program_))
        return;
    UploadLut();
    const WeatherInput &w = weather_;
    // Discs: the sun white-hot in its own colour, the moon pale; both only above the horizon. Stars come out as the
    // sun goes below it; the clouds hide them and the discs.
    const float sun_up = glm::smoothstep(-0.02f, 0.02f, state_.sun_dir.y);
    const float moon_up = glm::smoothstep(-0.02f, 0.02f, w.moon_dir.y) * state_.night;
    const float peak = std::max(std::max(state_.sun_light.x, state_.sun_light.y), std::max(state_.sun_light.z, 1e-3f));
    const glm::vec3 sun =
        glm::mix(state_.sun_light / peak, glm::vec3(1.0f), 0.6f * std::clamp(state_.sun_dir.y * 4.0f, 0.0f, 1.0f));
    const std::array<glm::vec4, kSkyUniforms> u = {{
        {state_.sun_dir, sun_up},
        {sun, 0.0f},
        {w.moon_dir, moon_up},
        {glm::vec3(0.75f, 0.78f, 0.85f), state_.night},
        {w.haze_color, w.haze},
        {1.0f / std::max(w.haze_height, 1e-3f), w.haze_from_sky ? 1.0f : 0.0f, 0.0f, 0.0f},
        {glm::fract(w.cloud_drift / kCloudHeight * kCloudTilesPerHeight), w.clouds, w.rain_clouds},
        {w.cloud_light, float(cloud_octaves)},
        {w.cloud_light_dir, kCloudTilesPerHeight},
    }};
    bgfx::setUniform(u_sky_, u.data(), kSkyUniforms);
    bgfx::setTexture(0, s_lut_, lut_, kClampLinear);
    bgfx::setTexture(1, s_noise_, noise_, kNoiseSampler);
    SubmitScreenTriangle(view, program_, BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LEQUAL,
                         flip_y, &corners);
}

void Sky::BindAerial(const std::array<float, 16> &view)
{
    EnsureResources();
    bgfx::setUniform(u_aerial_, ComputeAerial(state_, camera_height_, view).u_fxAerial.data(), 3);
}

} // namespace storm::fx
