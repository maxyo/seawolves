#pragma once

#include "shim/shim.hpp"

#include <cstdint>

namespace storm::effects
{
// The depth the sea's capture (fx::SeaDepth) holds: the frame, the target the sea is drawn into and the depth texture
// read. The sea reads it only while all three are those it draws with; a capture that failed holds none.
struct SeaDepthHeld
{
    uint32_t frame = UINT32_MAX;
    const void *target = nullptr;
    const void *source = nullptr;

    bool holds(uint32_t now, const void *drawn_into, const void *read) const
    {
        return frame == now && target == drawn_into && source == read;
    }
    void captured(bool ok, uint32_t now, const void *drawn_into, const void *read)
    {
        *this = ok ? SeaDepthHeld{now, drawn_into, read} : SeaDepthHeld{};
    }
};

// Whether the sea can read the depth of the target it is drawn into. A multisampled target is read through its resolve
// textures, which hold the depth too only where the backend resolves it (`msaa_depth`, Support::msaa_depth) and where
// the depth is no write-only attachment (the scene's at the display size, Device::create_scene_stores).
inline bool SeaDepthReadable(const shim::TexStore *color, const shim::TexStore *depth, bool msaa_depth)
{
    return color && depth && !(depth->target_flags & BGFX_TEXTURE_RT_WRITE_ONLY) &&
           (!(depth->target_flags & BGFX_TEXTURE_RT_MSAA_MASK) || msaa_depth);
}
} // namespace storm::effects
