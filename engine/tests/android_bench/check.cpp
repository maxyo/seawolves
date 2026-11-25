#include "bench.hpp"

#include <algorithm>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include <catch2/catch.hpp>

using namespace storm::render;

namespace
{
constexpr double kFrameS = 1.0 / 60.0;
constexpr float kReferenceMs = 5.0f;
constexpr float kHitchMs = 100.0f;     // the first frames after a switch became active
constexpr float kBackgroundMs = 50.0f; // added while background work runs
constexpr float kIncompleteMs = -3.0f; // a frame that left draws out is cheaper

float CostOf(int config)
{
    return config < 0 ? kReferenceMs : 10.0f + static_cast<float>(config);
}

// A renderer as the bench sees it: a switch stays pending for `pending_frames` while the previous settings draw, the
// first `hitch_frames` after it cost more, GPU times arrive `latency` frames late.
struct Renderer
{
    Bench bench;
    int pending_frames = 0;
    int hitch_frames = 5;
    uint32_t latency = 3;
    uint32_t batch = 1; // GPU times are handed over every `batch` frames, all that arrived meanwhile
    std::function<bool(double)> playing = [](double) { return true; };
    std::function<bool(double)> background = [](double) { return false; }; // relief, upscale or bakes
    enum class Work
    {
        Relief,
        Upscale,
        Bakes
    } work = Work::Relief;
    std::function<bool(uint32_t)> incomplete_at = [](uint32_t) { return false; };
    std::function<bool(double)> backgrounded = [](double) { return false; };

    double now = 0.0;
    uint32_t presented = 0, incomplete = 0;
    int drawn = -1, asked = -1, pending_left = 0, hitch_left = 0;
    std::deque<GpuFrameTime> in_flight;
    std::vector<int> applied;
    std::vector<BenchResult> results;
    std::vector<std::string> invalid;
    std::string aborted;
    bool finished = false;

    explicit Renderer(std::vector<BenchVariant> variants) : bench(BenchMode::Full, std::move(variants))
    {
    }

    void apply(const Bench::Step &step)
    {
        if (!step.apply)
            return;
        applied.push_back(step.variant);
        asked = step.variant;
        pending_left = pending_frames;
    }

    void run(double limit_s)
    {
        apply(bench.start(now));
        while (bench.running() && now < limit_s)
        {
            now += kFrameS;
            ++presented;
            if (asked != drawn && pending_left-- <= 0)
            {
                drawn = asked;
                hitch_left = hitch_frames;
            }
            const bool busy = background(now);
            float ms = CostOf(drawn);
            if (hitch_left > 0)
            {
                --hitch_left;
                ms += kHitchMs;
            }
            if (busy)
                ms += kBackgroundMs;
            if (incomplete_at(presented))
            {
                ++incomplete;
                ms += kIncompleteMs;
            }
            in_flight.push_back({presented, ms});
            std::vector<GpuFrameTime> arrived;
            while (presented % batch == 0 && !in_flight.empty() && in_flight.front().frame + latency <= presented)
            {
                arrived.push_back(in_flight.front());
                in_flight.pop_front();
            }
            Bench::Frame frame;
            frame.now_s = now;
            frame.playing = playing(now);
            frame.backgrounded = backgrounded(now);
            frame.pending = asked != drawn;
            frame.relief = busy && work == Work::Relief;
            frame.upscale = busy && work == Work::Upscale;
            frame.bakes = busy && work == Work::Bakes;
            frame.incomplete = incomplete;
            frame.presented = presented;
            frame.gpu = arrived;
            const Bench::Step step = bench.frame(frame);
            if (step.result)
                results.push_back(*step.result);
            if (!step.invalid.empty())
                invalid.push_back(step.invalid);
            aborted = step.aborted.empty() ? aborted : step.aborted;
            finished = finished || step.finished;
            apply(step);
        }
    }
};

std::vector<BenchVariant> Three()
{
    return {{"a", "x=1"}, {"b", "x=2"}, {"c", "x=3"}};
}

void RequireExact(const Renderer &r, size_t variants)
{
    REQUIRE(r.finished);
    REQUIRE(r.aborted.empty());
    REQUIRE(r.results.size() == variants);
    for (size_t i = 0; i < variants; ++i)
    {
        const BenchResult &result = r.results[i];
        CHECK(result.variant == i);
        CHECK(result.sample.mean_ms == Approx(CostOf(static_cast<int>(i))));
        CHECK(result.sample.p95_ms == Approx(CostOf(static_cast<int>(i))));
        CHECK(result.before.mean_ms == Approx(kReferenceMs));
        CHECK(result.after.mean_ms == Approx(kReferenceMs));
        CHECK(result.sample.gpu_frames == result.sample.frames);
        CHECK(result.sample.frames >= kAutoPresetMinFrames);
        CHECK(result.sample.seconds >= kAutoPresetMeasureSeconds);
    }
}
} // namespace

TEST_CASE("bench: the reference is measured before and after every variant")
{
    Renderer r(Three());
    r.run(600.0);
    RequireExact(r, 3);
    CHECK(r.applied == std::vector<int>{-1, 0, -1, 1, -1, 2, -1});
}

TEST_CASE("bench: a variant is timed only from its own frames once it is active and settled")
{
    // The switch stays pending longer than the settle, the GPU times arrive later than the settle lasts: frames of the
    // previous settings and of the switch's first frames reach the bench while it already settles or measures, and the
    // GPU times come in batches that also hold frames after a measurement's end.
    Renderer r(Three());
    r.pending_frames = 200;
    r.hitch_frames = 20;
    r.latency = 200;
    r.batch = 8;
    r.run(600.0);
    RequireExact(r, 3);
    for (const BenchResult &result : r.results)
        CHECK(result.sample.ready_s >= 200 * kFrameS + kAutoPresetSettleSeconds);
}

TEST_CASE("bench: a frame that left a draw out throws the measurement away")
{
    Renderer r(Three());
    // In the middle of variant 1's first measurement, and once while slot 0 settles.
    std::vector<uint32_t> marks;
    r.incomplete_at = [&](uint32_t frame) {
        if (frame == 30)
            return true;
        if (r.bench.slot() == 3 && r.bench.phase() == Bench::Phase::Measuring && marks.empty())
        {
            marks.push_back(frame);
            return true;
        }
        return false;
    };
    r.run(600.0);
    RequireExact(r, 3);
    REQUIRE(marks.size() == 1);
    CHECK(r.invalid.size() == 1);
    CHECK(r.results[1].sample.measured_again == 1);
    CHECK(r.results[0].sample.measured_again == 0);
}

TEST_CASE("bench: background work holds a slot until it is idle")
{
    for (const Renderer::Work work : {Renderer::Work::Relief, Renderer::Work::Upscale, Renderer::Work::Bakes})
    {
        Renderer r(Three());
        r.work = work;
        // Starts with variant 1's switch (the textures it needs are made) and lasts 4 s.
        double busy_from = -1.0;
        r.background = [&](double now) {
            if (busy_from < 0.0 && r.bench.slot() == 3)
                busy_from = now;
            return busy_from >= 0.0 && now < busy_from + 4.0;
        };
        r.run(600.0);
        RequireExact(r, 3);
        CHECK(r.results[1].sample.ready_s >= 4.0);
    }
}

TEST_CASE("bench: work that starts while a slot is measured throws the measurement away")
{
    Renderer r(Three());
    bool started = false;
    double busy_from = -1.0;
    r.background = [&](double now) {
        if (!started && r.bench.slot() == 1 && r.bench.phase() == Bench::Phase::Measuring)
        {
            started = true;
            busy_from = now;
        }
        return busy_from >= 0.0 && now < busy_from + 1.0;
    };
    r.run(600.0);
    RequireExact(r, 3);
    CHECK(r.invalid.size() == 1);
    CHECK(r.results[0].sample.measured_again == 1);
}

TEST_CASE("bench: it waits for play, and leaving play or the background ends it")
{
    {
        // Asked for in the menu: nothing is measured until the game is played.
        Renderer r(Three());
        r.playing = [](double now) { return now > 5.0; };
        r.run(600.0);
        RequireExact(r, 3);
        CHECK(r.results[0].before.ready_s >= 5.0);
    }
    {
        Renderer r(Three());
        r.playing = [](double now) { return now < 20.0; };
        r.run(600.0);
        CHECK(!r.finished);
        CHECK(!r.aborted.empty());
        CHECK(r.now < 20.1);
    }
    {
        Renderer r(Three());
        r.backgrounded = [](double now) { return now > 20.0 && now < 20.02; };
        r.run(600.0);
        CHECK(!r.finished);
        CHECK(!r.aborted.empty());
    }
}

TEST_CASE("bench: a slot the display holds shows the part of its time the GPU worked")
{
    // 10 ms and 5 ms of GPU work in frames presented every 1/60 s.
    Renderer r({{"a", "x=1"}});
    r.run(600.0);
    REQUIRE(r.results.size() == 1);
    CHECK(r.results[0].sample.gpu_share() == Approx(0.60).margin(0.01));
    CHECK(r.results[0].before.gpu_share() == Approx(0.30).margin(0.01));
}

TEST_CASE("bench: the variants")
{
    const auto quick = BenchVariants(BenchMode::Quick, {});
    REQUIRE(quick.size() == 2);
    CHECK(quick[0].options == "fx_render_height=1200");
    CHECK(quick[1].options == "fx_render_height=1400");

    const auto find = [](const std::vector<BenchVariant> &variants, const std::string &options) {
        return std::any_of(variants.begin(), variants.end(),
                           [&](const BenchVariant &v) { return v.options == options; });
    };
    const auto off = BenchVariants(BenchMode::Full, {});
    const auto on = BenchVariants(BenchMode::Full, {.normal_maps = true, .height_maps = true, .upscale = true});
    for (const char *name : {"texture_normal_maps", "texture_height_maps", "texture_upscale"})
    {
        CHECK(find(off, std::string(name) + "=1"));
        CHECK(find(on, std::string(name) + "=0"));
    }
    // The depth pre-pass is measured both ways whatever the preset has, also at the largest scene, where the GPU is
    // the limit.
    for (const char *with :
         {"", ",fx_render_height=1400", ",fx_light_per_pixel=0,fx_light_point=0,fx_render_height=1400",
          ",fx_msaa=1,fx_render_height=1400"})
        for (const char *on : {"0", "1"})
            CHECK(find(off, std::string("fx_depth_prepass=") + on + with));
    // The relief-only variants draw with the maps whatever the reference has.
    for (const auto &variants : {off, on})
        for (const BenchVariant &v : variants)
            if (v.options.find("fx_parallax") != std::string::npos ||
                v.options.find("fx_character_relief") != std::string::npos)
                CHECK(v.options.find("texture_normal_maps=1,texture_height_maps=1") == 0);
}
