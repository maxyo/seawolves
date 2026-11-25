// How a material is drawn in a pass that renders depth instead of colour (the sun layers, the lamp atlases, the AO
// bake), and the caster programs. Design: renderer_bgfx/README.md, "Casters".
#pragma once

#include "shim/shim.hpp"

#include <bgfx/bgfx.h>

#include <array>

namespace storm::shim
{
struct Device;
struct LayoutInfo;
} // namespace storm::shim

namespace storm::effects
{

struct CasterMaterial
{
    bool casts = true;
    float cut = 0.0f;             // > 0: the alpha reference (0..1) the stages below are tested against
    std::array<float, 3> stage{}; // 1 = the alpha of this stage's texture takes part
    std::array<float, 3> uv{};    // the texture coordinate set of the stage
};

// The material of the draw the device is about to make.
CasterMaterial caster_material(const shim::Device &device, const shim::LayoutInfo &layout);
// The material of the still location geometry drawn with `texture` at stage 0 under the game's default states
// (alpha test on at kGameAlphaReference, no blending: GEOM_SERVICE_R::SetMaterial, Renderer::SetCommonStates).
CasterMaterial location_caster_material(const shim::TexStore *texture);

class Casters
{
  public:
    enum class Pass
    {
        Sun,        // depth of the sun's layers
        Lamp,       // depth of a lamp face
        LampHolder, // depth of a lamp face, nothing inside the lamp's holder sphere (u_fxLampCaster)
    };
    // The program of a caster (logs a failure and returns an invalid handle), with its cut uniforms set when the
    // material has one. The material's textures must be bound at their stages (a draw's own still are).
    bgfx::ProgramHandle program(shim::Device &device, Pass pass, bool skinned, const CasterMaterial &material);
    // Creates the programs of `pass` (Lighting::prepare_programs), at most `budget` of them.
    bool prepare_programs(shim::Device &device, Pass pass, int &budget);
    // Stage 0's sampler, for a pass that binds a location texture itself.
    bgfx::UniformHandle sampler();

    void shutdown();

  private:
    void find_shaders();
    void ensure_uniforms();

    uint32_t vs_[2][2] = {}; // [cut][skinned]
    uint32_t fs_[3][2] = {}; // [pass][cut]
    bgfx::UniformHandle u_cut_ = BGFX_INVALID_HANDLE, u_uv_ = BGFX_INVALID_HANDLE, s_tex0_ = BGFX_INVALID_HANDLE;
};

} // namespace storm::effects
