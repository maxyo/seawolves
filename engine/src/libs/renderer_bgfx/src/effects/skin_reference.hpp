#pragma once

#include "storm/fx/skin_tone.hpp"
#include "storm/fx/texture_pixels.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace storm::effects
{
// The vertices a draw reaches, in its vertex buffer's CPU copy (shim::Device::draw_vertices): `count` vertices of
// `stride` bytes from `data`; empty when they cannot be read.
struct DrawVertices
{
    const uint8_t *data = nullptr;
    uint32_t stride = 0, count = 0;
};

// A character atlas's skin colour (fx/skin_tone.hpp), read under the face and hand vertices of its skinned draws. A
// material can first be drawn by a torso-only range, so an invalid reference is tried again at later draws, a bounded
// number of vertices each (rotating through large ranges) and a bounded tail of texels kept between them; only a valid
// reference is final. Unknown skin keeps the torso masked.
class SkinReference
{
  public:
    // A new source of the atlas: its colours at fx::kSkinToneLevel, null for an atlas that is not a character's.
    void reset(std::shared_ptr<const fx::TexturePixels> pixels);
    // Whether tone() reads the draw's vertices (the reference is not final yet).
    bool needs_vertices() const
    {
        return pixels_ && (!tone_ || !tone_->valid);
    }
    // u_fxSkinTone of a draw of `vertices` (render/skinned_vertex.hpp layout) on the atlas, `top` texels on its larger
    // side; `name` for the log.
    std::array<float, 8> tone(const DrawVertices &vertices, uint32_t top, std::string_view name);

  private:
    std::shared_ptr<const fx::TexturePixels> pixels_;
    std::shared_ptr<const fx::SkinTone> tone_;
    bool read_ = false;            // an attempt has been logged
    uint32_t cursor_ = 0;          // where the next walk of a large range starts
    std::vector<uint8_t> samples_; // the partial reference, released once valid
};
} // namespace storm::effects
