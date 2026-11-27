#include "layer_array.hpp"

#include "mips.hpp"
#include "storm/render/log.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace storm::texture
{
namespace
{
// The bytes of `level` within a layer's packed levels.
std::span<const uint8_t> LevelBytes(const LayerSource &layer, uint32_t level)
{
    size_t offset = 0;
    for (uint32_t l = 0; l < level; ++l)
        offset += dxt_level_size(layer.layout.format, layer.layout.level_width(l), layer.layout.level_height(l));
    return layer.levels.subspan(
        offset, dxt_level_size(layer.layout.format, layer.layout.level_width(level), layer.layout.level_height(level)));
}

std::vector<uint8_t> Decode(const LayerSource &layer, uint32_t level)
{
    const uint32_t w = layer.layout.level_width(level), h = layer.layout.level_height(level);
    const uint32_t aw = blocks_x(w) * 4, ah = blocks_x(h) * 4;
    std::vector<uint8_t> blocks(size_t(aw) * ah * 4), rgba(size_t(w) * h * 4);
    decode_level(layer.layout.format, LevelBytes(layer, level).data(), w, h, blocks.data());
    for (uint32_t y = 0; y < h; ++y)
        std::memcpy(rgba.data() + size_t(y) * w * 4, blocks.data() + size_t(y) * aw * 4, size_t(w) * 4);
    return rgba;
}

// Bilinear with wrapping: a layer smaller than the array (bimg resizes only float images).
std::vector<uint8_t> Resample(const std::vector<uint8_t> &rgba, uint32_t w, uint32_t h, uint32_t to_w, uint32_t to_h)
{
    std::vector<uint8_t> out(size_t(to_w) * to_h * 4);
    for (uint32_t y = 0; y < to_h; ++y)
    {
        const float fy = (float(y) + 0.5f) * float(h) / float(to_h) - 0.5f;
        const float y0f = std::floor(fy);
        const float ty = fy - y0f;
        const uint32_t y0 = uint32_t(int64_t(y0f) + h) % h, y1 = (y0 + 1) % h;
        for (uint32_t x = 0; x < to_w; ++x)
        {
            const float fx = (float(x) + 0.5f) * float(w) / float(to_w) - 0.5f;
            const float x0f = std::floor(fx);
            const float tx = fx - x0f;
            const uint32_t x0 = uint32_t(int64_t(x0f) + w) % w, x1 = (x0 + 1) % w;
            for (int c = 0; c < 4; ++c)
            {
                const auto at = [&](uint32_t xx, uint32_t yy) { return float(rgba[(size_t(yy) * w + xx) * 4 + c]); };
                const float top = at(x0, y0) + (at(x1, y0) - at(x0, y0)) * tx;
                const float bottom = at(x0, y1) + (at(x1, y1) - at(x0, y1)) * tx;
                out[(size_t(y) * to_w + x) * 4 + c] = uint8_t(std::lround(top + (bottom - top) * ty));
            }
        }
    }
    return out;
}
} // namespace

ArrayShape layer_array_shape(std::span<const std::optional<LayerSource>> layers, uint32_t max_side)
{
    ArrayShape shape;
    for (const std::optional<LayerSource> &layer : layers)
        if (layer && uint64_t(layer->layout.width) * layer->layout.height > uint64_t(shape.width) * shape.height)
        {
            shape.width = layer->layout.width;
            shape.height = layer->layout.height;
        }
    if (!shape.width)
        return {};
    while (std::max(shape.width, shape.height) > max_side && std::min(shape.width, shape.height) > 1)
        shape.width /= 2, shape.height /= 2;
    shape.levels = full_mip_levels(shape.width, shape.height);
    shape.layers = uint32_t(layers.size());
    return shape;
}

size_t layer_array_bytes(const ArrayShape &shape, GpuFormat format)
{
    size_t layer = 0;
    for (uint32_t level = 0; level < shape.levels; ++level)
        layer += gpu_level_size(format, std::max(1u, shape.width >> level), std::max(1u, shape.height >> level));
    return layer * shape.layers;
}

std::vector<uint8_t> build_layer_array(std::span<const std::optional<LayerSource>> layers, const ArrayShape &shape,
                                       GpuFormat format, TexelSpace texels)
{
    if (format != GpuFormat::Bc1 && format != GpuFormat::Etc2Rgb8)
        render::Fatal("layer arrays are BC1 or ETC2 RGB8, asked for format {}", int(format));
    std::vector<uint8_t> out(layer_array_bytes(shape, format));
    uint8_t *at = out.data();
    for (const std::optional<LayerSource> &layer : layers)
    {
        if (!layer)
        {
            at += layer_array_bytes({shape.width, shape.height, shape.levels, 1}, format);
            continue;
        }
        uint32_t level = 0;
        while (level < layer->layout.levels &&
               (layer->layout.level_width(level) > shape.width || layer->layout.level_height(level) > shape.height))
            ++level;
        const bool own = level < layer->layout.levels && layer->layout.level_width(level) == shape.width &&
                         layer->layout.level_height(level) == shape.height;
        std::vector<uint8_t> top;
        if (own)
            top = Decode(*layer, level);
        else
            top = Resample(Decode(*layer, 0), layer->layout.width, layer->layout.height, shape.width, shape.height);
        const std::vector<std::vector<uint8_t>> chain =
            build_mip_chain(top.data(), shape.width, shape.height, shape.levels, false, texels);
        for (uint32_t l = 0; l < shape.levels; ++l)
        {
            const uint32_t w = std::max(1u, shape.width >> l), h = std::max(1u, shape.height >> l);
            if (l == 0 && own && format == GpuFormat::Bc1 && layer->layout.format == DxtFormat::Bc1)
                std::memcpy(at, LevelBytes(*layer, level).data(), gpu_level_size(format, w, h));
            else
                encode_gpu_level(format, l == 0 ? top.data() : chain[l - 1].data(), w, h, at);
            at += gpu_level_size(format, w, h);
        }
    }
    return out;
}

} // namespace storm::texture
