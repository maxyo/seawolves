#include "storm_efk_shaders.hpp"

#include "storm/render/shader_blob.hpp"

#include <string>

#include "efk_model_adv_distortion/includes.inc"
#include "efk_model_adv_lit/includes.inc"
#include "efk_model_adv_unlit/includes.inc"
#include "efk_model_distortion/includes.inc"
#include "efk_model_lit/includes.inc"
#include "efk_model_unlit/includes.inc"
#include "efk_sprite_adv_distortion/includes.inc"
#include "efk_sprite_adv_lit/includes.inc"
#include "efk_sprite_adv_unlit/includes.inc"
#include "efk_sprite_distortion/includes.inc"
#include "efk_sprite_lit/includes.inc"
#include "efk_sprite_unlit/includes.inc"

namespace EffekseerRendererBGFX
{
namespace
{
struct Embedded
{
    const char *name;
    storm::render::ShaderBlob blob;
};
#define STORM_SHADER(name) {#name, STORM_SHADER_BLOB(name)},
const Embedded kShaders[] = {
#include "efk_model_adv_distortion/table.inc"
#include "efk_model_adv_lit/table.inc"
#include "efk_model_adv_unlit/table.inc"
#include "efk_model_distortion/table.inc"
#include "efk_model_lit/table.inc"
#include "efk_model_unlit/table.inc"
#include "efk_sprite_adv_distortion/table.inc"
#include "efk_sprite_adv_lit/table.inc"
#include "efk_sprite_adv_unlit/table.inc"
#include "efk_sprite_distortion/table.inc"
#include "efk_sprite_lit/table.inc"
#include "efk_sprite_unlit/table.inc"
};
#undef STORM_SHADER
} // namespace

uint16_t LoadShader(const char *name, bool vertex)
{
    std::string blob = vertex ? "vs_efk_" : "fs_efk_";
    const std::string shader = name;
    blob += !vertex && shader.starts_with("sprite_") ? "model_" + shader.substr(7) : shader;
    for (const Embedded &embedded : kShaders)
        if (blob == embedded.name)
            return storm::render::CreateShader(embedded.blob, embedded.name).idx;
    return bgfx::kInvalidHandle;
}
} // namespace EffekseerRendererBGFX
