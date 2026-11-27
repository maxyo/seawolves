// The atmosphere state of the weather (fx::SkyState: the place-and-hour grade, the hemisphere ambient, the aerial
// perspective), the uniforms of the emulator's FX_AERIAL variant and the drawn sky.
#pragma once

#include "storm/fx/sky_model.hpp"

#include <bgfx/bgfx.h>

#include <future>

namespace storm::fx
{

class Sky
{
  public:
    Sky() = default;
    ~Sky();
    Sky(const Sky &) = delete;
    Sky &operator=(const Sky &) = delete;

    // Once per frame. Rebuilds the sky state only when the weather moved enough (the hour's step of the sun: about
    // every six seconds of play). The first state is computed at once; a later one on a thread of its own and taken
    // when it is ready, a few frames after the weather moved: ComputeSky is about 2 ms of a desktop core and was a
    // 10-17 ms frame of the game thread on a phone at every step (inside WEATHER's Execute).
    // `camera_height`: the camera's y above the sea, for the haze layer.
    void Update(const WeatherInput &weather, float camera_height);
    const SkyState &State() const
    {
        return state_;
    }

    // FX_AERIAL variant of the fixed-function emulator: set u_fxAerial (needs the scene's world -> view matrix)
    // for the next draw call.
    void BindAerial(const std::array<float, 16> &view);

    // The sky into `view` behind what it holds (depth test at the far plane): the panorama, the sun's and the moon's
    // discs, the stars by night, the clouds and the haze band at the horizon. `corners`: the world directions through
    // the view's bottom-left, bottom-right and top-left corners as D3D orients them; `flip_y`: the backend's clip
    // space is upside down to D3D's; `cloud_octaves`: Settings::cloud_octaves.
    void Draw(bgfx::ViewId view, const std::array<glm::vec3, 3> &corners, bool flip_y, int cloud_octaves);

    void Shutdown();

  private:
    void EnsureResources();
    void UploadLut();

    WeatherInput weather_{};
    SkyState state_{};
    bool state_valid_ = false;
    std::future<SkyState> next_; // the state being computed for next_weather_; invalid: none
    WeatherInput next_weather_{};
    float camera_height_ = 0.0f;
    bool resources_ = false;
    bgfx::UniformHandle u_aerial_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_sky_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_lut_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle lut_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_noise_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle noise_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bool lut_current_ = false; // lut_ holds state_'s panorama
};

} // namespace storm::fx
