// Shadow casters as the game's traversal sees them (game thread only): what the camera culls, skinned bounds and
// still geometry. Design: renderer_bgfx/README.md, "Shadow casters".
#pragma once

#include <storm_glm.h>

#include <array>
#include <cstdint>

namespace storm::render
{

// The owner of the location's still geometry (render::SetEffectsLocationGeometry). A ship is its own owner: the id
// it reports its lanterns and its still geometry with (render::EffectsShip::id, never 0 or 1).
inline constexpr uint64_t kShadowStillLocation = 1;

// While one lives, the draws are still geometry of `owner`, as reported to the effects (the location's nodes of
// SetEffectsLocationGeometry, a ship's model of SetEffectsShipGeometry); 0: they move (a subtree that is animated
// inside a still model). Nested scopes restore the outer owner.
class ShadowCasterStill
{
  public:
    explicit ShadowCasterStill(uint64_t owner);
    ~ShadowCasterStill();
    ShadowCasterStill(const ShadowCasterStill &) = delete;
    ShadowCasterStill &operator=(const ShadowCasterStill &) = delete;

  private:
    uint64_t previous_;
};

// True when a shadow volume of the frame could take a caster inside this sphere, drawn now (the current render
// target, camera and ShadowCasterStill owner): `center` in the absolute world, or (Local) in the space of the current
// D3DTS_WORLD transform, where `radius` is scaled by it. The first call of a frame publishes the frame's volumes.
bool ShadowCasterReach(const glm::vec3 &center, float radius);
bool ShadowCasterReachLocal(const glm::vec3 &center, float radius);

// While one lives, the sphere (absolute world) bounds every draw, whatever its pose: the model node being drawn. The
// shim tests a skinned draw with it (its vertices are in the bind pose); a skinned draw outside any such scope has
// no bounds: every volume takes it, and it is counted and logged.
class ShadowCasterBounds
{
  public:
    ShadowCasterBounds(const glm::vec3 &center, float radius);
    ~ShadowCasterBounds();
    ShadowCasterBounds(const ShadowCasterBounds &) = delete;
    ShadowCasterBounds &operator=(const ShadowCasterBounds &) = delete;

  private:
    glm::vec4 previous_;
};

// While one lives, the draws are submitted to the shadow volumes only: what the camera does not see.
class ShadowCasterOnly
{
  public:
    ShadowCasterOnly();
    ~ShadowCasterOnly();
    ShadowCasterOnly(const ShadowCasterOnly &) = delete;
    ShadowCasterOnly &operator=(const ShadowCasterOnly &) = delete;

  private:
    bool previous_;
};

} // namespace storm::render
