// The reveal's glue: samples the producers for SceneReveal and forwards the engine's and the shim's events to it.
#include "effects/scene_reveal.hpp"

#include "effects/effects.hpp"
#include "effects/layer_arrays.hpp"
#include "effects/loading_upload.hpp"
#include "effects/relief_generator.hpp"
#include "effects/upscale.hpp"
#include "scene_load_status.hpp"
#include "shim/shim.hpp"
#include "storm/fx/cpu_admission.hpp"
#include "storm/fx/normal_map.hpp"
#include "storm/render/pipelines.hpp"
#include "texture/core/service.hpp"

#include <chrono>

namespace storm::effects
{
namespace
{
struct Load
{
    SceneReveal reveal;
    std::unordered_map<uint64_t, std::weak_ptr<shim::TexStore>> textures;
    // Scene textures that failed, each logged once as an ERROR; they never hold the reveal.
    std::unordered_set<std::string> failed_textures;
    std::unordered_set<uint64_t> fallback_textures;
    SceneSample sample;
    std::vector<std::shared_ptr<shim::TexStore>> held; // the stores of `sample`, while it is taken
    // The loading window (render::SetSceneLoadingWindow): the GPU time of the last frame and the entries a worker
    // may run on while the game thread waits.
    uint64_t timing_at = 0;
    double loading_gpu_ms = 0;
    int64_t timing_end = 0;
    uint64_t window_deadline = 0, window_generation = 0;
    std::vector<std::shared_ptr<NormalMapEntry>> loading_normal;
    std::vector<std::shared_ptr<UpscaleEntry>> loading_upscale;
};
Load &load()
{
    static Load instance;
    return instance;
}
uint64_t now_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
// The scenes that load behind the loading picture: the stock loading transaction (ReloadProgressEnd) covers on-land
// loading, SEA_AI's sea scene, the world map (wdmCreateWorldMap) and the title's scene (Main_Start).
bool RevealedScene(std::string_view scene)
{
    return scene.starts_with("location/") || scene == "sea" || scene == "worldmap" || scene == "mainmenu";
}
bool SceneTextureName(std::string_view normalized)
{
    return !normalized.empty() && !normalized.starts_with("interfaces/") && !normalized.starts_with("loading/");
}
void EndLoad()
{
    load().reveal.end();
    fx::SharedCpuAdmission().begin(0, false);
    texture::Service::instance().end_loading_cache_scope();
}
ShadowState Shadow(int state)
{
    return static_cast<ShadowState>(state);
}

// The scene's textures and the place's products, into `l.sample`. The stores stay held in `l.held` until Release, so
// the names the sample shows stay valid.
SceneSample &Sample(ShadowState sun, ShadowState lamps)
{
    auto &l = load();
    Host &host = Host::instance();
    SceneSample &s = l.sample;
    s.textures.clear();
    s.products = {};
    s.switching = host.switching();
    s.sun = sun;
    s.lamps = lamps;
    l.loading_normal.clear();
    l.loading_upscale.clear();
    const fx::BackgroundProducts loading_products = fx::BackgroundProductsFor(host.settings());
    const uint64_t generation = l.reveal.generation();
    const auto state = [](const auto &entry) {
        return entry ? uint8_t(1 + entry->state.load(std::memory_order_acquire)) : uint8_t(0);
    };
    for (auto it = l.textures.begin(); it != l.textures.end();)
    {
        auto store = it->second.lock();
        if (!store)
        { // no scene owner remains; a dead resource is no longer required
            it = l.textures.erase(it);
            l.reveal.restart_frames();
            continue;
        }
        ++it;
        store->sync(); // pumps completed transcodes, including textures outside the initial camera
        // A texture with a handle draws (its fallback while the transcode runs, or the previous upload while new
        // contents wait for the next sync): only one without any GPU copy yet holds the reveal.
        const bool drawable = bgfx::isValid(store->handle);
        const bool working = store->fallback_bytes || store->transcode_job || store->any_dirty;
        if (drawable && working)
            s.products.add(SceneProduct::Transcode);
        // Failed when no work is left: no GPU copy, or a finished transcode without its full mip chain.
        if (!working && (!drawable || (store->transcoded && !store->full_chain)) &&
            l.fallback_textures.insert(store->serial).second)
            render::Log().error(
                "location readiness: {}: texture {} failed ({}); drawn with {}, the reveal does not wait for it",
                l.reveal.scene(), store->name, drawable ? "transcode without its full mip chain" : "no GPU copy",
                drawable ? "the levels it has" : "the missing-texture stand-in");
        // The x2 and the relief/gloss maps are prepared with loading priority; only the x2 the residency plan chose
        // (near the camera) counts, or any while the first plan is not made.
        const int upscale = upscales().prepare_scene(*store, generation);
        bool relief_inference = false;
        const int relief = host.lighting().prepare_scene_texture(*store, &relief_inference);
        if (relief == 1)
            s.products.add(SceneProduct::Relief);
        if (upscale == 1 && store->upscale && (store->upscale->selected || !upscales().planned()))
            s.products.add(SceneProduct::Upscale);
        // The entries a worker may run on in the loading window: the ones NormalMaps::prepare_scene waits for.
        if (relief == 1 && relief_inference)
        {
            if (store->normal_map && store->normal_map->state == NormalMapEntry::Waiting &&
                (loading_products.relief ||
                 (loading_products.gloss && store->normal_map->material_gloss != fx::GlossClass::None)))
                l.loading_normal.push_back(store->normal_map);
            if (loading_products.gloss && store->gloss_map && store->gloss_map->state == NormalMapEntry::Waiting)
                l.loading_normal.push_back(store->gloss_map);
        }
        if (upscale == 1 && store->upscale && store->upscale->state == UpscaleEntry::Waiting)
            l.loading_upscale.push_back(store->upscale);
        s.textures.push_back({.serial = store->serial,
                              .name = store->name,
                              .drawable = drawable,
                              .fallback = store->fallback_bytes != 0,
                              .transcoding = bool(store->transcode_job),
                              .dirty = store->any_dirty,
                              .full_chain = store->full_chain,
                              .relief = state(store->normal_map),
                              .gloss = state(store->gloss_map),
                              .upscale = state(store->upscale)});
        l.held.push_back(std::move(store));
    }
    // The place's probes and the town light: made only when the preset wants them.
    if (host.place().state() == PlaceBake::State::Building)
        s.products.add(SceneProduct::Place);
    if (host.lighting().town_light().preparing())
        s.products.add(SceneProduct::TownLight);
    s.products.add(SceneProduct::Layers, LayerArraysPending());
    return s;
}
// The sample has been taken: its names go with the stores.
void Release()
{
    load().sample.textures.clear();
    load().held.clear();
}
} // namespace

SceneReveal &CurrentSceneReveal()
{
    return load().reveal;
}

void BeginSceneResources(const char *scene_name, uint64_t owner)
{
    auto &l = load();
    const std::string scene = scene_name ? scene_name : "";
    texture::Service::instance().end_loading_cache_scope();
    render::SetSceneLoadingWindow(0, 0);
    fx::SharedCpuAdmission().suspend();
    l.timing_at = 0;
    l.loading_normal.clear();
    l.loading_upscale.clear();
    // The previous scene's relief requests are dropped: this scene's draws ask for the maps they read.
    Host::instance().lighting().normal_maps().begin_scene(scene.c_str());
    l.textures.clear();
    l.failed_textures.clear();
    l.fallback_textures.clear();
    if (!RevealedScene(scene))
    {
        l.reveal.invalidate();
        l.reveal.end();
        fx::SharedCpuAdmission().begin(0, false);
        return;
    }
    texture::Service::instance().begin_loading_cache_scope();
    const uint64_t generation = l.reveal.begin(scene, owner, now_ms());
    fx::SharedCpuAdmission().begin(generation, true);
    upscales().begin_scene();
    render::Log().info("location readiness: begin {} generation={}", scene, generation);
}

void ObserveSceneTexture(const std::shared_ptr<shim::TexStore> &texture)
{
    auto &l = load();
    if (!l.reveal.preparing() || !texture || texture->render_target || texture->system_memory ||
        !SceneTextureName(texture->name))
        return;
    if (l.textures.emplace(texture->serial, texture).second)
        l.reveal.demand();
}

bool ScenePreparing()
{
    return load().reveal.preparing();
}

bool HoldSceneResources(bool pipelines)
{
    auto &l = load();
    if (!l.reveal.preparing())
        return false;
    Host &host = Host::instance();
    const bool hold = l.reveal.hold(
        now_ms(),
        Sample(Shadow(host.lighting().shadows().initial_state(!host.lighting().lamp_shadows().interior())),
               Shadow(host.lighting().lamp_shadows().initial_state())),
        pipelines);
    Release();
    return hold;
}

bool SceneRevealExpired()
{
    return load().reveal.state() == SceneReveal::State::Expired;
}

void SampleSceneFrame(bool scene_frame, uint32_t frame, ShadowState sun, ShadowState lamps)
{
    auto &l = load();
    // The last frame's GPU time, for the loading window. No profiling is switched on for it: without timer queries
    // the loading frames run unpaced.
    l.timing_at = 0;
    l.loading_gpu_ms = 0;
    if (const auto *timing = bgfx::getStats(); timing && timing->gpuTimerFreq > 0 &&
                                               timing->gpuTimeEnd > timing->gpuTimeBegin &&
                                               timing->gpuTimeEnd != l.timing_end)
    {
        l.timing_end = timing->gpuTimeEnd;
        l.loading_gpu_ms = double(timing->gpuTimeEnd - timing->gpuTimeBegin) * 1000.0 / timing->gpuTimerFreq;
        l.timing_at = now_ms();
    }
    if (l.reveal.preparing())
    {
        l.reveal.sample(now_ms(), Sample(sun, lamps), scene_frame, frame);
        Release();
    }
    l.reveal.end_frame();
}

LoadingUploadScope SceneUploadScope()
{
    const SceneReveal &reveal = load().reveal;
    return {reveal.generation(), reveal.state() == SceneReveal::State::Preparing && reveal.cover_presented()};
}

void BeginSceneFrame(uint64_t frame, uint64_t surface, uint64_t target, bool stable_target)
{
    load().reveal.begin_frame(frame, surface, target, stable_target);
}
bool HideScenePicture(uint64_t surface, uint64_t target)
{
    return load().reveal.hide_picture(surface, target);
}
void ScenePictureTarget(uint64_t surface, uint64_t target)
{
    load().reveal.check_target(surface, target);
}
void ScenePictureSubmitted()
{
    load().reveal.picture_submitted();
}
void ScenePostTarget(uint64_t surface, uint64_t before, uint64_t after)
{
    load().reveal.post_target(surface, before, after);
}
void SceneCoverDrawSubmitted(bool full_coverage, bool blend_add)
{
    load().reveal.cover_draw_submitted(full_coverage, blend_add);
}
void SceneFramePresented(bool advanced, bool stable)
{
    auto &l = load();
    l.reveal.presented(advanced, stable);
    fx::SharedCpuAdmission().opaque(l.reveal.generation(), l.reveal.cover_presented());
    if (!l.reveal.cover_presented())
        render::SetSceneLoadingWindow(0, 0);
}
void InvalidateSceneFrames()
{
    render::SetSceneLoadingWindow(0, 0);
    fx::SharedCpuAdmission().suspend();
    load().reveal.invalidate();
    load().timing_at = 0;
}
} // namespace storm::effects

namespace storm::render
{
SceneLoadStatus GetSceneLoadStatus()
{
    const auto &r = effects::load().reveal;
    using State = effects::SceneReveal::State;
    return {r.generation(), r.preparing(),
            !r.preparing()                             ? "inactive"
            : r.state() == State::Failed || r.failed() ? "error"
            : r.state() == State::Ready                ? "ready"
                                                       : "preparing",
            r.owner()};
}

void ArmSceneCover(bool armed)
{
    effects::load().reveal.arm_cover(armed);
}

void EndSceneCpuBudget(uint64_t generation)
{
    fx::SharedCpuAdmission().end(generation);
}
void SuspendSceneLoad(uint64_t generation)
{
    fx::SharedCpuAdmission().suspend(generation);
    auto &r = effects::load().reveal;
    if (generation == r.generation())
        r.pause(effects::now_ms());
}

SceneLoadingBudget GetSceneLoadingBudget()
{
    const auto &l = effects::load();
    const auto state = GetSceneLoadStatus();
    return {state.generation, l.timing_at, l.loading_gpu_ms,
            state.active && state.state == "preparing" && l.reveal.cover_presented() &&
                (!l.loading_normal.empty() || !l.loading_upscale.empty())};
}
void SetSceneLoadingWindow(uint64_t generation, uint64_t deadline_ms)
{
    auto &l = effects::load();
    const auto budget = GetSceneLoadingBudget();
    if (!budget.eligible || generation != budget.generation)
        deadline_ms = 0;
    if (l.window_generation == generation && l.window_deadline == deadline_ms)
        return;
    l.window_generation = generation;
    l.window_deadline = deadline_ms;
    effects::ReliefGenerator::instance().loading_window(
        generation, deadline_ms,
        deadline_ms ? l.loading_normal : std::vector<std::shared_ptr<effects::NormalMapEntry>>{},
        deadline_ms ? l.loading_upscale : std::vector<std::shared_ptr<effects::UpscaleEntry>>{});
}

void RequireSceneTexture(shim::BaseTexture *texture)
{
    effects::ObserveSceneTexture(shim::shared_store_of(texture));
}

namespace
{
// `texture`: a file name (no '/') matches in any folder; a path (normalized, fx::NormalizeTexturePath) matches only
// that folder, for a name the game ships elsewhere.
struct KnownMissing
{
    std::string_view texture;
    const char *named_by;
};
const KnownMissing *FindKnownMissing(const std::string &normalized)
{
    // TEHO 1.7.3, checked against RESOURCE of the installer.
    static constexpr KnownMissing kKnown[] = {
        // Material of the masts, in no folder: the original engine drew them without it too.
        {"phong256_bw.gif", "RESOURCE/MODELS/Ships/Sloop1/sloop1_mast2.gm, sloop1_mast201.gm and "
                            "RESOURCE/MODELS/Ships/Frigate_h1/Frigate_h1.gm"},
        // Third material of a blade: checker.tga is shipped in Textures/, Characters/ and MainMenu/, not in Ammo/.
        {"ammo/checker", "RESOURCE/MODELS/Ammo/blade_29.gm"},
        // A material of the store's locator model: Textures/ ships A_big22.tga only.
        {"a_big", "RESOURCE/MODELS/Locations/Inside/StoreSmall/storeSmall_locators.gm"},
        // Materials named by these models and shipped in no folder (found by the all-scenes sweep).
        {"phong256.gif", "RESOURCE/MODELS/Ships/Frigate_h1/Frigate_h1.gm"},
        {"capmd_2_dm", "RESOURCE/MODELS/Locations/Decks/capmd_2/capmd_2.gm"},
        {"groundrock2", "RESOURCE/MODELS/Locations/Outside/Shores/MountainPathDMChest/MountainPathDMChest_sb.gm"},
        {"hut_church_window", "RESOURCE/MODELS/Locations/Inside/church_IM/church_IM.gm"},
        {"jungle_land_09", "RESOURCE/MODELS/Locations/Outside/Jungles/TownExitM/DouExit_dno.gm"},
        {"kanion_leaf", "RESOURCE/MODELS/Locations/Outside/Canyon/canyon_locators.gm"},
        {"location_06_1", "RESOURCE/MODELS/Locations/Inside/SmallResidence/SmallResidence_locators.gm and "
                          "RESOURCE/MODELS/Locations/Inside/pirateresidence/ResP_l.gm"},
        {"new_mountain", "RESOURCE/MODELS/Locations/Outside/Shores/Shore13/plan3.gm"},
        {"rsd_1", "RESOURCE/MODELS/Locations/Inside/BigHouse01/BigHouse01_locators.gm and "
                  "RESOURCE/MODELS/Locations/Inside/BigHouse01_2Door/BigHouse01_2Door_locators.gm"},
        {"sh03_conenvday", "RESOURCE/MODELS/Locations/Inside/sklad/sklad.gm"},
        {"sh03_envday", "RESOURCE/MODELS/Locations/Inside/SmallResidence/SmallResidence.gm and "
                        "RESOURCE/MODELS/Locations/Inside/Pirate_House/PirH_w.gm"},
        {"treasuregoldb", "RESOURCE/MODELS/Locations/Inside/DungeonOld2/Treasures.gm"},
        {"treasuregoldc", "RESOURCE/MODELS/Locations/Inside/DungeonOld2/Treasures.gm"},
        {"treasuregoldd", "RESOURCE/MODELS/Locations/Inside/DungeonOld2/Treasures.gm"},
        {"waterblend", "RESOURCE/MODELS/Locations/Outside/Jungles/TownExitM/Waterfall.gm"},
        // Shipped in Textures/Islands/Guadeloupe/ and Textures/MainMenu/Russian/, not in the location's folder.
        {"guadeloupemask", "RESOURCE/MODELS/Locations/Outside/pyramid1/pyramid1.gm"},
        // The debris model is loaded with the texture path Tornado/; Items.tga is shipped in Textures/ only.
        {"tornado/items", "RESOURCE/MODELS/Tornado/palka03.gm"},
        // Loading pictures of two unreachable locations (PROGRAM/locations/init/QuestLocations.c).
        {"loading/farmfield", "PROGRAM/locations/init/QuestLocations.c (Clone_location)"},
        {"loading/art/open_sea_clear", "PROGRAM/locations/init/QuestLocations.c (Ship_deck)"},
        // Lightmaps of these models: their folders are missing under Textures/lighting/ in every weather ("*").
        {"lighting/*/locations/decks/capquest/shadow", "RESOURCE/MODELS/Locations/Decks/capquest/capquest.gm"},
        {"lighting/*/locations/decks/deck01/shadow", "RESOURCE/MODELS/Locations/Decks/deck01/deck01.gm"},
        {"lighting/*/locations/decks/deck2/shadow", "RESOURCE/MODELS/Locations/Decks/deck2/deck2.gm"},
        {"lighting/*/locations/inside/fort_1/shadow", "RESOURCE/MODELS/Locations/Inside/fort_1/fort1.gm"},
        {"lighting/*/locations/inside/fort_2/shadow", "RESOURCE/MODELS/Locations/Inside/fort_2/fort2.gm"},
        {"lighting/*/locations/inside/fort_3/shadow", "RESOURCE/MODELS/Locations/Inside/fort_3/fort3.gm"},
        {"lighting/*/locations/outside/jungles/terrain/shadow",
         "RESOURCE/MODELS/Locations/Outside/Jungles/Terrain/Terrain.gm"},
    };
    const size_t slash = normalized.rfind('/');
    const std::string_view file =
        slash == std::string::npos ? std::string_view(normalized) : std::string_view(normalized).substr(slash + 1);
    // An entry without a folder matches the file in any folder; "*" stands for one folder (the weather).
    const auto matches = [&](std::string_view texture) {
        if (texture.find('/') == std::string_view::npos)
            return file == texture;
        const size_t star = texture.find('*');
        if (star == std::string_view::npos)
            return std::string_view(normalized) == texture;
        const std::string_view head = texture.substr(0, star), tail = texture.substr(star + 1);
        const std::string_view path(normalized);
        return path.size() > head.size() + tail.size() && path.starts_with(head) && path.ends_with(tail) &&
               path.substr(head.size(), path.size() - head.size() - tail.size()).find('/') == std::string_view::npos;
    };
    for (const KnownMissing &known : kKnown)
        if (matches(known.texture))
            return &known;
    return nullptr;
}
} // namespace
const char *KnownMissingGameTexture(const char *name)
{
    const KnownMissing *known = FindKnownMissing(fx::NormalizeTexturePath(name ? name : ""));
    return known ? known->named_by : nullptr;
}
void FailSceneTexture(const char *name, const char *requester)
{
    if (const KnownMissing *known = FindKnownMissing(fx::NormalizeTexturePath(name ? name : "")))
    {
        // The same in every session and every scene: one line per texture per session, wherever it is first needed.
        static std::unordered_set<std::string_view> reported;
        if (reported.emplace(known->texture).second)
            render::Log().warn("known game data defect: texture '{}' requested by {} is not shipped by the game (named "
                               "by {}); a base texture draws the missing-texture stand-in",
                               known->texture, requester && *requester ? requester : "?", known->named_by);
        return;
    }
    auto &l = effects::load();
    const std::string normalized = fx::NormalizeTexturePath(name ? name : "");
    // It will never get a GPU copy: a base texture draws the missing-texture stand-in (GEOM, storm/render/
    // missing_texture.hpp), the other stages sample nothing; the reveal does not wait for it.
    if (l.reveal.preparing() && effects::SceneTextureName(normalized) && l.failed_textures.emplace(normalized).second)
        render::Log().error("location readiness: {}: texture '{}' requested by {} could not be created; drawn with the "
                            "missing-texture stand-in where it is a base texture, the reveal does not wait for it",
                            l.reveal.scene(), normalized, requester && *requester ? requester : "?");
}

void CompleteSceneReveal()
{
    auto &l = effects::load();
    if (l.reveal.complete())
        effects::EndLoad();
}
void AbandonSceneLoad(uint64_t generation)
{
    auto &l = effects::load();
    if (!l.reveal.preparing() || generation != l.reveal.generation())
        return;
    render::Log().error("location readiness: {} was unloaded before its reveal (its load failed)", l.reveal.scene());
    effects::EndLoad();
}
} // namespace storm::render
