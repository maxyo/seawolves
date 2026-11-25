// The stock loading transaction of CoreImpl (PrepareLoadingScene and its parts): templates over the core so that
// tests/scene_readiness_check runs them with a core of its own. A friend of CoreImpl.
#pragma once

#include "entity.h"
#include "loading_transaction.hpp"
#include "platform/host_hooks.hpp"
#include "scene_load_status.hpp"
#include "service.h"
#include "timer.h"

#include <SDL2/SDL_timer.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace storm::loading
{
struct SceneTransaction
{
    // AbortLoadingTransaction: logs the outcome, unwinds the running scripts and requests the engine exit.
    template <class C> static bool abort(C &self, storm::loading::Outcome outcome, uint64_t generation)
    {
        using storm::loading::Outcome;
        const bool user = outcome == Outcome::Cancel || outcome == Outcome::Quit;
        self.Trace("%sStock loading transaction aborted generation=%llu outcome=%s: the engine exits",
                   user ? "" : "ERROR: ", static_cast<unsigned long long>(generation),
                   storm::loading::OutcomeName(outcome));
        // No exception crosses the script VM: the running scripts (the stock loader and whatever called it) return
        // at once, and the frame ends with the ordinary engine exit.
        if (self.Compiler)
            self.Compiler->AbortExecution();
        self.Exit_flag = true;
        return false;
    }

    // DeferFrozenScene: the scene cannot draw yet (its owner only in realize layers the scripts froze); logged once per
    // load.
    template <class C> static bool defer_frozen(C &self, const storm::render::SceneLoadStatus &status)
    {
        if (!status.active || !status.owner || !self.IsEntityValid(status.owner))
            return false;
        const auto layers = self.entity_manager_.GetRealizeLayers(status.owner);
        if (layers.in == 0 || layers.frozen != layers.in)
            return false;
        if (self.deferred_scene_generation_ != status.generation)
        {
            // Stock OnLoad of a world-map save made at sea: wdmCreateWorldMap's own ReloadProgressEnd runs while
            // REALIZE is still frozen by the sea (CreateSeaEnvironment); OnLoad's EngineLayersOffOn lets it draw just
            // after, and its final ReloadProgressEnd prepares the map. Waiting here instead held the loading screen for
            // good.
            std::string frozen;
            for (uint32_t index = 0; index < 32; ++index)
                if (layers.frozen >> index & 1)
                    frozen += (frozen.empty() ? "" : ", ") + std::to_string(index);
            self.Trace(
                "Stock loading transaction deferred generation=%llu: the scene's owner is only in realize layers the "
                "scripts froze (%s); prepared once they let it draw",
                static_cast<unsigned long long>(status.generation), frozen.c_str());
            self.deferred_scene_generation_ = status.generation;
        }
        return true;
    }

    // LoadingSceneDeferred: the scene being prepared still waits for the scripts to let it draw.
    template <class C> static bool deferred(const C &self)
    {
        const auto status = storm::render::GetSceneLoadStatus();
        if (!status.active || !status.owner || !self.IsEntityValid(status.owner))
            return false;
        const auto layers = self.entity_manager_.GetRealizeLayers(status.owner);
        return layers.in != 0 && layers.frozen == layers.in;
    }

    // PrepareLoadingScene: the stock loading transaction. True: the scene is ready, deferred or abandoned; false:
    // aborted.
    template <class C> static bool prepare(C &self)
    {
        const auto initial = storm::render::GetSceneLoadStatus();
        if (!initial.active)
            return true;
        if (storm::loading::active)
            return abort(self, storm::loading::Outcome::Reentrant, initial.generation);
        if (defer_frozen(self, initial))
            return true;
        storm::loading::Scope loading;
        struct CpuAdmissionReset
        {
            uint64_t generation;
            ~CpuAdmissionReset()
            {
                storm::render::EndSceneCpuBudget(generation);
            }
        } cpu_admission_reset{initial.generation};
        const auto loading_started = std::chrono::steady_clock::now();
        uint64_t loading_frames = 0;
        self.Trace("Stock loading transaction enter generation=%llu",
                   static_cast<unsigned long long>(initial.generation));
        storm::platform::LoadingPlatformTransaction platform;
        struct LoadingScheduleReset
        {
            uint64_t generation;
            ~LoadingScheduleReset()
            {
                storm::platform::PumpLoadingSchedule(generation, storm::platform::LoadingSchedulePhase::Ended);
            }
        } schedule_reset{initial.generation};
        struct TimerBaseline
        {
            TIMER &timer;
            ~TimerBaseline()
            {
                timer.Current = timer.Previous = std::chrono::high_resolution_clock::now();
                timer.Carry = 0;
            }
        } baseline{self.Timer};
        // GetService stores SERVICE pointers; avoid pulling renderer backend headers into core.
        auto *renderer = static_cast<SERVICE *>(self.GetService("renderer"));
        auto *animation = static_cast<SERVICE *>(self.GetService("AnimationServiceImp"));
        if (!renderer)
            return abort(self, storm::loading::Outcome::Unavailable, initial.generation);
        while (true)
        {
            const auto status = storm::render::GetSceneLoadStatus();
            if (!status.active || status.generation != initial.generation)
                return abort(self, storm::loading::Outcome::Invalidated, initial.generation);
            if (status.owner && !self.IsEntityValid(status.owner))
            {
                storm::render::AbandonSceneLoad(initial.generation);
                self.Trace("ERROR: Stock loading transaction abandoned generation=%llu: the scene's owner was deleted "
                           "before its "
                           "reveal (its load failed); the game goes on",
                           static_cast<unsigned long long>(initial.generation));
                return true;
            }
            const auto host = storm::platform::PumpLoadingPlatform(initial.generation);
            if (host == storm::platform::LoadingPlatformState::Suspended)
            {
                storm::render::SuspendSceneLoad(initial.generation);
                storm::platform::PumpLoadingSchedule(initial.generation,
                                                     storm::platform::LoadingSchedulePhase::Suspended);
                SDL_Delay(2);
                continue;
            }
            if (host != storm::platform::LoadingPlatformState::Continue)
                return abort(self,
                             host == storm::platform::LoadingPlatformState::Cancel ? storm::loading::Outcome::Cancel
                             : host == storm::platform::LoadingPlatformState::Quit
                                 ? storm::loading::Outcome::Quit
                                 : storm::loading::Outcome::Unavailable,
                             initial.generation);
            const auto wait =
                storm::platform::PumpLoadingSchedule(initial.generation, storm::platform::LoadingSchedulePhase::Poll);
            if (wait)
            {
                // The next iteration pumps lifecycle/cancel again. No fake render
                // observation or ordinary frame is earned while the worker uses its window.
                SDL_Delay(std::min(wait, 2u));
                continue;
            }
            storm::platform::PumpLoadingSchedule(initial.generation,
                                                 storm::platform::LoadingSchedulePhase::FrameStarted);
            renderer->RunStart();
            // Animation service explicitly builds the already-selected pose in this context.
            if (animation)
                animation->RunStart();
            // Preparation is opt-in and unique across all loaded entities: WEATHER
            // can be execute-only, and cameras need their configured transform before
            // drawing. Never call ordinary Execute or change any layer's frozen flag.
            std::vector<entid_t> prepare_ids;
            self.ForEachEntity([&](entptr_t entity) { prepare_ids.push_back(entity->GetId()); });
            for (auto id : prepare_ids)
                if (auto *entity = self.GetEntityPointerSafe(id))
                    entity->ProcessStage(Entity::Stage::loading_prepare, 0);
            // Render only the original eligible realize layers (including SEA_REALIZE).
            // Copy the shared scratch vector before any consumer can reuse it.
            const auto ids = self.GetEntityIds(layer_type_t::realize);
            for (auto id : ids)
                if (auto *entity = self.GetEntityPointerSafe(id))
                    entity->ProcessStage(Entity::Stage::loading_realize, 0);
            renderer->RunEnd(); // stock artwork, actual Present, upload/shadow/resource pumps
            storm::platform::PumpLoadingSchedule(initial.generation,
                                                 storm::platform::LoadingSchedulePhase::FramePresented);
            ++loading_frames;
            storm::loading::first_frame = false;
            if (storm::loading::ready)
            {
                storm::render::CompleteSceneReveal();
                if (!storm::render::GetSceneLoadStatus().active)
                {
                    self.Trace("Stock loading transaction ready-exit generation=%llu frames=%llu elapsed-ms=%lld",
                               static_cast<unsigned long long>(initial.generation),
                               static_cast<unsigned long long>(loading_frames),
                               static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                          std::chrono::steady_clock::now() - loading_started)
                                                          .count()));
                    return true;
                }
            }
        }
    }
};
} // namespace storm::loading
