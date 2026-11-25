#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace storm::render
{
// The pace of the loading frames while relief or x2 inference waits for the GPU (loading_schedule.hpp): the
// game thread waits until twice the last frame's GPU time after the frame's start, so the loading frames leave the
// inference about half the GPU. At least one 60 Hz frame, at most 80 ms (the loading screen stays at 12.5 fps or
// more); a GPU time older than 250 ms paces nothing. Milliseconds on steady_clock.
class LoadingBudget
{
  public:
    static constexpr double kMinIntervalMs = 16, kMaxIntervalMs = 80;
    static constexpr uint64_t kStaleMs = 250;
    void frame_started(uint64_t now)
    {
        start_ = now;
        have_start_ = true;
    }
    uint64_t deadline(uint64_t now, bool eligible, double gpu_ms, uint64_t measured_at) const
    {
        if (!eligible || !have_start_ || now < start_ || now - start_ >= kMaxIntervalMs || !measured_at ||
            now < measured_at || now - measured_at > kStaleMs || !std::isfinite(gpu_ms) || gpu_ms <= 0)
            return now;
        const uint64_t interval = static_cast<uint64_t>(std::clamp(2.0 * gpu_ms, kMinIntervalMs, kMaxIntervalMs));
        return std::max(now, start_ + interval);
    }

  private:
    uint64_t start_ = 0;
    bool have_start_ = false;
};
} // namespace storm::render
