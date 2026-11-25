// The "gpu passes:" line's sums (src/shim/gpu_passes.hpp) from bgfx statistics as the backend fills them: the frame's
// GPU time in every frame, per-view times in sampled frames only (detailed diagnostics on), a view's GPU result
// belonging to the previous sampled frame and its names.
#include "gpu_passes.hpp"
#include "log_lines.hpp"

#include <catch2/catch.hpp>

#include <cstring>
#include <string>
#include <vector>

using storm::shim::GpuPasses;
using storm::shim::GpuPassTimes;
using storm::shim::GpuPassWindow;

namespace
{
constexpr int64_t kFrequency = 1000000; // ticks per second: 1000 ticks are 1 ms

struct View
{
    bgfx::ViewId id;
    const char *name;
    double cpu_ms;
    uint32_t gpu_frame = 0; // the frame of the GPU result (0: none)
    double gpu_ms = 0.0;
};

// The statistics of a frame whose GPU time (of frame `gpu_frame`) is `gpu_ms`, with `views` when it was sampled.
struct Stats
{
    bgfx::Stats stats{};
    std::vector<bgfx::ViewStats> views;

    Stats(uint32_t gpu_frame, double gpu_ms, const std::vector<View> &list = {}) : views(list.size())
    {
        stats.cpuTimerFreq = kFrequency;
        stats.gpuTimerFreq = gpu_ms > 0.0 ? kFrequency : 0;
        stats.gpuTimeBegin = 5000;
        stats.gpuTimeEnd = 5000 + static_cast<int64_t>(gpu_ms * 1000.0);
        stats.gpuFrameNum = gpu_frame;
        for (size_t i = 0; i < list.size(); ++i)
        {
            bgfx::ViewStats &view = views[i];
            std::strncpy(view.name, list[i].name, sizeof(view.name) - 1);
            view.view = list[i].id;
            view.cpuTimeBegin = 100;
            view.cpuTimeEnd = 100 + static_cast<int64_t>(list[i].cpu_ms * 1000.0);
            view.gpuFrameNum = list[i].gpu_frame;
            view.gpuTimeBegin = list[i].gpu_frame ? 7000 : 0;
            view.gpuTimeEnd = list[i].gpu_frame ? 7000 + static_cast<int64_t>(list[i].gpu_ms * 1000.0) : 0;
        }
        stats.numViews = static_cast<uint16_t>(list.size());
        stats.viewStats = views.data();
    }
};

const GpuPassTimes *find(const GpuPassWindow &window, const std::string &name)
{
    for (const GpuPassTimes &pass : window.passes)
        if (pass.name == name)
            return &pass;
    return nullptr;
}

// Runs frames until the next sampled one is about to be submitted; frame numbers go on from `frame`.
uint32_t run_to_sample(GpuPasses &passes, uint32_t frame, double gpu_ms)
{
    while (!passes.sample_next())
    {
        passes.add(Stats(frame - 2, gpu_ms).stats, frame, false);
        ++frame;
    }
    return frame;
}
} // namespace

TEST_CASE("without timer queries nothing is sampled and no line is written")
{
    storm::render::SetDetailedDiagnostics(true);
    GpuPasses passes;
    for (uint32_t frame = 1; frame <= 3 * GpuPasses::kSampleFrames; ++frame)
    {
        REQUIRE_FALSE(passes.sample_next());
        passes.add(Stats(0, 0.0).stats, frame, false);
    }
    const GpuPassWindow window = passes.take();
    CHECK(window.frames == 0);
    CHECK(storm::shim::Describe(window, 30.0).empty());
}

TEST_CASE("without detailed diagnostics no frame is sampled and the line is the frame's GPU time")
{
    storm::render::SetDetailedDiagnostics(false);
    GpuPasses passes;
    passes.add(Stats(1, 5.0).stats, 3, false);
    uint32_t frame = 4;
    for (; frame < 4 + 3 * GpuPasses::kSampleFrames; ++frame)
    {
        REQUIRE_FALSE(passes.sample_next());
        passes.add(Stats(frame - 2, 5.0).stats, frame, false);
    }
    const GpuPassWindow window = passes.take();
    CHECK(window.sampled_frames == 0);
    CHECK(window.frames == 3 * GpuPasses::kSampleFrames + 1);
    CHECK(storm::shim::Describe(window, 30.0) ==
          fmt::format("gpu passes: 30.0 s; gpu_frame_ms mean 5.00 max 5.00 over {} frames", window.frames));
    // Turned on: the next frame is sampled.
    storm::render::SetDetailedDiagnostics(true);
    CHECK(passes.sample_next());
}

TEST_CASE("the switch logs its state when it changes")
{
    std::vector<std::string> lines;
    storm::host_check::CollectLog([&](spdlog::level::level_enum, std::string_view line) { lines.emplace_back(line); });
    storm::render::SetDetailedDiagnostics(false);
    storm::render::SetDetailedDiagnostics(false);
    storm::render::SetDetailedDiagnostics(true);
    storm::render::SetDetailedDiagnostics(true);
    storm::host_check::StopCollectingLog();
    CHECK(lines == std::vector<std::string>{"diagnostics: detailed off", "diagnostics: detailed on"});
}

TEST_CASE("one presented frame in kSampleFrames carries the per-view queries")
{
    storm::render::SetDetailedDiagnostics(true);
    GpuPasses passes;
    passes.add(Stats(1, 5.0).stats, 3, false); // the first GPU time: timer queries exist
    int sampled = 0;
    for (uint32_t frame = 4; frame < 4 + 10 * GpuPasses::kSampleFrames; ++frame)
    {
        const bool sample = passes.sample_next();
        sampled += sample;
        passes.add(Stats(frame - 2, 5.0).stats, frame, sample);
    }
    CHECK(sampled == 10);
}

TEST_CASE("the frame's GPU time: each result once, the sampled frames' apart")
{
    storm::render::SetDetailedDiagnostics(true);
    GpuPasses passes;
    passes.add(Stats(10, 4.0).stats, 12, false);
    passes.add(Stats(10, 4.0).stats, 13, false); // no new result: the same frame again
    passes.add(Stats(11, 6.0).stats, 14, false);
    uint32_t frame = run_to_sample(passes, 15, 5.0);
    passes.add(Stats(frame - 2, 5.0).stats, frame, true);
    const uint32_t sampled = frame;
    passes.add(Stats(frame - 1, 5.0).stats, frame + 1, false);
    passes.add(Stats(sampled, 9.0).stats, frame + 2, false); // the sampled frame's own time
    const GpuPassWindow window = passes.take();
    // 4, 6, then 5 ms for frames 13 .. sampled - 1, and 9 ms for the sampled one.
    const uint32_t fives = sampled - 13;
    CHECK(window.frames == 2 + fives + 1);
    CHECK(window.frame_max == Approx(9.0));
    CHECK(window.frame_mean == Approx((4.0 + 6.0 + 5.0 * fives + 9.0) / window.frames));
    CHECK(window.sampled_frames == 1);
    CHECK(window.sampled_mean == Approx(9.0));
    CHECK(passes.take().frames == 0); // a new window
}

TEST_CASE("per pass: the render thread's time of the sampled frame, the GPU time of the one before by its names")
{
    storm::render::SetDetailedDiagnostics(true);
    GpuPasses passes;
    passes.add(Stats(1, 8.0).stats, 3, false);
    // Sampled frame A: views 10 and 11 are both "scene", view 12 is "ui", view 13 "post". Its view list arrives with
    // the next frame's statistics; the GPU results there are older than any sampled frame.
    uint32_t frame = run_to_sample(passes, 4, 8.0);
    const uint32_t a = frame;
    passes.add(Stats(a - 2, 8.0).stats, a, true);
    passes.add(Stats(a - 1, 8.0, {{10, "scene", 1.0}, {11, "scene", 0.5}, {12, "ui", 0.25}, {13, "post", 0.1}}).stats,
               a + 1, false);
    // Sampled frame B: view 10 is now "shadow"; its GPU results are frame A's (view 10 was "scene" then), but view
    // 13's is a stale one of a frame before A.
    frame = run_to_sample(passes, a + 2, 8.0);
    const uint32_t b = frame;
    passes.add(Stats(b - 2, 8.0).stats, b, true);
    passes.add(Stats(b - 1, 8.0,
                     {{10, "shadow", 2.0, a, 3.0},
                      {11, "scene", 0.5, a, 1.0},
                      {12, "ui", 0.5, a, 0.5},
                      {13, "post", 0.1, a - 30, 4.0}})
                   .stats,
               b + 1, false);
    const GpuPassWindow window = passes.take();
    CHECK(window.cpu_samples == 2);
    CHECK(window.gpu_samples == 1);
    const GpuPassTimes *scene = find(window, "scene");
    const GpuPassTimes *shadow = find(window, "shadow");
    const GpuPassTimes *ui = find(window, "ui");
    const GpuPassTimes *post = find(window, "post");
    REQUIRE(scene);
    REQUIRE(shadow);
    REQUIRE(ui);
    REQUIRE(post);
    // GPU: frame A's views 10 and 11 were both "scene"; nothing of B's names, nothing stale.
    CHECK(scene->gpu_mean == Approx(4.0));
    CHECK(scene->gpu_max == Approx(4.0));
    CHECK(ui->gpu_mean == Approx(0.5));
    CHECK(shadow->gpu_mean == 0.0);
    CHECK(post->gpu_mean == 0.0);
    // CPU: scene 1.5 ms in A and 0.5 ms in B; shadow only in B (0 in A).
    CHECK(scene->cpu_mean == Approx(1.0));
    CHECK(scene->cpu_max == Approx(1.5));
    CHECK(shadow->cpu_mean == Approx(1.0));
    CHECK(shadow->cpu_max == Approx(2.0));
    CHECK(ui->cpu_mean == Approx(0.375));
    // The largest GPU time first.
    CHECK(window.passes.front().name == "scene");
    const std::string line = storm::shim::Describe(window, 30.0);
    CHECK(line.rfind("gpu passes: ", 0) == 0); // the diagnostics report keeps the line by this start
    CHECK(line.find("[scene] 4.00/4.00 1.00/1.50") != std::string::npos);
}

TEST_CASE("a view list no sampled frame awaits is not counted")
{
    storm::render::SetDetailedDiagnostics(true);
    GpuPasses passes;
    passes.add(Stats(1, 8.0).stats, 3, false);
    uint32_t frame = run_to_sample(passes, 4, 8.0);
    const uint32_t a = frame;
    passes.add(Stats(a - 2, 8.0).stats, a, true);
    passes.add(Stats(a - 1, 8.0, {{10, "scene", 1.0}}).stats, a + 1, false);
    // Without a sampled frame since, a view list does not belong to anything: it is not counted.
    passes.add(Stats(a, 8.0, {{10, "scene", 7.0, a, 7.0}}).stats, a + 2, false);
    const GpuPassWindow window = passes.take();
    CHECK(window.cpu_samples == 1);
    CHECK(window.gpu_samples == 0);
}

TEST_CASE("a window that took a sampled frame's view list before its GPU time still gives the passes")
{
    storm::render::SetDetailedDiagnostics(true);
    GpuPasses passes;
    passes.add(Stats(1, 8.0).stats, 3, false);
    const uint32_t a = run_to_sample(passes, 4, 8.0);
    passes.add(Stats(a - 2, 8.0).stats, a, true);
    passes.add(Stats(a - 1, 8.0, {{10, "scene", 1.0}}).stats, a + 1, false);
    const GpuPassWindow first = passes.take(); // the sampled frame's own GPU time has not arrived yet
    CHECK(first.sampled_frames == 0);
    CHECK(storm::shim::Describe(first, 30.0).find("[scene]") != std::string::npos);
    passes.add(Stats(a, 9.0).stats, a + 2, false);
    const GpuPassWindow second = passes.take();
    CHECK(second.sampled_frames == 1);
    CHECK(storm::shim::Describe(second, 30.0).find("sampled frames 9.00 over 1") != std::string::npos);
}
