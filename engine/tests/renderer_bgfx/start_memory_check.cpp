// The start's memory marks through the renderer's own calls (platform/process_memory.hpp StartMemory): the library's
// load, the renderer's start, bgfx::init called and returned, the title as the first scene begun and shown, and one
// "memory at start" line once the scene after it is shown. Headless (bgfx's Noop backend).
#include "device.hpp"

#include "log_lines.hpp"
#include "platform/process_memory.hpp"
#include "storm/render/pipelines.hpp"

#include <catch2/catch.hpp>

#include <mutex>
#include <string>
#include <vector>

using namespace storm;
namespace pm = storm::platform::process_memory;

namespace
{
const pm::StartMark &mark(pm::StartPhase phase)
{
    return pm::Start().Marks()[static_cast<size_t>(phase)];
}

// Frames of the loading transaction until the scene begun last is shown.
void show(shim::Device &d, const char *scene)
{
    render::BeginScenePipelines(scene, 0);
    int frames = 0;
    do
    {
        REQUIRE(SUCCEEDED(d.BeginScene()));
        REQUIRE(SUCCEEDED(d.EndScene()));
        REQUIRE(SUCCEEDED(d.Present()));
        REQUIRE(++frames < 600);
    } while (render::HoldSceneReveal());
}
} // namespace

TEST_CASE("the start's phases are marked in order and logged once the scene after the title is shown")
{
    std::mutex mutex;
    std::vector<std::string> lines;
    host_check::CollectLog([&](spdlog::level::level_enum, std::string_view text) {
        if (text.starts_with("memory at start"))
        {
            std::lock_guard lock(mutex);
            lines.emplace_back(text);
        }
    });
    REQUIRE(shim::Context::instance().init_headless(64, 64));
    D3DPRESENT_PARAMETERS params{};
    params.BackBufferWidth = params.BackBufferHeight = 64;
    auto *device = new shim::Device(nullptr, params);
    show(*device, "check/title");
    CHECK(mark(pm::StartPhase::Loaded).seconds >= 0.0);
    CHECK(mark(pm::StartPhase::RendererBegins).seconds >= mark(pm::StartPhase::Loaded).seconds);
    CHECK(mark(pm::StartPhase::BgfxBegins).seconds >= mark(pm::StartPhase::RendererBegins).seconds);
    CHECK(mark(pm::StartPhase::BgfxInit).seconds >= mark(pm::StartPhase::BgfxBegins).seconds);
    CHECK(mark(pm::StartPhase::TitleBegins).seconds >= mark(pm::StartPhase::BgfxInit).seconds);
    CHECK(mark(pm::StartPhase::TitleShown).seconds >= mark(pm::StartPhase::TitleBegins).seconds);
    CHECK(mark(pm::StartPhase::FirstScene).seconds < 0.0);
    CHECK(lines.empty());
    CHECK_FALSE(render::HoldSceneReveal()); // asked again after the reveal
    CHECK(lines.empty());
    show(*device, "check/next");
    CHECK(mark(pm::StartPhase::FirstScene).seconds >= mark(pm::StartPhase::TitleShown).seconds);
    show(*device, "check/third");
    CHECK_FALSE(render::HoldSceneReveal());
    device->Release();
    shim::Context::instance().shutdown();
    host_check::StopCollectingLog();
    REQUIRE(lines.size() == 1);
    INFO(lines[0]);
    CHECK(lines[0].find("first_scene=none") == std::string::npos);
    CHECK(lines[0].find("title_shown=none") == std::string::npos);
}
