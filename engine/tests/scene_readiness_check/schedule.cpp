// The engine's loading schedule (storm::render::LoadingSchedule) through the host hook the stock loader pumps, with
// the renderer's side as doubles: the submitted cover and the GPU timing are inputs.
#include "storm/render/loading_schedule.hpp"

#include <catch2/catch.hpp>

#include <limits>

namespace
{
struct Renderer
{
    storm::render::SceneLoadStatus status{.generation = 7, .active = true, .state = "preparing", .owner = 0};
    bool cover_presented = false, required = true;
    uint64_t budget_generation = 7, measured = 1000, window_generation = 0, window_until = 0;
    double gpu = 8;
} renderer;
} // namespace

namespace storm::render
{
SceneLoadStatus GetSceneLoadStatus()
{
    return renderer.status;
}
SceneLoadingBudget GetSceneLoadingBudget()
{
    return {.generation = renderer.budget_generation,
            .measured_at_ms = renderer.measured,
            .gpu_ms = renderer.gpu,
            .eligible = renderer.status.active && renderer.required && renderer.cover_presented};
}
void SetSceneLoadingWindow(uint64_t generation, uint64_t deadline)
{
    renderer.window_generation = generation;
    renderer.window_until = deadline;
}
} // namespace storm::render

TEST_CASE("the loading window opens after an opaque cover and closes with the frame, its load or its replacement")
{
    using namespace storm::platform;
    using Phase = LoadingSchedulePhase;
    storm::render::LoadingSchedule schedule;
    uint64_t now = 1000;
    LoadingScheduleRegistration registration(
        [&](uint64_t generation, Phase phase) { return schedule(generation, phase, now); });
    // Only a native transaction reaches the schedule through the hook.
    REQUIRE(PumpLoadingSchedule(7, Phase::Poll) == 0);
    REQUIRE(schedule.generation() == 0);
    {
        LoadingPlatformTransaction transaction;
        auto pump = [&](Phase phase) { return PumpLoadingSchedule(renderer.status.generation, phase); };
        REQUIRE(pump(Phase::FramePresented) == 0);
        REQUIRE(renderer.window_until == 0); // no frame started
        pump(Phase::FrameStarted);
        now = 1004;
        pump(Phase::FramePresented);
        REQUIRE(renderer.window_until == 0); // the cover was not presented opaque
        renderer.cover_presented = true;
        REQUIRE(pump(Phase::FramePresented) == 0); // only a poll waits
        REQUIRE(renderer.window_generation == 7);
        REQUIRE(renderer.window_until == 1016); // twice the GPU time from the frame's start
        REQUIRE(pump(Phase::Poll) == 2);
        now = 1015;
        REQUIRE(pump(Phase::Poll) == 1);
        now = 1016;
        REQUIRE(pump(Phase::Poll) == 0);
        REQUIRE(renderer.window_until == 0); // the loading frame runs at its deadline
        now = 1020;
        pump(Phase::FrameStarted);
        now = 1024;
        pump(Phase::FramePresented);
        REQUIRE(renderer.window_until == 1036);
        pump(Phase::Suspended);
        REQUIRE(renderer.window_until == 0);
        REQUIRE(schedule.generation() == 0);
        now = 1025;
        REQUIRE(pump(Phase::Poll) == 0);
        REQUIRE(renderer.window_until == 0); // nothing paces after a suspension until a frame starts again
        pump(Phase::FrameStarted);
        now = 1028;
        pump(Phase::FramePresented);
        REQUIRE(renderer.window_until == 1041);
        pump(Phase::FrameStarted);
        REQUIRE(renderer.window_until == 0); // a frame drawing closes the window

        // A replacement load revokes the old window and the old frame's start; a late Ended of the old load leaves the
        // new one's; the old load's GPU time paces nothing.
        renderer.status.generation = renderer.budget_generation = 8;
        renderer.measured = now;
        pump(Phase::FramePresented);
        REQUIRE(renderer.window_until == 0);
        renderer.cover_presented = false;
        pump(Phase::FrameStarted);
        now = 1030;
        pump(Phase::FramePresented);
        REQUIRE(renderer.window_until == 0);
        renderer.cover_presented = true;
        renderer.budget_generation = 7;
        pump(Phase::FramePresented);
        REQUIRE(renderer.window_until == 0);
        renderer.budget_generation = 8;
        pump(Phase::FramePresented);
        REQUIRE(renderer.window_generation == 8);
        const auto replacement = renderer.window_until;
        REQUIRE(replacement > now);
        PumpLoadingSchedule(7, Phase::Ended);
        REQUIRE(renderer.window_until == replacement);
        PumpLoadingSchedule(7, Phase::Poll);
        REQUIRE(renderer.window_until == replacement);

        // No usable GPU time, a stale one or no required work: no hold, control goes back to a real frame.
        renderer.gpu = std::numeric_limits<double>::quiet_NaN();
        REQUIRE(pump(Phase::Poll) == 0);
        REQUIRE(renderer.window_until == 0);
        renderer.gpu = 8;
        renderer.measured = 1;
        REQUIRE(pump(Phase::Poll) == 0);
        REQUIRE(renderer.window_until == 0);
        renderer.measured = now;
        renderer.required = false;
        REQUIRE(pump(Phase::Poll) == 0);
        REQUIRE(renderer.window_until == 0);
        renderer.required = true;
        REQUIRE(pump(Phase::Poll) > 0);
        // The load ends: the window closes and the schedule forgets it.
        renderer.status.active = false;
        pump(Phase::Poll);
        REQUIRE(renderer.window_until == 0);
        REQUIRE(schedule.generation() == 0);
    }
    REQUIRE_FALSE(LoadingPlatformTransactionActive());
    REQUIRE(PumpLoadingSchedule(8, Phase::Poll) == 0);
}
