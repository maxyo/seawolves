// The atmosphere state of the weather (fx::SkyState: the place-and-hour grade, the hemisphere ambient, the aerial
// perspective), the uniforms of the emulator's FX_AERIAL variant and the drawn sky.
#pragma once

#include "storm/fx/sky_model.hpp"

#include <bgfx/bgfx.h>

namespace storm::fx
{

class Sky
{
  public:
    Sky() = default;
    ~Sky();
    Sky(const Sky &) = delete;
    Sky &operator=(const Sky &) = delete;

    // Once per frame. `camera_height`: the camera's y above the sea, for the haze layer.
    void Update(const WeatherInput &weather, float camera_height);
    const SkyState &State() const
    {
        return state_;
    }

    // The game's sun and moon sprites (bgfx::TextureHandle::idx, 0xffff: none, no disc). The caller keeps them alive.
    void SetDiscs(uint16_t sun, uint16_t moon);
    // A game's textured cloud layers; a base layer without cubes draws the clouds from noise. The caller keeps the
    // cubes alive.
    void SetCloudLayers(const CloudLayers &layers);

    // FX_AERIAL variant of the fixed-function emulator: set u_fxAerial (needs the scene's world -> view matrix)
    // for the next draw call.
    void BindAerial(const std::array<float, 16> &view);

    // The sky into `view` behind what it holds (depth test at the far plane): the atmosphere, the stars by night, the
    // sun's and the moon's discs, the clouds, the haze band at the horizon and the height fog. `corners`: the world
    // directions through the view's bottom-left, bottom-right and top-left corners as D3D orients them; `flip_y`: the
    // backend's clip space is upside down to D3D's; `cloud_octaves`: Settings::cloud_octaves.
    void Draw(bgfx::ViewId view, const std::array<glm::vec3, 3> &corners, bool flip_y, int cloud_octaves);

    void Shutdown();

  private:
    void EnsureResources();
    bool Textured() const;      // a base layer's cubes are there
    float CloudAmbient() const; // the sky's light on the clouds

    WeatherInput weather_{};
    SkyState state_{};
    bool state_valid_ = false;
    bgfx::TextureHandle sun_disc_ = BGFX_INVALID_HANDLE, moon_disc_ = BGFX_INVALID_HANDLE;
    CloudLayers clouds_{};
    float camera_height_ = 0.0f;
    bool resources_ = false;
    bgfx::UniformHandle u_aerial_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_sky_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_sun_disc_ = BGFX_INVALID_HANDLE, s_moon_disc_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_noise_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle noise_ = BGFX_INVALID_HANDLE;
    // base, upper, rain: positive, negative. Each starts invalid: a zeroed handle is uniform 0, not "none".
    std::array<bgfx::UniformHandle, 6> s_cubes_ = {{BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
                                                    BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE, program_cubes_ = BGFX_INVALID_HANDLE;
};

} // namespace storm::fx
