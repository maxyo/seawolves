// Shadow casters collected by the lights: they publish their shadow volumes, the shim's caster entry offers every
// draw and submit() puts it into each volume it meets (the game's side: storm/render/shadow_casters.hpp). Frame order:
// publish() while a light prepares its maps, submit() per caster, stats() until end_frame(), which the publishers'
// own end_frame() precede. Design: renderer_bgfx/README.md, "Shadow casters".
#pragma once

#include "effects/casters.hpp"
#include "storm/render/shadow_casters.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <vector>

namespace storm::shim
{
struct Device;
}

namespace storm::effects
{

enum class ShadowLayer : uint8_t
{
    Static,  // drawn rarely and kept: the still geometry of `still`
    Dynamic, // drawn every frame: everything else
};

struct ShadowVolume
{
    ShadowLayer layer = ShadowLayer::Dynamic;
    uint64_t still = 0;                      // the owner whose still geometry the light's static layer holds; 0: none
    Casters::Pass pass = Casters::Pass::Sun; // the caster programs (effects/casters.hpp)
    bgfx::ViewId view = 0;
    glm::vec3 origin{}; // absolute world = volume space + origin
    // Where a caster reaches the map, in volume space: inside every plane (n·p + d >= 0, n unit) and, with
    // sphere[3] > 0, within the sphere (xyz centre, w radius: a lamp's range).
    std::array<glm::vec4, 6> planes{};
    uint8_t plane_count = 0;
    glm::vec4 sphere{};
    // The model matrix of a caster in the view: its world matrix (in volume space) times this. Row vectors, as bx.
    std::array<float, 16> transform{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                    0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    uint64_t state = BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS; // the primitive type is the caster's
    std::array<uint16_t, 4> scissor{}; // x, y, width, height in the view's target; width 0: none (an atlas tile)
    // Where the light is, so a caster whose shadow cannot fall into the camera's view is left out (set_camera()):
    // w = 1: a point at xyz (a lamp; its shadows end at the sphere's radius); w = 0: xyz is the unit direction towards
    // it (the sun; its shadows are `shadow_length` long); w < 0: not given, every caster in the bounds is taken.
    glm::vec4 light{0.0f, 0.0f, 0.0f, -1.0f};
    float shadow_length = 0.0f;
    // A lamp's holder sphere (volume space, w > 0): a caster whose sphere meets it is drawn with
    // Casters::Pass::LampHolder (nothing inside the sphere casts), the others with `pass` (plain depth, early depth
    // testing kept).
    glm::vec4 holder{};
    // One vec4 set with every submit (the lamp casters' u_fxLampCaster); invalid: none.
    bgfx::UniformHandle uniform = BGFX_INVALID_HANDLE;
    std::array<float, 4> uniform_value{};

    // The planes of the clip volume of `to_clip` (volume space -> the clip space of the map's own square, before any
    // atlas placement; an orthographic or a perspective projection; clip depth 0..w, or -w..w with
    // `homogeneous_depth`, bgfx::Caps::homogeneousDepth). `depth` false: the four side planes only, no near and far.
    void set_clip_bounds(const std::array<float, 16> &to_clip, bool homogeneous_depth, bool depth = true);
};

struct ShadowVolumeStats
{
    uint32_t draws = 0; // casters submitted into the volume this frame
    uint32_t triangles = 0;
    uint32_t skinned = 0;   // of the draws: with a bone palette
    uint32_t offscreen = 0; // of the draws: the camera did not draw them (render::ShadowCasterOnly)
    // The box of the admitted casters' spheres in volume space (lo > hi: none): a light may fit its map to it.
    glm::vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
};

// A candidate draw, as the shim's caster entry describes it. Its buffers, the textures its material cuts with and
// its bone palette are set on the encoder when submit() is called.
struct ShadowCaster
{
    const float *world = nullptr; // its world matrix in the device's world (16 floats, row vectors)
    glm::vec3 origin{};           // absolute world = device world + origin
    // The sphere that bounds it, device world; radius < 0: unknown (every volume takes it).
    glm::vec3 center{};
    float radius = -1.0f;
    uint64_t still = 0; // render::ShadowCasterStill
    bool lit = false;   // drawn with lighting and normals: what the dynamic layers take
    bool sun = false;   // ... under a directional light and no lightmap: Casters::Pass::Sun takes it
    bool skinned = false;
    bool offscreen = false; // render::ShadowCasterOnly
    uint64_t primitive = 0; // BGFX_STATE_PT_* of its primitive type
    uint32_t triangles = 0;
    CasterMaterial material{};
};

class ShadowCasters
{
  public:
    using Id = uint32_t;
    // A light, while it prepares its maps: the volume's id, valid until end_frame().
    Id publish(const ShadowVolume &volume);
    // Lighting, before the lights publish: the camera of the picture (`view_proj`: device world -> clip, depth 0..w;
    // `origin` as ShadowCaster::origin). Shadows are seen only inside its side planes: the game draws one frame with
    // several near and far planes (at sea the far pass's near plane lies beyond the ships), so the projection of
    // whichever draw comes first bounds the view by its field of view only.
    void set_camera(const std::array<float, 16> &view_proj, const glm::vec3 &origin);
    size_t volumes() const
    {
        return volumes_.size();
    }
    const ShadowVolume &volume(Id id) const
    {
        return volumes_[id];
    }
    const ShadowVolumeStats &stats(Id id) const
    {
        return stats_[id];
    }

    // True when a volume could take a caster of `still` inside the sphere (device world, `origin` as
    // ShadowCaster::origin): the traversal's question for what the camera rejects.
    bool reach(const glm::vec3 &center, float radius, const glm::vec3 &origin, uint64_t still) const;
    // True when a volume takes the caster: the shim binds its buffers only then.
    bool takes(const ShadowCaster &caster) const;
    // Submits the caster into every volume that takes it and leaves the encoder's draw state discarded. The number
    // of submits.
    uint32_t submit(shim::Device &device, Casters &programs, const ShadowCaster &caster);
    void end_frame();

    // The scopes of storm/render/shadow_casters.hpp.
    uint64_t still() const
    {
        return still_;
    }
    void set_still(uint64_t owner)
    {
        still_ = owner;
    }
    bool only() const
    {
        return only_;
    }
    void set_only(bool only)
    {
        only_ = only;
    }
    // xyz the centre in the absolute world, w the radius; w < 0: no node is being drawn.
    const glm::vec4 &bounds() const
    {
        return bounds_;
    }
    void set_bounds(const glm::vec4 &bounds)
    {
        bounds_ = bounds;
    }

    // The last frame, for the effects profile line: per shadow view its caster draws and triangles, and the frame's
    // candidates.
    struct ViewCount
    {
        bgfx::ViewId view = 0;
        uint32_t draws = 0, triangles = 0;
    };
    struct FrameCount
    {
        uint32_t candidates = 0; // draws that could cast (lit or still, a material that casts): offered to the volumes
        uint32_t casters = 0;    // ... that a volume took (each counted once)
        uint32_t offscreen = 0;  // of the casters: not drawn by the camera
        uint32_t skinned = 0;    // of the casters: with a bone palette
        uint32_t unbounded = 0;  // of the casters: without bounds
        std::vector<ViewCount> views;
    };
    const FrameCount &last() const
    {
        return last_;
    }
    // The shim, for every candidate it offers.
    void count_candidate()
    {
        ++frame_.candidates;
    }

  private:
    bool accepts(const ShadowVolume &volume, const ShadowCaster &caster) const;
    bool meets(const ShadowVolume &volume, const ShadowCaster &caster, glm::vec3 &in_volume) const;
    ShadowVolume camera_; // its planes and origin; plane_count 0: no camera this frame

    std::vector<ShadowVolume> volumes_;
    std::vector<ShadowVolumeStats> stats_;
    // The box of the volumes' spheres in the absolute world, while every volume has one (the lamps' faces): most of a
    // scene is nowhere near a lamp.
    std::array<double, 3> lo_{}, hi_{};
    bool boxed_ = false;
    uint64_t still_ = 0;
    bool only_ = false;
    glm::vec4 bounds_{0.0f, 0.0f, 0.0f, -1.0f};
    FrameCount frame_, last_;
    bool unbounded_reported_ = false;
};

} // namespace storm::effects
