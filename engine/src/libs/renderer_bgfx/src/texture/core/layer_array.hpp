// Texture arrays the renderer makes from several game textures of one kind: a layered material's albedo, rma or normal
// maps (renderer_bgfx README, "Materials"). Every layer at one size, its chain rebuilt, in one RGB format.
#pragma once

#include "formats.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace storm::texture
{

// One layer: a game file's DXT levels from the full size down, tightly packed whole blocks (TextureFile's face 0).
struct LayerSource
{
    Layout layout;
    std::span<const uint8_t> levels;
};

struct ArrayShape
{
    uint32_t width = 0, height = 0, levels = 0, layers = 0;
    bool operator==(const ArrayShape &) const = default;
};

// The array of `layers` (nullopt: absent): the size of the largest present layer, halved while its larger side exceeds
// `max_side`, with the chain down to 1x1. No present layer: an empty shape.
ArrayShape layer_array_shape(std::span<const std::optional<LayerSource>> layers, uint32_t max_side);
size_t layer_array_bytes(const ArrayShape &shape, GpuFormat format);

// The array's texels in `format` (Bc1 or Etc2Rgb8), as bgfx takes an array: layer by layer, each its levels from the
// top. A present layer starts from its own level of the array's size (on BC1 that level's blocks as they are), else
// from its top level resampled (wrapping: the layers tile); its chain is rebuilt (mips.hpp, `texels`). An absent
// layer is zero blocks: the material gives it no weight.
std::vector<uint8_t> build_layer_array(std::span<const std::optional<LayerSource>> layers, const ArrayShape &shape,
                                       GpuFormat format, TexelSpace texels);

} // namespace storm::texture
