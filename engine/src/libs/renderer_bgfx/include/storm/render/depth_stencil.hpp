// The depth + stencil format of the engine's render targets: the scene's depth and its copies, the interface layers.
#pragma once

#include <bgfx/bgfx.h>

namespace storm::render
{

// D24S8 where the device renders to it, else D32FS8: a Vulkan device must have one of the two, and bgfx offers no
// D24S8 on a device with the second alone (Xclipse). After bgfx::init.
inline bgfx::TextureFormat::Enum DepthStencilFormat()
{
    const uint32_t d24s8 = bgfx::getCaps()->formats[bgfx::TextureFormat::D24S8];
    return (d24s8 & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ? bgfx::TextureFormat::D24S8 : bgfx::TextureFormat::D32FS8;
}

} // namespace storm::render
