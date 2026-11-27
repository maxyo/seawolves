// Sun shadows (fx::Shadows, fx/shadow_model.hpp): a static and a dynamic layer on one projection around the camera's
// focus, drawn in pre-game views. Design: renderer_bgfx/README.md, "Sun shadows".
#pragma once

#include "storm/fx/settings.hpp"
#include "storm/fx/shadows.hpp"
#include "storm/fx/shadow_model.hpp"
#include "effects/casters.hpp"
#include "effects/shadow_casters.hpp"
#include "storm/render/effects.hpp"

#include <algorithm>
#include <bgfx/bgfx.h>

#include <array>
#include <functional>
#include <string>
#include <vector>
#include <cstdint>
#include <utility>

namespace storm::shim
{
struct Device;
}

namespace storm::effects
{

class SunShadows
{
  public:
    void configure(const fx::Settings &settings)
    {
        settings_ = settings;
    }
    bool enabled() const
    {
        return settings_.shadows != fx::ShadowQuality::Off;
    }
    // World direction towards the sun of this frame (WEATHER); none: no shadows this frame. `strength`: the scene
    // environment's shadow strength of the hour (0 none .. 1 full).
    void set_sun(const glm::vec3 &sun_dir, bool present, float strength)
    {
        sun_dir_ = sun_dir;
        sun_present_ = present;
        hour_strength_ = std::clamp(strength, 0.0f, 1.0f);
    }
    // The place's still, collidable geometry (render::SetEffectsLocationGeometry, outdoors): it casts into the
    // static layer in tiles, with the holes its textures' alpha cuts (effects/casters.hpp). Empty: none (indoors, at
    // sea).
    void set_casters(const render::EffectsLocationGeometry &geometry);
    // For a draw into the game picture with the camera `view` (D3D row-vector device world -> view; `origin`: the
    // device world's origin in absolute world, Lighting::set_world_origin): prepares this frame's layers on the first
    // call (the static one only when it is due) and publishes the dynamic one to `volumes` for the casters that
    // move. True when the frame has sun shadows.
    bool prepare(shim::Device &device, const std::array<float, 16> &view, const glm::vec3 &origin, Casters &casters,
                 ShadowCasters &volumes);
    // After a true prepare(): a moving caster's shadow (the box of the last frame's casters, swept away from the
    // sun) can fall on the sphere (device world).
    bool shaded(const glm::vec3 &center, float radius) const;
    // ... on anything at all: the last frame had moving casters.
    bool casting() const
    {
        return active_ && footprint_;
    }
    // Receiver: binds the map and its matrix for the draw's camera.
    void bind(const std::array<float, 16> &view, const fx::SunReceiver &receiver)
    {
        shadows_.Bind(view, receiver);
    }
    // A draw of a shadowed program that receives nothing (fx::Shadows::BindUnshadowed).
    void bind_unshadowed()
    {
        shadows_.BindUnshadowed(settings_);
    }
    // The still geometry's tiles that `reaches` (a tile's sphere, absolute world) into `view`, as depth-only casters
    // with their textures' holes; `origin`: the world position the view's coordinates are relative to. For the
    // static layer and for the place's sun map of the towns' relight (effects/town_light.hpp). {tiles, triangles}.
    std::array<uint32_t, 2> draw_tiles(shim::Device &device, Casters &casters, bgfx::ViewId view,
                                       const glm::vec3 &origin,
                                       const std::function<bool(const glm::vec3 &, float)> &reaches);
    // The cut-out textures' GPU handles of the tiles: one that changed (loaded, replaced) voids what was drawn with
    // them.
    uint64_t cut_signature() const;
    // Before bgfx::frame(): the dynamic layer's clear is decided by the frame's casters (the volume's count).
    void before_frame();
    int initial_state(bool applicable = true) const
    {
        if (!applicable || !enabled() || !sun_present_ || strength() <= 0.0f)
            return 0;
        if (!prepared_)
            return 1;
        if (!active_)
            return 2;
        if (caster_indices_.empty())
            return 0;
        if (tiles_dirty_)
            return 1;
        return !tiles_failed_ && bgfx::isValid(tiles_vb_) ? 0 : 2;
    }
    void end_frame();

    void shutdown();

    // Since the last take (the effects profile line, every 30 frames).
    struct Counters
    {
        uint32_t static_redraws = 0;   // frames that drew the static layer
        uint32_t static_tiles = 0;     // ... their tiles
        uint32_t static_triangles = 0; // ... and triangles
        uint32_t dynamic_casters = 0;  // draws submitted into the dynamic layer
        uint32_t dynamic_frames = 0;   // frames that drew the dynamic layer
    };
    Counters take_counters()
    {
        const Counters taken = counters_;
        counters_ = {};
        return taken;
    }
    // Totals since the start (host checks).
    const Counters &totals() const
    {
        return totals_;
    }

  private:
    fx::Settings settings_{};
    fx::Shadows shadows_;
    fx::SunLayerTracker tracker_;
    fx::SunLayers layers_{};
    std::string casters_name_;
    std::vector<glm::vec3> casters_;
    std::vector<std::array<float, 2>> caster_uvs_;
    std::vector<uint32_t> caster_indices_;
    // Per triangle: the cut-out texture, null when solid. Held: the game may release its texture wrapper while the
    // place's geometry (and these tiles) live (effects.hpp: Surface::texture is alive only during the report).
    std::vector<shim::TexStorePtr> caster_texture_;
    glm::vec3 sun_dir_{0.0f, 1.0f, 0.0f};
    glm::vec3 origin_{};
    bool sun_present_ = false;
    float hour_strength_ = 1.0f;
    float strength() const
    {
        return fx::SunShadowStrength(sun_dir_[1]) * hour_strength_;
    }
    bool prepared_ = false; // prepare() ran this frame
    bool active_ = false;   // ... and fx::Shadows::Begin() accepted it
    // The place's still geometry in tiles of kTileSize metres (xz): the tiles the static layer reaches are drawn into
    // it.
    struct Tile
    {
        glm::vec3 lo{}, hi{};
        bgfx::IndexBufferHandle solid = BGFX_INVALID_HANDLE;
        std::vector<std::pair<shim::TexStorePtr, bgfx::IndexBufferHandle>> cut; // with their texture's holes
        uint32_t triangles = 0;
    };
    bgfx::VertexBufferHandle tiles_vb_ = BGFX_INVALID_HANDLE; // positions and texture coordinates of every tile
    std::vector<Tile> tiles_;
    bool tiles_dirty_ = false;
    bool tiles_failed_ = false; // a required VB or (solid/cut) IB failed allocation
    void build_tiles();
    void destroy_tiles();
    uint64_t drawn_cut_signature_ = 0; // cut_signature() the static layer was drawn with
    // This frame's dynamic layer among the shadow volumes (the registry is Lighting's; valid until its end_frame()).
    ShadowCasters *volumes_ = nullptr;
    ShadowCasters::Id volume_ = 0;
    bool published_ = false;
    // The last frame's moving casters for shaded(): their box in light space (x, y its sides, z its sunward end).
    bool footprint_ = false;
    std::array<float, 2> footprint_lo_{}, footprint_hi_{};
    float footprint_near_ = 0.0f;
    Counters counters_{}, totals_{};
    int logged_centre_ = -1;             // what the region was centred on when it was last said
    const void *logged_owner_ = nullptr; // and the player entity (identity only)
};

} // namespace storm::effects
