// The texture arrays of a layered material (render::EffectsLayerSet; renderer_bgfx README, "Materials").
#pragma once

#include "storm/render/effects.hpp"
#include "texture/core/layer_array.hpp"

#include <bgfx/bgfx.h>

#include <atomic>
#include <memory>

namespace storm::render
{

class EffectsLayerSet
{
  public:
    // The largest side of an array (CL's layers are 1024-4096; its 4096 maps are rma and normal maps of rock).
    static constexpr uint32_t kMaxSide = 2048;

    EffectsLayerSet(const EffectsLayerFiles &files, uint32_t skipped_levels);
    ~EffectsLayerSet();
    EffectsLayerSet(const EffectsLayerSet &) = delete;
    EffectsLayerSet &operator=(const EffectsLayerSet &) = delete;

    struct Array
    {
        bgfx::TextureHandle handle = BGFX_INVALID_HANDLE; // invalid until it is made
        std::array<bool, 3> present{};                    // the layers it holds
    };
    // The array of `kind` for a draw (game thread): asked for the first time, it is read from the disk cache or made
    // from the files on a texture worker, and drawn from the draw after it is there.
    Array array(EffectsLayerKind kind);

  private:
    struct Build;
    EffectsLayerFiles files_;
    uint32_t max_side_;
    std::array<std::shared_ptr<Build>, 3> builds_;
    std::array<Array, 3> arrays_;
    std::array<bool, 3> pending_{}; // counted in LayerArraysPending
};

} // namespace storm::render

namespace storm::effects
{
// Made on a worker: the array of these layer files at most `max_side` wide in `format` (missing or unreadable files
// absent, logged by path), from the cache directory when it holds it; written there when made.
struct LayerArrayResult
{
    texture::ArrayShape shape;
    std::vector<uint8_t> texels;
    std::array<bool, 3> present{};
    bool cached = false;
};
// Arrays asked for and not yet drawable (the scene reveal's product): game thread.
uint32_t LayerArraysPending();
LayerArrayResult MakeLayerArray(const std::array<std::string, 3> &files, render::EffectsLayerKind kind,
                                uint32_t max_side, texture::GpuFormat format);
} // namespace storm::effects
