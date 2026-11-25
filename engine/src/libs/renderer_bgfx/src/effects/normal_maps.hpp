// Normal, material and gloss maps of the game's textures for the per-pixel lighting's relief (FX_LIGHT_NORMALMAP,
// fx/normal_map.hpp, fx/relief_store.hpp). Design: renderer_bgfx/README.md, "Relief maps".
#pragma once

#include "effects/generation_priority.hpp"

#include "storm/fx/gloss_map.hpp"
#include "storm/fx/normal_map.hpp"
#include "storm/fx/parallax_exclusions.hpp"

#include <bgfx/bgfx.h>

#include "storm_render_types.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace storm::fx
{
class Lighting;
}
namespace storm::texture
{
struct CacheTask;
}
namespace storm::shim
{
struct TexStore;
}

namespace storm::effects
{

struct ReliefSetup;
// The relief directory and the generator's model (render::ConfigureRelief). An empty directory: no relief at all
// (logged once).
void ConfigureRelief(ReliefSetup setup);

inline constexpr std::chrono::milliseconds kReliefFade{300};

// One texture's normal map (TexStore::normal_map) or gloss map (TexStore::gloss_map: `gloss` is not None), owned by its
// TexStore.
struct NormalMapEntry
{
    enum State : int
    {
        Unqueued, // not looked up (yet, or again): no draw has read it, its request was dropped (a new scene, the
                  // settings), or its map left the cache since: the next draw that reads it looks it up
        Pending,  // on a texture worker (being looked up, read or derived)
        Stored,   // in the relief directory
        Waiting,  // a colour texture whose map (or gloss map) the relief generator is making
        Missing,  // a colour texture without a map that will not get one: the generator is unavailable (logged)
        Derived,  // `map` is ready for the upload
        Uploaded, // `handle` is valid
        Failed,
    };
    GenerationPriority priority; // nearest player distance across observed instances
    GenerationScreenSize screen; // projected size ×3 on characters; only breaks equal fresh distance ranks
    uint16_t queued_scene = 0;
    uint64_t queued_after = 0; // generator admission counter, under its queue mutex
    std::atomic<int> state{Unqueued};
    std::shared_ptr<texture::CacheTask> cache_task; // game thread; queued task retains its own ticket
    std::atomic<bool> wanted{false}; // a draw asked for it: the worker keeps the map in memory for the upload
    std::atomic<uint32_t> drawn{0}; // the frame a draw that reads it last asked for it, 0: none in this scene since the
                                    // settings changed (the generator makes those on screen first)
    std::string name;               // normalized texture path, for the logs
    fx::ReliefClass relief = fx::ReliefClass::None;
    fx::GlossClass gloss = fx::GlossClass::None;          // a gloss map's class
    fx::GlossClass material_gloss = fx::GlossClass::None; // a relief map's: the gloss class of its material's G
    // fx::ReliefKey of the map the settings read: a relief map's with or without its height (key_height, key_flat:
    // fx::ReliefMethod, NormalMaps::configure's `heights`; the same for a class without a height), a gloss map's own.
    // Written on the main thread, read by the texture workers and the generator.
    std::atomic<uint64_t> key{0};
    uint64_t key_height = 0, key_flat = 0;
    uint64_t map_key = 0; // the key `map` was read under, written by the worker before `state` becomes Derived
    bool heights() const
    {
        return key == key_height;
    } // its map carries the height
    std::filesystem::path file; // the game file the texture was loaded from (the generator and the derivation read it)
    fx::NormalMap map;          // written by the worker before `state` becomes Derived
    bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle material = BGFX_INVALID_HANDLE; // the uploaded material texture (R height, G gloss)
    float depth = 0.0f;                                 // the height's 0..1 span in texels of the uploaded top mip
    float trust_coverage = 1.0f;                        // the share of trusted texels (fx/parallax_trust.hpp)
    int width = 0, height_texels = 0;                   // the uploaded top mip
    size_t bytes = 0;                                   // of the uploaded textures
    size_t material_bytes = 0;                          // of them the material's
    std::chrono::steady_clock::time_point uploaded{};   // its relief fades in from here
    bool failure_reported = false;
    bool no_height_reported = false;
    ~NormalMapEntry();
};

class NormalMaps
{
  public:
    // `max_size`: larger side of the uploaded top mip (Settings::normal_map_size); `material`: the material textures
    // are uploaded too (the parallax, the cavity or the gloss reads them); `characters`: the characters' draws read
    // their relief and gloss (Settings::character_relief). Turning the option off or changing the size or `material`
    // releases the uploaded maps; they come back from the relief directory when drawn again. `heights`: the relief maps
    // carry the height (fx::BackgroundProducts::heights); a change takes the textures' other maps (their keys,
    // fx::ReliefMethod): the uploaded ones are released and the next draws look the others up. A change of what the
    // draws read (`enabled`, `characters`, `heights`, or the gloss) drops the generator's queue: the next draws that
    // read a map ask again.
    void configure(bool enabled, int max_size, bool material, bool characters, bool heights);
    // Main thread, a new scene (render::BeginScenePipelines): logs what the previous scene looked up and why, forgets
    // which textures were drawn and drops the generator's queue (the new scene's draws ask for theirs).
    void begin_scene(const char *scene);
    // Main thread, right after the texture's levels were filled. `file`: the game file it was read from.
    void on_texture_loaded(shim::TexStore &store, const char *path, const std::filesystem::path &file);
    // The uploaded map of the texture, or invalid: no relief for it, its map still being made, read or derived (relief
    // appears later), or failed (logged). `fade`: 0..1, the share of its relief while it fades in.
    struct Relief
    {
        bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
        float fade = 0.0f;
        bgfx::TextureHandle material = BGFX_INVALID_HANDLE; // the material texture, for a class with a height
        float depth = 0.0f;                                 // its height's span in texels of the top mip
        bool parallax = true;                               // the parallax draws (trust coverage, exclusion lists)
        int width = 0, height_texels = 0;                   // the top mip
    };
    Relief handle(shim::TexStore &store, fx::Lighting &lighting);
    struct Selection
    {
        Relief map;
        const NormalMapEntry *entry = nullptr;
        int stage = -1;
    };
    // Enabled stages only (disabled stages are null). A pending/failed detail map cannot hide a ready colour map.
    // If neither is ready, the preferred entry is kept (its relief class still decides the parallax depth).
    Selection select(shim::TexStore *colour, shim::TexStore *detail, fx::Lighting &lighting);

    // Gloss maps: as configure() and handle(), for Settings::gloss and Settings::gloss_map_size.
    void configure_gloss(bool enabled, int max_size);
    Relief gloss(shim::TexStore &store, fx::Lighting &lighting);

    struct Stats
    {
        uint32_t resident = 0;  // uploaded maps
        size_t bytes = 0;       // their GPU memory (with the materials)
        uint32_t materials = 0; // of them with a material texture uploaded
        size_t material_bytes = 0;
        uint32_t unqueued = 0; // loaded, not looked up (no draw read them, or their request was dropped)
        uint32_t waiting = 0;  // loaded colour textures of a relief class whose map is being made
        uint32_t missing = 0;  // loaded colour textures of a relief class that get no map (generator unavailable)
        uint32_t gloss_resident = 0, gloss_unqueued = 0, gloss_waiting = 0, gloss_missing = 0;
        size_t gloss_bytes = 0;
    };
    Stats stats();
    // Pump only a current-location texture whose map a draw of this scene has read (drawn): one no draw read is not
    // applicable (0) and starts nothing. 0 ready/not applicable, 1 pending, 2 retryable producer failure, 3 upload
    // failure requiring scene reload. Stored is not ready.
    int prepare_scene(shim::TexStore &store, fx::Lighting &lighting, bool *inference_pending = nullptr);
    void retry_scene(shim::TexStore &store);
    // Once per summary period, when the stats changed: one "effects relief:" line (and "effects gloss:").
    void end_frame();

  private:
    // The draw takes the parallax: not excluded, and the trust coverage reaches kParallaxMinCoverage.
    bool parallax_allowed(const NormalMapEntry &entry);
    void load_exclusions();
    fx::ParallaxExclusions exclusions_;
    bool exclusions_loaded_ = false;
    void release(NormalMapEntry &entry);
    void read_stored(const std::shared_ptr<NormalMapEntry> &entry);
    // On a texture worker: Stored when the directory has its map, else requested from the generator.
    void look_up(const std::shared_ptr<NormalMapEntry> &entry);
    // The same for a gloss map: Stored, or requested from the generator.
    void look_up_gloss(const std::shared_ptr<NormalMapEntry> &entry);
    // `draw`: a draw reads it now (it is marked drawn); else the loading screen pumps one a draw read before.
    Relief upload(const std::shared_ptr<NormalMapEntry> &entry, fx::Lighting &lighting, int max_size, bool draw = true);
    bool reads(const NormalMapEntry &entry) const; // its product is on: a draw that asks for it reads it
    void forget_drawn(const char *why);            // drawn = 0 everywhere, the generator's queue dropped
    void add_gloss(shim::TexStore &store, const std::string &name, std::span<const uint8_t> probe, uint32_t pw,
                   uint32_t ph, const std::filesystem::path &file);

    bool enabled_ = false;
    int max_size_ = 1024;
    bool material_ = false;
    bool characters_ = false;
    bool heights_ = false;
    // What this scene looked up (look_up / look_up_gloss, counted on the texture workers), for begin_scene's line.
    struct SceneTally
    {
        std::array<std::atomic<uint32_t>, 9> relief_requested{}, relief_stored{}; // by fx::ReliefClass
        std::atomic<uint32_t> gloss_requested{0}, gloss_stored{0};
    };
    std::shared_ptr<SceneTally> tally_ = std::make_shared<SceneTally>();
    std::string scene_;
    fx::ReliefClassifier classifier_;
    bool format_reported_ = false;
    bool directory_reported_ = false;
    std::vector<std::weak_ptr<NormalMapEntry>> entries_; // for configure() and stats()
    bool gloss_enabled_ = false;
    int gloss_max_size_ = fx::kGlossMapSize;
    std::vector<std::weak_ptr<NormalMapEntry>> gloss_entries_;
    std::vector<std::string> gloss_logged_; // names whose gloss class was logged
    std::chrono::steady_clock::time_point next_log_{};
    uint32_t frame_ = 1;                                                           // for NormalMapEntry::drawn
    std::chrono::steady_clock::time_point now_ = std::chrono::steady_clock::now(); // this frame's, for the fade
    Stats logged_{};
    uint32_t logged_made_ = 0;
    std::string logged_device_;
};

} // namespace storm::effects
