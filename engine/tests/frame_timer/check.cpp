#include "timer.h"

#include <cmath>
#include <cstdio>
#include <limits>

#include <catch2/catch.hpp>

// Game milliseconds handed out for `frames` frames of `frameMs` each.
static double simulate(double frameMs, int frames, double *exact = nullptr)
{
    TIMER timer;
    uint64_t total = 0;
    double real = 0;
    for (int i = 0; i < frames; ++i)
    {
        const uint32_t delta = timer.Advance(static_cast<float>(frameMs));
        REQUIRE((delta >= 1 && delta <= 100));
        REQUIRE((timer.rDelta_Time == delta && timer.Delta_Time == delta));
        REQUIRE(std::fabs(timer.fDeltaTime - std::fmin(frameMs, 100.0)) < 1e-3);
        total += delta;
        real += frameMs;
    }
    if (exact)
        *exact = real;
    return static_cast<double>(total);
}

TEST_CASE("frame_timer")
{
    // 10 simulated seconds at common refresh rates: game time tracks real time.
    for (double hz : {30.0, 45.0, 60.0, 90.0, 120.0, 144.0, 240.0})
    {
        double real;
        const double game = simulate(1000.0 / hz, static_cast<int>(hz * 10), &real);
        REQUIRE(std::fabs(game - real) <= 1.5);
        // What plain truncation (the previous behaviour) would have produced.
        const double truncated = std::floor(1000.0 / hz) * hz * 10;
        std::printf("%5.0f Hz: real %.0f ms, game %.0f ms (truncation: %.0f ms, %+.1f%%)\n", hz, real, game, truncated,
                    (truncated - real) / real * 100.0);
    }
    // Integer frame times are unchanged.
    REQUIRE(simulate(20.0, 500) == 10000.0);
    // Above 1000 fps every frame still gets at least 1 ms (as before); never zero.
    REQUIRE(simulate(0.25, 4000) == 4000.0);
    // Stalls are clamped to 100 ms and not repaid afterwards.
    TIMER timer;
    REQUIRE(timer.Advance(5000.0f) == 100);
    REQUIRE(timer.Advance(16.0f) == 16);
    REQUIRE(timer.Advance(250.5f) == 100);
    REQUIRE(timer.Advance(16.0f) == 16);
    // Clock anomalies never produce a zero or huge delta.
    REQUIRE(timer.Advance(-5.0f) == 1);
    REQUIRE(timer.Advance(std::numeric_limits<float>::quiet_NaN()) == 1);
    REQUIRE(timer.Advance(std::numeric_limits<float>::infinity()) == 100);
    // Fixed delta mode returns the fixed value and keeps accounting.
    timer.SetDelta(33);
    REQUIRE((timer.Advance(16.7f) == 33 && timer.GetDeltaTime() == 33));
    timer.SetDelta(0);
    REQUIRE(timer.GetDeltaTime() == timer.Delta_Time);
    // FPS counter: one refresh per simulated second.
    TIMER fps;
    int rings = 0;
    for (int i = 0; i < 600; ++i)
    {
        fps.Advance(1000.0f / 60.0f);
        rings += fps.Ring;
    }
    REQUIRE(((rings == 10 || rings == 9) && fps.fps >= 59 && fps.fps <= 61)); // the 10th second may end one frame later
    std::puts("frame timer: carry/clamp/minimum PASS");
}
