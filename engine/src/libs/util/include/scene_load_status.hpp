// Plain scene preparation status shared by renderer and platform UI. No renderer backend dependencies.
#pragma once

#include <cstdint>
#include <string>

namespace storm::render
{
struct SceneLoadStatus
{
    uint64_t generation = 0;
    bool active = false;
    std::string state;  // "inactive", "preparing", "error" (a mandatory resource failed), "ready"
    uint64_t owner = 0; // the entity whose scene this is (BeginScenePipelines)
};
SceneLoadStatus GetSceneLoadStatus();
void CompleteSceneReveal();
// The scene's owner was deleted before its reveal (a location whose load failed): reported as an ERROR, the load ends
// unrevealed and the game goes on without it.
void AbandonSceneLoad(uint64_t generation);
// The loading transaction of `generation` is suspended (the app in the background): the background work gives up its
// loading share until a cover is presented again, and the reveal's clocks stop.
void SuspendSceneLoad(uint64_t generation);
void EndSceneCpuBudget(uint64_t generation);
} // namespace storm::render
