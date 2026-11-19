// Host checks for src/libs/renderer_bgfx/src/shim/frame_overlap.hpp (when the game thread runs a frame ahead of the
// render thread) and src/libs/renderer_bgfx/src/shim/swap_interval.hpp: the swap interval of a frame rate on
// mocked display mode sets (60, 90, 120 Hz panels and their combinations): it is the setting's, and Swappy's own
// adaptation that would change it with the frame times stays off.
#include "shim/frame_overlap.hpp"
#include "shim/swap_interval.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using storm::shim::ChooseSwapInterval;
using storm::shim::DisplayInMode;

namespace
{
constexpr uint64_t hz(double rate)
{
    return static_cast<uint64_t>(1e9 / rate + 0.5);
}

void expect(bool condition, const char *what)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", what);
        std::exit(1);
    }
}

void expect_choice(std::vector<uint64_t> modes, double current, double fps, double mode, uint64_t refreshes, const char *what)
{
    const auto choice = ChooseSwapInterval(modes, hz(current), hz(fps));
    if (choice.period != hz(mode) || choice.refreshes != refreshes)
    {
        std::fprintf(stderr, "FAIL: %s: got %.2f ms x %llu\n", what, choice.period / 1e6,
                     static_cast<unsigned long long>(choice.refreshes));
        std::exit(1);
    }
}
} // namespace

// Frames overlap only while game work + render work do not fit the swap interval.
void check_frame_overlap()
{
    using storm::shim::FrameOverlap;
    const double interval = 16.67;
    {
        // A fast phone: 5 ms of game work, 3 ms of rendering at 60 fps. Never overlapped.
        FrameOverlap overlap;
        for (int i = 0; i < 1000; ++i)
            expect(!overlap.update(5.0, 3.0, interval) && !overlap.overlapped(), "frames that fit stay serial");
        // One long frame (a load hitch) does not switch.
        overlap.update(60.0, 3.0, interval);
        expect(!overlap.overlapped(), "a single long frame keeps the frames serial");
        overlap.update(5.0, 3.0, interval);
        expect(!overlap.overlapped(), "and so does the frame after it");
    }
    {
        // A slow phone: 12 ms + 9 ms do not fit 16.67 ms, each fits alone.
        FrameOverlap overlap;
        int frames = 0;
        while (!overlap.overlapped() && frames < 100)
        {
            overlap.update(12.0, 9.0, interval);
            ++frames;
        }
        expect(frames == 2, "two frames in a row that do not fit overlap the next ones");
        // The scene gets light: back to serial only after a calm stretch.
        for (int i = 0; i < FrameOverlap::kCalmFrames - 1; ++i)
            overlap.update(4.0, 3.0, interval);
        expect(overlap.overlapped(), "a short calm stretch keeps the overlap");
        for (int i = 0; i < 20; ++i)
            overlap.update(4.0, 3.0, interval);
        expect(!overlap.overlapped(), "a long calm stretch ends the overlap");
    }
    {
        // A load just under the interval (15 of 16.67 ms) still overlaps: no margin for the present.
        FrameOverlap overlap;
        for (int i = 0; i < 60; ++i)
            overlap.update(9.0, 6.0, interval);
        expect(overlap.overlapped(), "a load of 90% of the interval overlaps");
        // Around the leave threshold with peaks: stays overlapped, no flapping.
        int changes = 0;
        for (int i = 0; i < 2000; ++i)
            changes += overlap.update(i % 50 == 0 ? 14.0 : 5.0, 4.0, interval);
        expect(changes == 0 && overlap.overlapped(), "regular peaks above the calm limit keep the overlap");
    }
    {
        // No known interval (desktop): overlapped at once.
        FrameOverlap overlap;
        expect(overlap.update(1.0, 1.0, 0.0) && overlap.overlapped(), "without a frame rate frames overlap");
    }
}

int main()
{
    check_frame_overlap();
    const std::vector<uint64_t> phone = {hz(60), hz(90), hz(120)}; // e.g. Xiaomi 15 Ultra
    expect_choice(phone, 120, 60, 120, 2, "60 fps on a 120 Hz panel in its 120 Hz mode: every second refresh, no switch");
    expect_choice(phone, 60, 60, 60, 1, "60 fps in the 60 Hz mode stays there");
    expect_choice(phone, 90, 60, 60, 1, "60 fps from the 90 Hz mode: the exact 60 Hz mode, the slower of the exact ones");
    expect_choice(phone, 120, 120, 120, 1, "120 fps: the 120 Hz mode");
    expect_choice(phone, 60, 120, 120, 1, "120 fps from the 60 Hz mode: switches to 120 Hz");
    expect_choice(phone, 120, 90, 90, 1, "90 fps: the 90 Hz mode");
    expect_choice(phone, 120, 30, 120, 4, "30 fps on 120 Hz: every fourth refresh, no switch");
    expect_choice({hz(60)}, 60, 30, 60, 2, "30 fps on a 60 Hz panel (the emulator): every second refresh");
    expect_choice({hz(60)}, 60, 60, 60, 1, "60 fps on a 60 Hz panel");
    expect_choice({hz(90)}, 90, 30, 90, 3, "30 fps on a 90 Hz-only panel");
    expect_choice({hz(59.94)}, 59.94, 60, 59.94, 1, "a 59.94 Hz panel counts as 60");
    expect(ChooseSwapInterval(std::vector<uint64_t>{0}, 0, hz(60)).period == 0, "no usable period: no choice");

    // The interval is the setting's on every panel the phone may be in: 120 fps presents at every 120 Hz refresh,
    // 60 fps every 16.67 ms, whatever mode the display starts in.
    for (const double current : {60.0, 90.0, 120.0})
        for (const double fps : {30.0, 60.0, 90.0, 120.0})
        {
            const auto choice = ChooseSwapInterval(phone, hz(current), hz(fps));
            const double ms = choice.interval() / 1e6;
            expect(std::fabs(ms - 1000.0 / fps) < 0.01, "the swap interval is the frame rate setting's");
        }
    // Nothing else sets it: Swappy's auto swap interval (slower pacing for late frames, unpaced past its maximum) and
    // auto pipeline mode stay off, so late frames and heat leave the interval as chosen.
    static_assert(!storm::shim::kSwappyAutoSwapInterval && !storm::shim::kSwappyAutoPipeline,
                  "Swappy must not change the swap interval on its own");

    // The setting is a cap: never faster than it, also on modes it does not divide.
    expect_choice({hz(60)}, 60, 40, 60, 2, "40 fps on a 60 Hz panel: 30 fps, not 60");
    expect_choice({hz(60)}, 60, 45, 60, 2, "45 fps on a 60 Hz panel: 30 fps");
    expect_choice({hz(90)}, 90, 60, 90, 2, "60 fps on a 90 Hz-only panel: 45 fps");
    expect_choice(phone, 60, 40, 120, 3, "40 fps on 60/90/120 Hz: three 120 Hz refreshes");

    // The Xiaomi 15 Ultra at 60 Hz, 40 fps: it lists a 40 Hz mode and stays at 60 Hz.
    {
        const std::vector<uint64_t> xiaomi = {hz(60), hz(40), hz(90), hz(120)};
        const auto chosen = ChooseSwapInterval(xiaomi, hz(60), hz(40));
        expect(chosen.period == hz(40) && chosen.refreshes == 1, "40 fps: the 40 Hz mode, the slower exact one");
        expect(!DisplayInMode(chosen.period, hz(60)), "the display at 60 Hz is not in the chosen 40 Hz mode");
        expect(DisplayInMode(chosen.period, hz(40.3)) && DisplayInMode(hz(60), hz(59.94)),
               "within 2 % the display is in the mode");
        expect(!DisplayInMode(0, hz(60)) && !DisplayInMode(hz(60), 0), "an unknown period is no match");
        // Chosen again with the display's own period: 30 fps, two 60 Hz refreshes.
        const std::vector<uint64_t> stayed = {hz(60)};
        const auto again = ChooseSwapInterval(stayed, hz(60), hz(40));
        expect(again.period == hz(60) && again.refreshes == 2, "re-chosen at 60 Hz: two refreshes");
    }
    std::puts("PASS: frame overlap choice; frame pacing swap interval (60/90/120 Hz display mode sets, the interval of the "
              "setting, the setting as a cap, a display that stays out of the chosen mode, no automatic interval)");
    return 0;
}
