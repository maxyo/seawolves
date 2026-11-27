// The scene load state core asks the renderer for: no renderer here, so no scene ever loads.
#include "scene_load_status.hpp"

namespace storm::render
{
SceneLoadStatus GetSceneLoadStatus()
{
    return {};
}
void CompleteSceneReveal()
{
}
void AbandonSceneLoad(uint64_t)
{
}
void SuspendSceneLoad(uint64_t)
{
}
void EndSceneCpuBudget(uint64_t)
{
}
} // namespace storm::render
