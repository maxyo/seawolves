// What the GPU behind bgfx supports, for Support / ConstrainSettings() (settings.hpp).
#pragma once

#include "storm/fx/settings.hpp"

namespace storm::fx
{

// What only the renderer knows; bgfx has no capability bits for these.
struct RendererFacts
{
    bool scene_depth = false; // the game's offscreen depth attachment is a texture that can be sampled
    bool msaa_depth = false;  // ... and it still can when the scene target is multisampled (depth is resolved)
    bool anisotropy = false;  // anisotropic filtering exists (GL_EXT_texture_filter_anisotropic or core)
};

// Call once after bgfx::init (and again after a device was re-created). The result is logged once (info).
Support DetectSupport(const RendererFacts &renderer);

// Typical limits of GPU families, for tests and for forcing a profile on a development device
// (engine option fx_caps_profile): the presets must constrain cleanly and report what they lose on each.
enum class CapsProfile : uint8_t
{
    Device,        // what DetectSupport found (no restriction)
    Gles30Minimal, // GLES 3.0 floor: no MSAA, no anisotropy, no vertex texture fetch of RGBA16F
    MaliG5x, // Mali-G52/G57, Adreno 6xx: MSAA yes, multisampled depth not resolvable (as after a failed self-test)
    PowerVR, // Rogue / IMG: no MSAA render targets in practice, no anisotropy
    Count,
};
// `device` restricted to the profile (a profile never adds what the device lacks).
Support RestrictSupport(const Support &device, CapsProfile profile);
const char *CapsProfileName(CapsProfile profile);

} // namespace storm::fx
