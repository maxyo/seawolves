// A stay without a surface (Android: Home, the game thread paused by SDL until DidEnterForeground) through the shim's
// Context: it is no frame's work in the game thread's windows (platform/frame_work.hpp), and on Vulkan it ends the
// pipelines' measured window after a reveal (src/shim/pipeline_sets.hpp) instead of being one of its frame times, and
// it does not count towards a fade-in's wait for the scene's pipelines.
#include "device.hpp"
#include "pipeline_sets.hpp"

#include "check_backend.hpp"
#include "log_lines.hpp"
#include "platform/frame_work.hpp"
#include "storm/render/effects.hpp"
#include "storm/render/pipelines.hpp"

#include <SDL2/SDL.h>
#include <catch2/catch.hpp>

#include <chrono>
#include <mutex>
#include <regex>
#include <string>
#include <thread>
#include <vector>

using namespace storm;
namespace frame_work = storm::platform::frame_work;

namespace
{
constexpr uint32_t kWidth = 96, kHeight = 64;
constexpr auto kStay = std::chrono::milliseconds(400);
constexpr auto kLongStay = shim::PipelineSets::kRevealBound + std::chrono::milliseconds(100);

struct Vertex
{
    float x, y, z;
};

// A frame of a 3D scene: one perspective triangle into the game picture, then the scene's end.
void scene_frame(shim::Device &d)
{
    REQUIRE(SUCCEEDED(d.BeginScene()));
    REQUIRE(SUCCEEDED(d.Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF2050A0, 1.0f, 0)));
    D3DMATRIX identity{}, projection{};
    identity.m[0][0] = identity.m[1][1] = identity.m[2][2] = identity.m[3][3] = 1.0f;
    projection.m[0][0] = projection.m[1][1] = 1.0f;
    projection.m[2][2] = 1.0001f, projection.m[2][3] = 1.0f, projection.m[3][2] = -0.1f;
    d.SetTransform(D3DTS_WORLD, &identity);
    d.SetTransform(D3DTS_VIEW, &identity);
    d.SetTransform(D3DTS_PROJECTION, &projection);
    d.SetRenderState(D3DRS_LIGHTING, FALSE);
    d.SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    d.SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_DISABLE);
    d.SetFVF(D3DFVF_XYZ);
    const Vertex triangle[3] = {{-1, -1, 5}, {1, -1, 5}, {0, 1, 5}};
    REQUIRE(SUCCEEDED(d.DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, triangle, sizeof(Vertex))));
    render::EffectsSceneComplete();
    REQUIRE(SUCCEEDED(d.EndScene()));
    REQUIRE(SUCCEEDED(d.Present()));
}

// What WillEnterBackground and DidEnterForeground do around the game thread's pause.
void stay_in_background()
{
    render::NotifySurfaceLost();
    std::this_thread::sleep_for(kStay);
    render::NotifySurfaceRestored();
}

struct Renderer
{
    SDL_Window *window = nullptr;
    shim::Device *device = nullptr;
    Renderer()
    {
        REQUIRE(SDL_Init(SDL_INIT_VIDEO) == 0);
        host_check::UseBackendFromEnvironment();
        window = SDL_CreateWindow("background_stay_check", 0, 0, kWidth, kHeight, 0);
        REQUIRE(window);
        REQUIRE(shim::Context::instance().init(window, kWidth, kHeight, false));
        D3DPRESENT_PARAMETERS params{};
        params.BackBufferWidth = kWidth;
        params.BackBufferHeight = kHeight;
        device = new shim::Device(window, params);
    }
    ~Renderer()
    {
        device->Release(); // shuts the context down with the device
        shim::Context::instance().shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
    }
};

double ms(int64_t ns)
{
    return static_cast<double>(ns) * 1e-6;
}
} // namespace

TEST_CASE("a stay in the background is no frame's work, nor a frame time of the pipelines' measured window")
{
    std::mutex mutex;
    std::vector<std::string> lines;
    host_check::CollectLog([&](spdlog::level::level_enum, std::string_view text) {
        std::lock_guard lock(mutex);
        lines.emplace_back(text);
    });
    {
        Renderer r;
        shim::Device &d = *r.device;
        render::BeginScenePipelines("check/scene", 0);
        // On Vulkan a scene shown without a fade-in asking for it starts the measured window after its 300th frame.
        for (int i = 0; i < 320; ++i)
            scene_frame(d);
        frame_work::Take(frame_work::Reader::Summary);
        stay_in_background();
        for (int i = 0; i < 3; ++i)
            scene_frame(d);
        const frame_work::Window window = frame_work::Take(frame_work::Reader::Summary);
        INFO(frame_work::Describe(window));
        REQUIRE(window.frames == 3);
        CHECK(ms(window.worst_work) < 0.5 * std::chrono::duration<double, std::milli>(kStay).count());
        render::BeginScenePipelines("check/next", 0); // ends a window still measuring
        if (host_check::VulkanAsked())
        {
            // A fade-in asked, then the app stayed away longer than the wait's bound: on return the wait starts again
            // (two presented frames before it may lift) instead of lifting at once without the pipelines.
            shim::PipelineSets &sets = shim::PipelineSets::instance();
            REQUIRE(sets.hold_reveal());
            render::NotifySurfaceLost();
            std::this_thread::sleep_for(kLongStay);
            render::NotifySurfaceRestored();
            CHECK(sets.hold_reveal());
            bool held = true;
            for (int i = 0; i < 30 && held; ++i)
            {
                scene_frame(d);
                held = sets.hold_reveal();
            }
            CHECK_FALSE(held);
        }
    }
    host_check::StopCollectingLog();
    if (!host_check::VulkanAsked())
        return;
    const std::regex measured(R"(pipelines: first \S+ s of check/scene \([^)]*\): \d+ frames, .* max ([0-9.]+) ms;)");
    std::smatch match;
    bool found = false;
    for (const std::string &line : lines)
        if (std::regex_search(line, match, measured))
        {
            found = true;
            INFO(line);
            CHECK(std::stod(match[1]) < 0.5 * std::chrono::duration<double, std::milli>(kStay).count());
        }
    CHECK(found);
    const std::regex revealed(R"(pipelines: scene check/next revealed: .* ([0-9]+) ms after the scene began;)");
    found = false;
    for (const std::string &line : lines)
        if (std::regex_search(line, match, revealed))
        {
            found = true;
            INFO(line);
            CHECK(std::stod(match[1]) < 0.5 * std::chrono::duration<double, std::milli>(kLongStay).count());
        }
    CHECK(found);
}
