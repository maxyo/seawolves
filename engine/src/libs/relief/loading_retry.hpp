#pragma once
#include <atomic>
#include <cstdint>
namespace storm::relief
{
// One GPU texture in a place's loading window (the game draws its scene under the loading cover, device_choice.hpp),
// and none within kLoadingRetryGapMs of the last one, so the GPU work does not hold the loads up. The renderer may
// revoke it (`active`) from its thread while the worker decodes.
inline constexpr uint64_t kLoadingRetryGapMs = 60000;
struct LoadingRetry
{
    explicit LoadingRetry(uint64_t until) : deadline_ms(until)
    {
    }
    const uint64_t deadline_ms;
    std::atomic<bool> active{true}, used{false};
    bool take(uint64_t now, bool possible, uint64_t last_retry)
    {
        if (!active.load() || now >= deadline_ms || !possible || (last_retry && now - last_retry < kLoadingRetryGapMs))
            return false;
        bool expected = false;
        return used.compare_exchange_strong(expected, true) && active.load();
    }
};
} // namespace storm::relief
