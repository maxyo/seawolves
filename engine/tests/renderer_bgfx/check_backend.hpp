// The backend a GPU check runs on: OpenGL ES, or Vulkan with STORM_CHECK_BACKEND=vulkan (the tests
// renderer_bgfx.<check>.vulkan, CMakeLists.txt here, which fail when the run ends on OpenGL ES). A Vulkan run on a host
// without a Vulkan device exits 77: CTest reports it as not run.
#pragma once

#include "backend_select.hpp"
#include "storm/render/frame_owner.hpp"

#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace storm::host_check
{
// The run asks for Vulkan (expectations that depend on the backend).
inline bool VulkanAsked()
{
    const char *value = std::getenv("STORM_CHECK_BACKEND");
    return value && std::string_view(value) == "vulkan";
}

// Before the device is created.
inline void UseBackendFromEnvironment()
{
    const char *value = std::getenv("STORM_CHECK_BACKEND");
    const std::string_view backend = value ? value : "gles";
    if (backend != "gles" && backend != "vulkan")
    {
        std::fprintf(stderr, "STORM_CHECK_BACKEND=%s is not gles or vulkan\n", value);
        std::exit(2);
    }
    if (backend == "vulkan")
    {
        if (const shim::VulkanDevice device = shim::ProbeVulkan(); !device.problem.empty())
        {
            std::printf("SKIP: no Vulkan device on this host: %s\n", device.problem.c_str());
            std::exit(77);
        }
    }
    render::SetRenderBackend(backend == "vulkan" ? render::Backend::Vulkan : render::Backend::OpenGLES, {});
}
} // namespace storm::host_check
