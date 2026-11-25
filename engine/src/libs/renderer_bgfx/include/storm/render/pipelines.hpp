// A scene loaded behind the loading picture (src/effects/scene_reveal.hpp) and its Vulkan pipelines
// (src/shim/pipeline_sets.hpp): every pipeline the device creates is recorded under the scene that was loading or
// shown then; when that scene loads again its recorded pipelines are created while it loads, and the reveal waits for
// them. Nothing of the pipelines on the other backends.
#pragma once

#include "scene_load_status.hpp"

namespace storm::shim
{
struct BaseTexture;
}

namespace storm::render
{

// Game thread, when a scene starts loading ("location/<id>", "sea", "worldmap", "mainmenu"): from now on created
// pipelines are recorded under `scene`, and the ones recorded under it in earlier runs are created ahead. `owner`: the
// entity whose scene it is (0 for none); its deletion before the reveal abandons the load (AbandonSceneLoad).
void BeginScenePipelines(const char *scene, uint64_t owner);

// Game thread, before each frame of the loading transaction is drawn (Renderer::RunEnd): true while the scene begun
// last still waits for its pipelines or resources. A location is held until it is ready (Cancel on the loading
// screen); the title at most kTitleRevealBudgetMs; a scene of another name only for its pipelines, at most 5 s.
bool HoldSceneReveal();

// The loading cover: the full-screen layer of the loading picture is about to be drawn (true) / was drawn (false).
void ArmSceneCover(bool armed);
struct SceneLoadingBudget
{
    uint64_t generation = 0, measured_at_ms = 0;
    double gpu_ms = 0;
    bool eligible = false;
};
SceneLoadingBudget GetSceneLoadingBudget();
void SetSceneLoadingWindow(uint64_t generation, uint64_t deadline_ms);
// A world texture the scene draws (Renderer::TextureCreate, including cache hits): the reveal waits for its upload.
// A model's texture that cannot be created (`requester`: the model) never holds the reveal: an ERROR, or once per
// session a WARNING for one the game's data names but does not ship (KnownMissingGameTexture).
void RequireSceneTexture(shim::BaseTexture *texture);
void FailSceneTexture(const char *name, const char *requester);
// A texture the game's own models name but the game does not ship, in any folder or archive: a defect of its data, the
// same in every session. Returns where the game names it, nullptr for any other texture (a missing one of those is a
// real failure). Matched with or without "resource\textures\" and ".tx", by file name in any folder, or by folder and
// file name for a name the game ships in other folders.
const char *KnownMissingGameTexture(const char *name);

} // namespace storm::render
