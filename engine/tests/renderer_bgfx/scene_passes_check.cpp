// The scene's render passes on Vulkan and what they load, store and resolve
// (patches/bgfx-vulkan-traffic-counters.patch, read as the "render passes:" line reads it: shim/gpu_passes.hpp
// PassTraffic). A game frame through the shim at a render scale with MSAA (fx_render_height under the window's height,
// «Средняя»): the scene, the scene boundary (EffectsSceneComplete, the post chain upscales), an interface.
// - «Средняя» asks for 2x: the scene pass has 2 samples (a device without 2x, lavapipe: fewer, never more).
// - At 4x (fx_msaa=4, a count every Vulkan device has), the scene is one render pass through its views (a depth clear
// in the middle opens one): nothing cuts it. Its colour is cleared, resolved in the pass and its samples
//   discarded; its depth is cleared and resolved for the post chain, which carries it to the display size: in the
//   pass where the device can (bgfx::getVkDepthResolve), its samples discarded at the boundary, else by bgfx's
//   resolve pass, which runs once, when the frame buffer is left, and needs the samples stored.
//   STORM_VK_DEPTH_RESOLVE_IN_PASS=0 (the second test) takes the resolve pass. The bytes are worked out by hand from
//   the target size.
// - A viewport change in the middle of the scene cuts the pass (bgfx ends it at another rect): the cut is counted with
//   its cause, and the pass after it loads the samples the one before stored.
// - Draws count by class; with STORM_VK_PIPELINE_STATISTICS=1 (the test sets it) the fragment shader invocations per
//   target pixel: a quad over the whole scene once, an alpha-tested one over its left half.
// - 3D drawn after the scene boundary is depth-tested against the carried depth: a quad between the two scene quads
//   shows only where the nearer one is not, with the nearer one moved from half to half between frames.
#include "check_backend.hpp"
#include "device.hpp"
#include "effects/effects.hpp"
#include "gpu_passes.hpp"
#include "storm/render/effects.hpp"

#include <SDL2/SDL.h>

#include <bgfx/bgfx.h>
#include <bgfx/storm_vk_traffic.h>

namespace bgfx
{
uint32_t getVkPipelinesPending();             // patches/bgfx-vulkan-pipeline-cache.patch
const char *getVkDepthResolve(bool *_inPass); // patches/bgfx-vulkan-resolve-in-pass.patch
} // namespace bgfx

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace storm;
using shim::Device;

namespace
{
// The window (display) and the scene's internal height: a scene of 480x360.
constexpr uint32_t kWidth = 640, kHeight = 480, kRenderHeight = 360;
constexpr uint32_t kSceneWidth = 480, kSceneHeight = 360;
constexpr uint64_t kScenePixels = uint64_t(kSceneWidth) * kSceneHeight; // 172800
// RGBA8 colour 4 bytes a sample; D24S8 depth 3 + stencil 1.
constexpr uint64_t kColourBytes = 4, kDepthBytes = 4;
// The 3D drawn after the scene boundary.
constexpr uint32_t kLateColour = 0xFF00FF00;

void check(HRESULT result, const char *what)
{
    if (FAILED(result))
    {
        std::fprintf(stderr, "FAIL: %s returned 0x%08x\n", what, static_cast<unsigned>(result));
        std::exit(1);
    }
}

bool expect(bool ok, const std::string &what)
{
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
    return ok;
}

struct Vertex
{
    float x, y, z;
    uint32_t color;
};

class Frames
{
  public:
    explicit Frames(Device &device) : d_(device)
    {
        check(d_.GetRenderTarget(0, &back_), "GetRenderTarget");
    }
    ~Frames()
    {
        back_->Release();
    }

    // One game frame; `cut`: the scene's second quad is drawn in a viewport of the left half, then the full viewport
    // again for the third draw. `late`: the alpha-tested quad is on the right half instead with `near_right`, and after
    // the scene boundary a quad between it and the opaque one covers the picture, which is returned (the display's
    // pixels, BGRA).
    std::vector<uint8_t> render(bool cut, bool late = false, bool near_right = false)
    {
        check(d_.BeginScene(), "BeginScene");
        check(d_.SetRenderTarget(0, back_), "SetRenderTarget");
        check(d_.Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0xFF2050A0, 1.0f, 0),
              "Clear");
        D3DMATRIX identity{};
        identity.m[0][0] = identity.m[1][1] = identity.m[2][2] = identity.m[3][3] = 1.0f;
        D3DMATRIX projection{};
        projection.m[0][0] = static_cast<float>(kHeight) / kWidth;
        projection.m[1][1] = 1.0f;
        projection.m[2][2] = 100.0f / 99.0f;
        projection.m[2][3] = 1.0f;
        projection.m[3][2] = -100.0f / 99.0f;
        d_.SetTransform(D3DTS_WORLD, &identity);
        d_.SetTransform(D3DTS_VIEW, &identity);
        d_.SetTransform(D3DTS_PROJECTION, &projection);
        d_.SetRenderState(D3DRS_ZENABLE, TRUE);
        d_.SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        d_.SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        d_.SetRenderState(D3DRS_LIGHTING, FALSE);
        d_.SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
        d_.SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        d_.SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
        d_.SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
        d_.SetTexture(0, nullptr);
        d_.SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
        // An opaque quad far beyond the picture's edges at depth 10, then an alpha-tested one in front of it over the
        // left half (its alpha 0x80 passes the test, so it is drawn everywhere it covers).
        const auto quad = [&](float x0, float x1, float z, uint32_t color, const char *what) {
            const Vertex v[6] = {{x0, -20, z, color}, {x1, -20, z, color}, {x0, 20, z, color},
                                 {x0, 20, z, color},  {x1, -20, z, color}, {x1, 20, z, color}};
            check(d_.DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(Vertex)), what);
        };
        d_.SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        quad(-40, 40, 10, 0xFFC08040, "draw (opaque)");
        // A depth clear in the middle of the scene opens a view of its own (bgfx clears at a view's start), in the
        // same render pass.
        check(d_.Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, 1.0f, 0), "Clear (depth, mid-scene)");
        if (cut)
        {
            const D3DVIEWPORT9 half{0, 0, kWidth / 2, kHeight, 0.0f, 1.0f};
            check(d_.SetViewport(&half), "SetViewport (left half)");
        }
        d_.SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
        d_.SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
        d_.SetRenderState(D3DRS_ALPHAREF, 0x40);
        // The left half of the picture: x < 0 at depth 5 (the projection's x scale is 0.75).
        if (near_right)
            quad(0, 40, 5, 0x8040C080, "draw (alpha-tested)");
        else
            quad(-40, 0, 5, 0x8040C080, "draw (alpha-tested)");
        d_.SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        if (cut)
        {
            const D3DVIEWPORT9 full{0, 0, kWidth, kHeight, 0.0f, 1.0f};
            check(d_.SetViewport(&full), "SetViewport (full)");
            quad(-1, 1, 20, 0xFF808080, "draw (opaque, behind)");
        }
        // The scene boundary and an interface over the processed picture.
        render::EffectsSceneComplete();
        if (late)
        {
            d_.SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
            quad(-40, 40, 7, kLateColour, "draw (after the scene boundary)");
        }
        d_.SetRenderState(D3DRS_ZENABLE, FALSE);
        struct UiVertex
        {
            float x, y, z, rhw;
            uint32_t color;
        };
        const UiVertex ui[6] = {{0, 0, 0, 1, 0xFFE0E0E0},  {32, 0, 0, 1, 0xFFE0E0E0}, {0, 24, 0, 1, 0xFFE0E0E0},
                                {0, 24, 0, 1, 0xFFE0E0E0}, {32, 0, 0, 1, 0xFFE0E0E0}, {32, 24, 0, 1, 0xFFE0E0E0}};
        d_.SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
        check(d_.DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, ui, sizeof(UiVertex)), "draw (interface)");
        check(d_.EndScene(), "EndScene");
        std::vector<uint8_t> pixels;
        if (late)
        {
            shim::Surface *readback = nullptr;
            check(
                d_.CreateOffscreenPlainSurface(kWidth, kHeight, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &readback, nullptr),
                "CreateOffscreenPlainSurface");
            check(d_.GetRenderTargetData(back_, readback), "GetRenderTargetData");
            D3DLOCKED_RECT locked;
            check(readback->LockRect(&locked, nullptr, D3DLOCK_READONLY), "LockRect");
            pixels.resize(size_t(kWidth) * kHeight * 4);
            for (uint32_t y = 0; y < kHeight; ++y)
                std::memcpy(&pixels[size_t(y) * kWidth * 4], static_cast<uint8_t *>(locked.pBits) + y * locked.Pitch,
                            kWidth * 4);
            readback->UnlockRect();
            readback->Release();
        }
        check(d_.Present(), "Present");
        return pixels;
    }

  private:
    Device &d_;
    shim::Surface *back_ = nullptr;
};

// The pixel at (x, y) of a picture render() returned, as the D3DCOLOR it holds.
uint32_t pixel(const std::vector<uint8_t> &pixels, uint32_t x, uint32_t y)
{
    const uint8_t *p = &pixels[(size_t(y) * kWidth + x) * 4];
    return uint32_t(p[3]) << 24 | uint32_t(p[2]) << 16 | uint32_t(p[1]) << 8 | p[0];
}

// The passes of a frame drawn after `after` (frames are drawn until one is published).
std::vector<bgfx::VkPassTraffic> traffic_after(Frames &frames, bool cut, uint32_t after)
{
    std::vector<bgfx::VkPassTraffic> passes(256);
    for (;;)
    {
        frames.render(cut);
        uint32_t frame = 0;
        const uint32_t count = bgfx::getVkPassTraffic(passes.data(), static_cast<uint32_t>(passes.size()), &frame);
        if (frame > after && count <= passes.size())
        {
            passes.resize(count);
            return passes;
        }
    }
}

std::vector<const bgfx::VkPassTraffic *> scene_passes(const std::vector<bgfx::VkPassTraffic> &passes)
{
    std::vector<const bgfx::VkPassTraffic *> scene;
    for (const bgfx::VkPassTraffic &pass : passes)
        if (shim::PassTraffic::IsScene(pass))
            scene.push_back(&pass);
    return scene;
}

uint32_t count_kind(const std::vector<bgfx::VkPassTraffic> &passes, bgfx::VkPassKind::Enum kind)
{
    uint32_t n = 0;
    for (const bgfx::VkPassTraffic &pass : passes)
        n += pass.kind == kind;
    return n;
}

void print(const std::vector<bgfx::VkPassTraffic> &passes)
{
    shim::PassTraffic traffic;
    traffic.add(passes.data(), static_cast<uint32_t>(passes.size()));
    std::printf("     %s\n", shim::PassTraffic::Describe(traffic.take(), 0.0).c_str());
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
    SDL_Window *window = SDL_CreateWindow("scene_passes_check", 0, 0, kWidth, kHeight, 0);
    if (!window || !shim::Context::instance().init(window, kWidth, kHeight, false))
    {
        std::fprintf(stderr, "FAIL: no window: %s\n", SDL_GetError());
        return 1;
    }
    bool ok = true;
    if (bgfx::getRendererType() != bgfx::RendererType::Vulkan)
        std::printf("SKIP: the render backend is not Vulkan: the counters are bgfx's Vulkan backend's\n");
    else
    {
        // The path asked for: the second test keeps bgfx's resolve pass; the first one needs a device that can
        // resolve the depth in the render pass (both host devices can: lavapipe and the desktop GPUs).
        bool in_pass = false;
        const char *depth_resolve = bgfx::getVkDepthResolve(&in_pass);
        const char *keep_pass = std::getenv("STORM_VK_DEPTH_RESOLVE_IN_PASS");
        const bool pass_asked = keep_pass && std::strcmp(keep_pass, "0") == 0;
        std::printf("     depth resolve: %s\n", depth_resolve);
        if (!pass_asked && !in_pass)
        {
            std::printf("NOT_RUN: %s cannot resolve a depth in its render pass\n",
                        shim::Context::instance().device_info().renderer.c_str());
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 77;
        }
        ok &= expect(in_pass != pass_asked, std::string("the depth is resolved ") +
                                                (pass_asked ? "by the resolve pass" : "in the scene's render pass") +
                                                " as asked");
        D3DPRESENT_PARAMETERS params{};
        params.BackBufferWidth = kWidth;
        params.BackBufferHeight = kHeight;
        auto *device = new Device(window, params);
        {
            Frames frames(*device);
            const auto configure = [&](std::vector<render::EffectsOption> options, int height = kRenderHeight) {
                options.push_back({"fx_render_height", height});
                render::ConfigureEffects(1, options);
                while (render::CurrentEffects().pending)
                    frames.render(false);
                // The scene's pipelines exist: no draw is left out waiting for one.
                for (int i = 0; i < 8 || bgfx::getVkPipelinesPending() != 0; ++i)
                    frames.render(false);
                return effects::Host::instance().renderer_state().msaa_samples;
            };
            const std::string &gpu = shim::Context::instance().device_info().renderer;
            const bool software = gpu.find("llvmpipe") != std::string::npos;

            // «Средняя» as it is: the scene has the samples it asks for; a device without that count gets bgfx's next
            // lower one (renderer_vk.cpp init: an unsupported count takes the last supported one below it), never
            // more. bgfx's render-target field is a count: RT or'ed into RT_MSAA_X2 once made every 2x scene 4x.
            uint32_t asked = configure({});
            ok &= expect(asked == 2, "«Средняя» asks for a 2x scene (" + std::to_string(asked) + "x)");
            std::vector<bgfx::VkPassTraffic> passes =
                traffic_after(frames, false, shim::Context::instance().last_frame());
            std::vector<const bgfx::VkPassTraffic *> scene = scene_passes(passes);
            if (!scene.empty())
                ok &= expect(scene[0]->samples == asked || (software && scene[0]->samples < asked),
                             "the scene has the samples asked for: " + std::to_string(scene[0]->samples) + " of " +
                                 std::to_string(asked) + " on " + gpu);

            // The traffic at 4x, a count every Vulkan device has (the spec requires it for colour and depth).
            asked = configure({{"fx_msaa", 4}});
            passes = traffic_after(frames, false, shim::Context::instance().last_frame());
            print(passes);
            scene = scene_passes(passes);
            uint64_t samples = 0;
            // Its two views and the depth pre-pass's twins of them («Средняя» has it on) share the pass.
            ok &= expect(scene.size() == 1 && scene[0]->views >= 2,
                         "the scene is one render pass through all its views (" + std::to_string(scene.size()) +
                             " passes, " + std::to_string(scene.empty() ? 0 : scene[0]->views) + " views)");
            if (!scene.empty())
            {
                const bgfx::VkPassTraffic &pass = *scene[0];
                samples = pass.samples;
                ok &= expect(pass.width == kSceneWidth && pass.height == kSceneHeight,
                             "the scene pass covers the scaled scene target");
                ok &= expect(samples == asked, "fx_msaa=4: the scene has 4 samples (" + std::to_string(samples) + ")");
                ok &= expect(pass.loaded[0] == 0 && pass.loaded[1] == 0, "the scene loads nothing: both are cleared");
                ok &= expect(pass.stored[0] == 0 && pass.resolved[0] == kScenePixels * kColourBytes,
                             "its colour is resolved in the pass (" + std::to_string(pass.resolved[0]) +
                                 " bytes) and its samples not stored");
                if (in_pass)
                {
                    ok &= expect(pass.resolved[1] == kScenePixels * kDepthBytes,
                                 "its depth is resolved in the pass (" + std::to_string(pass.resolved[1]) + " bytes)");
                    ok &= expect(pass.stored[1] == 0, "its depth samples are discarded at the boundary (" +
                                                          std::to_string(pass.stored[1]) + " bytes stored)");
                }
                else
                {
                    ok &= expect(pass.resolved[1] == 0, "its depth is not resolved in the pass");
                    ok &= expect(pass.stored[1] == kScenePixels * samples * kDepthBytes,
                                 "its depth samples are stored for the resolve pass (" +
                                     std::to_string(pass.stored[1]) + " bytes)");
                }
                ok &= expect(pass.draws[bgfx::VkDrawClass::Opaque] == 1 && pass.draws[bgfx::VkDrawClass::Discards] == 1,
                             "one opaque and one alpha-tested draw");
                if (pass.invocations[bgfx::VkDrawClass::Opaque] == UINT64_MAX)
                    ok &= expect(false, "fragment shader invocations counted (STORM_VK_PIPELINE_STATISTICS=1)");
                else
                {
                    const double opaque = double(pass.invocations[bgfx::VkDrawClass::Opaque]) / kScenePixels;
                    const double tested = double(pass.invocations[bgfx::VkDrawClass::Discards]) / kScenePixels;
                    std::printf("     invocations per pixel: opaque %.4f, alpha-tested %.4f\n", opaque, tested);
                    ok &= expect(std::fabs(opaque - 1.0) < 0.02, "the opaque quad runs its shader once a pixel");
                    ok &= expect(std::fabs(tested - 0.5) < 0.02, "the alpha-tested quad once a pixel of its half");
                }
            }
            ok &= expect(count_kind(passes, bgfx::VkPassKind::DepthResolve) == (in_pass ? 0u : 1u),
                         in_pass ? "no resolve pass for the depth"
                                 : "the scene's depth is resolved once, when its frame buffer is left");
            for (const bgfx::VkPassTraffic &pass : passes)
                if (pass.kind == bgfx::VkPassKind::DepthResolve)
                    ok &=
                        expect(pass.loaded[1] == kScenePixels * 3 && pass.stored[1] == kScenePixels * kDepthBytes,
                               "the resolve pass reads sample 0's depth (3 bytes a pixel), loads nothing it "
                               "overwrites and stores the single-sample depth (" +
                                   std::to_string(pass.loaded[1]) + " / " + std::to_string(pass.stored[1]) + " bytes)");
            ok &= expect(count_kind(passes, bgfx::VkPassKind::ResolveCopy) == 0, "no colour is resolved by a copy");

            // A viewport change in the middle of the scene cuts it.
            passes = traffic_after(frames, true, shim::Context::instance().last_frame());
            print(passes);
            scene = scene_passes(passes);
            ok &= expect(scene.size() == 3,
                         "two viewport changes cut the scene into three passes (" + std::to_string(scene.size()) + ")");
            if (scene.size() == 3)
            {
                const bgfx::VkPassTraffic &before = *scene[0], &half = *scene[1], &after = *scene[2];
                ok &= expect(before.end == bgfx::VkPassEnd::Rect && half.end == bgfx::VkPassEnd::Rect,
                             "each cut is counted with its cause: another rect");
                ok &=
                    expect(half.width == kSceneWidth / 2 && half.loaded[0] == kScenePixels / 2 * samples * kColourBytes,
                           "the pass of the left half loads the samples of its half");
                ok &= expect(before.stored[0] == kScenePixels * samples * kColourBytes &&
                                 before.stored[1] == kScenePixels * samples * kDepthBytes &&
                                 after.loaded[0] == before.stored[0] && after.loaded[1] == before.stored[1],
                             "a cut stores the scene's samples and the next pass of the whole scene loads them");
                ok &= expect(after.stored[1] == (in_pass ? 0 : kScenePixels * samples * kDepthBytes),
                             "the last pass " + std::string(in_pass ? "discards" : "stores") + " the depth samples");
            }
            ok &= expect(count_kind(passes, bgfx::VkPassKind::DepthResolve) == (in_pass ? 0u : 1u),
                         in_pass ? "a cut scene resolves its depth in its passes"
                                 : "a cut by another rect keeps the frame buffer: one depth resolve");

            // The 3D after the scene boundary sees the depth of its own frame's scene: the nearer quad moves. At the
            // display size (no render scale) it draws into the scene's own multisampled depth, whose samples stay.
            const auto late_3d = [&](const char *where) {
                for (const bool near_right : {false, true, false})
                {
                    const std::vector<uint8_t> picture = frames.render(false, true, near_right);
                    const uint32_t left = pixel(picture, kWidth / 4, kHeight / 2);
                    const uint32_t right = pixel(picture, 3 * kWidth / 4, kHeight / 2);
                    const uint32_t near = near_right ? right : left, far = near_right ? left : right;
                    char what[160];
                    std::snprintf(what, sizeof(what),
                                  "3D after the boundary: hidden by the nearer quad on the %s (%08x), drawn over the "
                                  "farther one (%08x)",
                                  near_right ? "right" : "left", near, far);
                    ok &= expect(near != kLateColour && far == kLateColour, std::string(where) + what);
                }
            };
            late_3d("");
            configure({{"fx_msaa", 4}}, 0);
            passes = traffic_after(frames, false, shim::Context::instance().last_frame());
            scene = scene_passes(passes);
            ok &= expect(scene.size() == 1 && scene[0]->width == kWidth &&
                             scene[0]->stored[1] == uint64_t(kWidth) * kHeight * samples * kDepthBytes,
                         "at the display size the scene's depth samples are stored for the 3D after the boundary");
            late_3d("at the display size: ");
            render::ConfigureEffects(0, {});
            while (render::CurrentEffects().pending)
                frames.render(false);
        }
        device->Release();
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::printf("%s: scene_passes_check\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
