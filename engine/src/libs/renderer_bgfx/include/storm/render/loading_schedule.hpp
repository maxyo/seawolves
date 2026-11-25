#pragma once
#include "platform/host_hooks.hpp"
#include "storm/render/loading_budget.hpp"
#include "storm/render/pipelines.hpp"

#include <algorithm>
#include <cstdint>
namespace storm::render
{
// The engine's loading schedule (the main loop's and the stock loader's storm::platform::PumpLoadingSchedule): opens
// the renderer's loading window for the load being prepared and returns how long the game thread waits (ms) before
// its next poll.
class LoadingSchedule
{
  public:
    unsigned operator()(uint64_t generation, platform::LoadingSchedulePhase phase, uint64_t now)
    {
        using Phase = platform::LoadingSchedulePhase;
        if (phase == Phase::Suspended || phase == Phase::Ended)
        {
            if (generation == generation_)
                reset(0);
            return 0;
        }
        const auto state = GetSceneLoadStatus();
        if (!state.active)
        {
            if (generation_)
                reset(0);
            return 0;
        }
        if (state.generation != generation)
            return 0;
        if (generation_ != generation)
            reset(generation);
        if (phase == Phase::FrameStarted)
        {
            SetSceneLoadingWindow(0, 0);
            budget_.frame_started(now);
            return 0;
        }
        // The deadline requires the renderer's actual matching opaque Present,
        // selected required work and fresh GPU timing; a stock request is insufficient.
        const auto budget = GetSceneLoadingBudget();
        const auto until = budget_.deadline(now, budget.eligible && budget.generation == generation, budget.gpu_ms,
                                            budget.measured_at_ms);
        SetSceneLoadingWindow(until > now ? generation : 0, until > now ? until : 0);
        return phase == Phase::Poll && until > now ? static_cast<unsigned>(std::min<uint64_t>(2, until - now)) : 0;
    }

    // The load being paced; 0 for none.
    uint64_t generation() const
    {
        return generation_;
    }

  private:
    void reset(uint64_t generation)
    {
        SetSceneLoadingWindow(0, 0);
        budget_ = {};
        generation_ = generation;
    }

    LoadingBudget budget_;
    uint64_t generation_ = 0;
};
} // namespace storm::render
