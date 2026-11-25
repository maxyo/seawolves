#include "storm/fx/capabilities.hpp"

#include "gpu_util.hpp"
#include "storm/render/log.hpp"

#include <bgfx/bgfx.h>

namespace storm::fx
{

Support DetectSupport(const RendererFacts &renderer)
{
    Support support;
    support.lut_3d = detail::SupportsLut3d();
    support.shadow_map = detail::SupportsShadowMap();
    support.lamp_depth = detail::LampDepthFormat();
    support.normal_map = detail::SupportsNormalMap(bgfx::TextureFormat::EACRG11);
    support.ao_volume = detail::SupportsAoVolume();
    support.msaa_samples = detail::MsaaSamples();
    support.smaa = detail::SupportsSmaa();
    support.anisotropy = renderer.anisotropy;
    support.scene_depth = renderer.scene_depth;
    support.msaa_depth = renderer.scene_depth && renderer.msaa_depth;
    render::Log().info("fx: {}: {}", bgfx::getRendererName(bgfx::getCaps()->rendererType), support.Describe());
    return support;
}

} // namespace storm::fx
