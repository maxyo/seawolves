// The hour's sun on the towns' lightmaps (fx/town_light.hpp, fx/sun_relight.hpp), on Средняя / Высокая: a light
// ratio map per lightmap that the lightmapped draws multiply the game's own lightmap by. Design:
// renderer_bgfx/README.md, "Town light".
#pragma once

#include "effects/place.hpp"
#include "storm/fx/sun_fit.hpp"
#include "storm/fx/sun_relight.hpp"
#include "storm/fx/town_light.hpp"
#include "storm/render/effects.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace storm::shim
{
struct Device;
struct TexStore;
} // namespace storm::shim

namespace storm::effects
{

class Casters;
class SunShadows;

class TownLight
{
  public:
    // Off: the bakes still running are cancelled and no new one starts; on: the place's bake starts if it is missing.
    void set_enabled(bool enabled);
    // The place's lightmapped triangles and lightmaps (empty: none, e.g. at sea, indoors, when leaving).
    void set_lightmaps(std::string_view name, std::span<const glm::vec3> positions,
                       std::span<const std::array<float, 2>> uvs, std::span<const uint32_t> indices,
                       std::span<const uint32_t> triangle_lightmap, std::span<const render::EffectsLightmap> lightmaps);
    // Main thread, every frame: the place once its probes are there (null: none, not yet, or an interior).
    void set_place(std::shared_ptr<const PlaceReady> place);
    // The set's bake still needs the place's rays (PlaceBake keeps the ray structure for it).
    bool needs_rays() const
    {
        return enabled_ && place_ && (place_job_ || want_place_);
    }
    // The walls' relight (Lighting, u_fxTownWall): on while the sun is up and the walls are calibrated.
    bool walls_active() const
    {
        return active() && wall_ready_ && sun_present_ && wall_.valid;
    }
    const fx::TownLightTone *wall_light() const
    {
        return wall_ready_ ? &wall_light_ : nullptr;
    }
    // The walls' relight this frame: 0 -> 1 over shim::kTextureFadeMs after it came on (update()), 0 while off.
    float wall_weight() const
    {
        return wall_weight_;
    }
    const fx::TownWallTone *wall_tone() const
    {
        return &wall_;
    }
    const glm::vec3 &live_sun() const
    {
        return live_sun_;
    }
    glm::vec3 bake_sun() const
    {
        return bake_dir_;
    }
    // How far the walls' light may rise above the game's now (fx::RelightLift of the hour's sun against the bake's).
    float wall_lift() const;
    // Once per frame, before the draws: `sun` the hour's direction towards the sun (world), `present` false at night.
    void update(const glm::vec3 &sun, bool present);
    // Once per frame after update(): the hour's sun on the lightmaps. Draws the place's sun map when the sun moved
    // (the still geometry's tiles of `shadows`) and composes the ratio maps that are due, one a frame.
    void render(shim::Device &device, SunShadows &shadows, Casters &casters);
    // The light ratio a draw that binds the game's lightmap `store` multiplies it by (ff_stages.sh ffTownRatio).
    struct Ratio
    {
        bgfx::TextureHandle texture = BGFX_INVALID_HANDLE; // invalid: the game's lightmap as it is
        float weight = 0.0f;                               // 0 -> 1 while it fades in (1 -> 0 while it fades out)
    };
    // For any draw that binds a game town lightmap (the shim asks for every one it binds); counted for the profile.
    Ratio ratio(const shim::TexStore *store);
    // The shim could not apply the ratio of `store` to a draw (stages 2 to 4 all sample textures, a game shader port):
    // drawn with the game's lightmap as it is, counted, and said once per place.
    void ratio_missed(const shim::TexStore *store, const char *why);
    // What a shadow leaves of an open texel of a relit lightmap for `store`: its light without the hour's sun over its
    // light with it, from the lightmap's tone (a + c sky) / (a + b sun + c sky); `unmeasured` where no ratio is
    // applied.
    float sun_shade(const shim::TexStore *store, float unmeasured) const;
    void end_frame();
    // Draws of the last frame that bound a game town lightmap / multiplied it by the ratio / could not (ratio_missed).
    std::array<uint32_t, 3> last_counts() const
    {
        return last_counts_;
    }
    // Since the start: the place's sun map drawn for the hour's sun, ratio maps composed, the place map drawn along the
    // bake's sun (its visibility read once per lightmap) (host checks, the profile line).
    struct SunCounters
    {
        uint32_t place_draws = 0, composes = 0, bake_draws = 0;
    };
    const SunCounters &sun_counters() const
    {
        return sun_counters_;
    }
    bool active() const
    {
        return enabled_ && ready_;
    }
    // The ratio's weight this frame (fade_ratio(); 1 once it has faded in) (host checks).
    float ratio_weight() const
    {
        return ratio_weight_;
    }
    // The place's lightmap set is being made for the location's reveal (scene readiness, SceneProduct::TownLight):
    // its job runs (cache load or bake), starts at the next update (the probes are there and the sun is up), or its
    // ratio maps wait for their first composition. False while off, without lightmapped ground, while the probes are
    // still being made (their own product) and on a device that cannot relight.
    bool preparing() const;

    void shutdown();

  private:
    struct Map
    {
        // The game's lightmap (its texture) this one replaces: not owned (the game frees it with the location), read
        // only through a lock; the draws are matched by its serial, which no later texture at the same address has.
        std::weak_ptr<const shim::TexStore> store;
        uint64_t serial = 0;
        std::shared_ptr<const fx::LightmapTexels> texels; // shared with the sun jobs on the bake thread
        std::vector<uint8_t> game;                        // its RGBA8 texels at texels' size
        std::vector<uint8_t> sky;                         // per texel
        fx::TownLightTone tone;
        float texel_metres = 0.0f; // the world size of one of its texels
        float ratio_metres = 0.0f; // ... of one of its ratio map's (the sun is read over one)
        fx::SunRelight::Map relight = fx::SunRelight::kNoMap; // its inputs and ratio map on the GPU
        bool compose_due = true;
    };
    // The lightmapped ground as the location reported it, shared with the bakes.
    struct Lightmapped
    {
        std::vector<glm::vec3> positions;
        std::vector<std::array<float, 2>> uvs;
        std::vector<uint32_t> indices, triangle_lightmap;
        std::vector<render::EffectsLightmap> lightmaps;
    };
    struct PlaceJob
    {
        std::atomic<int> state{0}; // 0 running, 1 done
        std::vector<Map> maps;     // made by the load, finished by it (the set is cached) or by the bake
        uint64_t set_key = 0;
        fx::SunFit bake;
        fx::TownWallTone wall;
        fx::TownLightTone wall_light; // the tone of the first relit lightmap
        std::string how;
    };
    void start_place();
    // The steps of the place's job (start_place), on the bake's threads.
    static bool LoadPlaceSet(PlaceJob &job, const std::vector<std::shared_ptr<const shim::TexStore>> &stores,
                             const Lightmapped &lm, const PlaceReady &place,
                             std::chrono::steady_clock::time_point started);
    static bool BakePlaceSet(PlaceJob &job, const Lightmapped &lm, const PlaceReady &place,
                             std::chrono::steady_clock::time_point started);
    static std::vector<fx::SunFitSample> SunFitSamples(const std::vector<Map> &maps);
    static void CalibrateWalls(PlaceJob &job, const Map &first, const Lightmapped &lm, const Place &geometry,
                               const fx::VisibilityVolume &volume, const fx::Occluders &occluders, unsigned threads);
    void upload_map(Map &map);
    void compose_all();
    void fade_walls();
    void fade_ratio();
    static float TexelMetres(const fx::LightmapTexels &texels);
    static void RetireRatio(bgfx::TextureHandle texture);

    bool enabled_ = false;
    bool ready_ = false;
    std::string name_;
    std::shared_ptr<const Lightmapped> lightmapped_ = std::make_shared<Lightmapped>();
    std::shared_ptr<const PlaceReady> place_;
    bool want_place_ = false; // the set's job is due once the sun is up
    fx::Bake<PlaceJob> place_job_;
    std::vector<Map> maps_;
    fx::SunRelight relight_{RetireRatio};   // a ratio map let go is kept while the frame's draws may still read it
    glm::vec3 place_sun_{0.0f, 0.0f, 0.0f}; // the direction the place's sun map was drawn for (0: not drawn)
    bool place_is_bake_ = false;            // ... the bake's (for the lightmaps' bake visibility)
    uint64_t place_cut_signature_ = 0;      // ... and the cut-out textures it was drawn with
    bool night_ = false;                    // the lightmaps were composed without a sun
    bool unsupported_reported_ = false;
    SunCounters sun_counters_{};
    fx::TownWallTone wall_;
    fx::TownLightTone wall_light_; // the tone of the first relit lightmap
    bool wall_ready_ = false;
    bool walls_were_active_ = false;
    uint64_t walls_since_ = 0;
    float wall_weight_ = 0.0f;
    bool sun_present_ = false;
    // The ratio's weight: fades from `ratio_from_` towards 1 (shown) or 0 (not) since `ratio_since_` (fade_ratio()).
    bool ratio_shown_ = false;
    float ratio_from_ = 0.0f, ratio_weight_ = 0.0f;
    uint64_t ratio_since_ = 0;
    bool ratio_missed_logged_ = false;
    glm::vec3 live_sun_{0.0f, 1.0f, 0.0f};
    glm::vec3 bake_dir_{0.0f, 1.0f, 0.0f}; // the direction the game's lightmaps were baked with
    std::array<uint32_t, 3> counts_{}, last_counts_{};
};

} // namespace storm::effects
