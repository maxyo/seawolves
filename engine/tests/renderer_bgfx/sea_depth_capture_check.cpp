// The sea's depth capture (fx/sea_depth.hpp SeaDepth::Capture) through the shim on OpenGL ES and Vulkan: it reports a
// capture only when it was submitted, so a frame whose transient buffer is used up keeps the sea's hard edges instead
// of reading the last frame's depth, and the next frame captures again.
#include "device.hpp"
#include "check_backend.hpp"
#include "storm/fx/sea_depth.hpp"

#include <SDL2/SDL.h>

#include <cstdio>

using namespace storm;

namespace
{
constexpr uint32_t kWidth = 320, kHeight = 180;
constexpr bgfx::ViewId kView = 200;

bool expect(bool ok, const char *what)
{
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    return ok;
}

// Takes every transient vertex the frame has left.
void use_up_transient_vertices()
{
    bgfx::VertexLayout layout;
    layout.begin().add(bgfx::Attrib::Position, 4, bgfx::AttribType::Float).end();
    while (const uint32_t left = bgfx::getAvailTransientVertexBuffer(UINT32_MAX, layout))
    {
        bgfx::TransientVertexBuffer buffer;
        bgfx::allocTransientVertexBuffer(&buffer, left, layout);
    }
}
} // namespace

int main()
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        std::fprintf(stderr, "FAIL: SDL video: %s (an X display is needed)\n", SDL_GetError());
        return 1;
    }
    host_check::UseBackendFromEnvironment();
    SDL_Window *window = SDL_CreateWindow("sea_depth_capture_check", 0, 0, kWidth, kHeight, 0);
    if (!window || !shim::Context::instance().init(window, kWidth, kHeight, false))
    {
        std::fprintf(stderr, "FAIL: no window: %s\n", SDL_GetError());
        return 1;
    }
    bool ok = true;
    {
        D3DPRESENT_PARAMETERS params{};
        params.BackBufferWidth = kWidth;
        params.BackBufferHeight = kHeight;
        auto *device = new shim::Device(window, params);
        const bgfx::TextureHandle depth =
            bgfx::createTexture2D(kWidth, kHeight, false, 1, bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT);
        fx::SeaDepth sea;
        ok &= expect(sea.Capture(kView, depth, kWidth, kHeight, 0.1f, 4000.0f), "a capture is submitted");
        bgfx::frame();
        use_up_transient_vertices();
        ok &= expect(!sea.Capture(kView, depth, kWidth, kHeight, 0.1f, 4000.0f),
                     "no transient vertices left: no capture is claimed");
        bgfx::frame();
        ok &= expect(sea.Capture(kView, depth, kWidth, kHeight, 0.1f, 4000.0f), "the next frame captures again");
        ok &= expect(!sea.Capture(kView, BGFX_INVALID_HANDLE, kWidth, kHeight, 0.1f, 4000.0f),
                     "no depth to read: no capture");
        bgfx::frame();
        sea.Shutdown();
        bgfx::destroy(depth);
        device->Release();
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    return ok ? 0 : 1;
}
