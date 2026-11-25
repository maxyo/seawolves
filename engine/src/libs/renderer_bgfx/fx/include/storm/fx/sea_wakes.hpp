// Ship wakes on the legacy sea (Settings::sea_wakes): the churned water each hull leaves, drawn top-down into
// a target around the camera that the Sea2 pixel shader reads (shaders/include/sea_wakes.sh) for foam and for the
// calmed band under it. Design: fx/README.md, "Ship wakes".
#pragma once

#include "storm/fx/wake_trail.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace storm::fx
{

class SeaWakes
{
  public:
    static constexpr uint16_t kSize = 512;
    static constexpr float kExtent = 512.0f; // metres the target covers

    SeaWakes() = default;
    SeaWakes(const SeaWakes &) = delete;
    SeaWakes &operator=(const SeaWakes &) = delete;
    ~SeaWakes();

    // The program, for the settings switch (counts against `budget` when it has to be loaded).
    bool Prepare(int &budget);
    // This frame's hulls at game time `now` (seconds). `resumed`: the first
    // frame of the sea after frames without it (WakeTrails::Update).
    void Update(std::span<const WaveSource> sources, double now, bool resumed);
    // Draws the wakes around `camera` (absolute world x, z) into the target in `view`. False: nothing drawn (no wake
    // in reach), the target is not to be sampled.
    bool Render(bgfx::ViewId view, std::array<double, 2> camera, double now);
    // Drops every trail (the effect switched off).
    void Reset();
    bgfx::TextureHandle Target() const
    {
        return texture_;
    }
    // The absolute world x, z of the target's centre: uv = (world - centre) / kExtent + 0.5.
    std::array<double, 2> Centre() const
    {
        return centre_;
    }
    void Shutdown();

  private:
    bool Draw(bgfx::ViewId view, bool bow);

    WakeTrails trails_;
    std::vector<WakeVertex> vertices_;
    bgfx::TextureHandle texture_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle target_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_wake_ = BGFX_INVALID_HANDLE;
    std::array<double, 2> centre_{};
    uint32_t unbuffered_ = 0; // bands left out for want of a transient vertex buffer
};

} // namespace storm::fx
