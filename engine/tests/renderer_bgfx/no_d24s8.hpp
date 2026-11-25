// A GPU check again as on a device that renders to no D24S8 (Vulkan on Xclipse: D32FS8 alone): the tests
// renderer_bgfx.<check>.no_d24s8 (CMakeLists.txt here) set STORM_CHECK_NO_D24S8.
#pragma once

#include "effects/effects.hpp"

#include <bgfx/bgfx.h>

#include <cstdlib>

namespace storm::host_check
{
inline bool NoD24S8Asked()
{
    return std::getenv("STORM_CHECK_NO_D24S8") != nullptr;
}

// After the context started, before the device is created. The render thread does not read the table between frames.
inline void UseDepthStencilFromEnvironment()
{
    if (!NoD24S8Asked())
        return;
    const_cast<bgfx::Caps *>(bgfx::getCaps())->formats[bgfx::TextureFormat::D24S8] = 0;
    effects::Host::instance().on_device_created();
}
} // namespace storm::host_check
