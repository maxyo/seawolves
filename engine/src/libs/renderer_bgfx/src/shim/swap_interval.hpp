// The swap interval for a frame rate on a display with several refresh rates (frame_pacing.cpp; host-tested in
// tests/frame_pacing): a whole number of refreshes of one of the display's modes, never faster than the target frame
// time (the setting is a cap) and closest to it; on a tie the mode the display is in (no mode switch), then the slower
// mode (less power).
//
// The display may not enter the mode chosen (a 40 Hz mode a phone lists but keeps at 60 Hz): frame_pacing.cpp then
// chooses again with the period the display is in as the only mode (DisplayInMode).
//
// The interval is fixed: Swappy's auto swap interval and auto pipeline mode stay off (frame_pacing.cpp). A frame that
// misses its slot is presented at the next refresh; SwapIntervalWatch reports Swappy pacing at another interval.
#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <span>

namespace storm::shim
{

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
        const bool better =
            best.period == 0 || error < best_error - kTie ||
            (error < best_error + kTie && best.period != current && (period == current || period > best.period));
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

// The interval Swappy is expected to pace at. Swappy takes a new interval in at the end of the next present
// (SwappyCommon::onPostSwap); until then its getter reports the one before. A present under way while the game thread
// sets the interval may end with the old one, so only presents that began after the set are compared.
class SwapIntervalWatch
{
  public:
    using Clock = std::chrono::steady_clock;

    // `interval` was handed to Swappy; `at` is after the call returned.
    void set(std::chrono::nanoseconds interval, Clock::time_point at)
    {
        expected_ = interval;
        set_at_ = at;
    }

    // A present that began at `began` ended and Swappy reports `reported` at `refresh`. True once per change when it
    // paces at another interval than set; the reported one is expected from then on.
    bool unexpected(Clock::time_point began, std::chrono::nanoseconds reported, std::chrono::nanoseconds refresh)
    {
        if (expected_ == std::chrono::nanoseconds::zero() || began < set_at_)
            return false;
        // The set interval comes from the mode list, Swappy's from its refresh period: the same interval may differ
        // in the last nanoseconds, another one by a whole refresh.
        if (std::chrono::abs(reported - expected_) * 2 < refresh)
            return false;
        expected_ = reported;
        return true;
    }

    std::chrono::nanoseconds expected() const
    {
        return expected_;
    }

  private:
    std::chrono::nanoseconds expected_{0}; // zero: nothing set yet
    Clock::time_point set_at_{};
};

} // namespace storm::shim
