// The swap interval for a frame rate on a display with several refresh rates (frame_pacing.cpp; host-tested in
// tests/frame_pacing): a whole number of refreshes of one of the display's modes, never faster than the target frame
// time (the setting is a cap) and closest to it; on a tie the mode the display is in (no mode switch), then the slower
// mode (less power).
//
// The display may not enter the mode chosen (a 40 Hz mode a phone lists but keeps at 60 Hz): frame_pacing.cpp then
// chooses again with the period the display is in as the only mode (DisplayInMode).
//
// The interval is fixed: Swappy's auto swap interval (which paces slower than the setting while frames are late, and
// stops pacing past Swappy*_setMaxAutoSwapIntervalNS) and its auto pipeline mode (which switches the pipelining with
// the frame times) stay off. A frame that misses its slot is presented at the next refresh.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>

namespace storm::shim
{

inline constexpr bool kSwappyAutoSwapInterval = false;
inline constexpr bool kSwappyAutoPipeline = false;

struct SwapChoice
{
    uint64_t period = 0;    // ns, of the chosen display mode; 0: no usable period
    uint64_t refreshes = 0; // of that mode per frame
    uint64_t interval() const
    {
        return period * refreshes;
    }
};

// Refreshes of `period` for a frame of `target`: the fewest that are not faster than it, within 5 % of a refresh
// (a 59.94 Hz panel counts as 60 Hz).
inline uint64_t RefreshesFor(uint64_t period, uint64_t target)
{
    constexpr double kSlack = 0.05;
    return static_cast<uint64_t>(std::max(1.0, std::ceil(static_cast<double>(target) / period - kSlack)));
}

inline SwapChoice ChooseSwapInterval(std::span<const uint64_t> periods, uint64_t current, uint64_t target)
{
    constexpr double kTie = 1e5; // 0.1 ms
    SwapChoice best;
    double best_error = 0.0;
    for (const uint64_t period : periods)
    {
        if (period == 0)
            continue;
        const uint64_t refreshes = RefreshesFor(period, target);
        const double error = std::abs(static_cast<double>(refreshes * period) - static_cast<double>(target));
        const bool better = best.period == 0 || error < best_error - kTie ||
                            (error < best_error + kTie && best.period != current &&
                             (period == current || period > best.period));
        if (better)
        {
            best = {period, refreshes};
            best_error = error;
        }
    }
    return best;
}

// The display runs the chosen mode: its refresh period is the mode's within 2 %.
inline bool DisplayInMode(uint64_t chosen_period, uint64_t display_period)
{
    if (chosen_period == 0 || display_period == 0)
        return false;
    const double ratio = static_cast<double>(display_period) / static_cast<double>(chosen_period);
    return ratio > 0.98 && ratio < 1.02;
}

} // namespace storm::shim
