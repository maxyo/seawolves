#include "storm/fx/lighting_model.hpp"

#include "storm/fx/color_lut.hpp"

#include <algorithm>
#include <cmath>

namespace storm::fx
{

LightingMaterial LightingMaterial::Stone()
{
    return {16.0f, 0.05f, 0.0f, 0.0f, 1.0f, 0.0f};
}

LightingMaterial LightingMaterial::Hull()
{
    return {32.0f, 0.22f, 0.0f, 0.0f, 1.0f, 0.3f};
}

LightingMaterial LightingMaterial::Sail()
{
    return {8.0f, 0.04f, 0.8f, 0.2f, 0.35f, 0.3f};
}

LightingMaterial LightingMaterial::Character()
{
    // Matte like the original fixed-function characters: no highlight of the material (a white sheen on skin and cloth
    // read as glaze in the sun; their gloss maps' capped one stays off skin), a faint rim. The relief at full scale:
    // its strength on characters is Settings::character_relief_strength (effects lighting.cpp), faded out on skin.
    return {20.0f, 0.0f, 0.0f, 0.2f, 1.0f, 0.2f};
}

LightingUniforms ComputeLighting(const LightingInput &input, const LightingMaterial &material, const SkyState *sky,
                                 const Settings &settings)
{
    const auto to_view = [&](glm::vec3 v) {
        glm::vec3 r;
        for (int j = 0; j < 3; ++j)
            r[j] = v[0] * input.view[j] + v[1] * input.view[4 + j] + v[2] * input.view[8 + j];
        const float len = glm::length(r);
        return len > 1e-6f ? r / len : glm::vec3{0.0f, 1.0f, 0.0f};
    };
    const glm::vec3 sun = to_view(input.sun_dir);
    const glm::vec3 up = to_view(glm::vec3{0.0f, 1.0f, 0.0f});

    // Hemisphere: brighter, sky-tinted light from above and dimmer, warmer light from below. Per channel the two
    // average to the flat ambient whatever the sky's colour (a normal facing sideways keeps the flat ambient), so the
    // overall brightness of a scene does not change with the flag: above = ambient * (1 + k) * hue, below = ambient *
    // (2 - (1 + k) * hue). The dome's hue is clamped so that below stays positive (a deep blue night sky).
    constexpr float kContrast = 0.3f;
    constexpr float kHueMin = 0.6f, kHueMax = 1.4f; // (1 + kContrast) * kHueMax < 2
    glm::vec3 hue{1.0f, 1.0f, 1.0f};
    if (sky)
    {
        const float luma = std::max(glm::dot(sky->dome, kLumaRec709), 1e-4f);
        // gamma-space hue of the dome, half strength
        hue = glm::clamp(0.5f + 0.5f * glm::sqrt(glm::max(sky->dome, 0.0f) / luma), kHueMin, kHueMax);
    }
    LightingUniforms u{};
    u.u_fxLight[0] = {sun[0], sun[1], sun[2], material.specular_power};
    u.u_fxLight[1] = {input.sun_diffuse[0], input.sun_diffuse[1], input.sun_diffuse[2], material.specular_strength};
    u.u_fxLight[2] = {up[0], up[1], up[2], material.translucency * std::max(settings.sail_light, 0.0f)};
    for (int i = 0; i < 3; ++i)
    {
        u.u_fxLight[3][i] = input.ambient[i] * (1.0f + kContrast) * hue[i];
        u.u_fxLight[4][i] = input.ambient[i] * (2.0f - (1.0f + kContrast) * hue[i]);
    }
    u.u_fxLight[3][3] = material.wrap;
    // The run-time switches of the programs (fx_lighting.sh): the soft sun shadow filter and linear light.
    u.u_fxLight[4][3] = settings.shadows == ShadowQuality::Soft ? 1.0f : 0.0f;
    u.u_fxLight[5] = {material.relief * std::clamp(settings.normal_map_strength, 0.0f, 1.0f), material.rim,
                      1.0f - std::clamp(settings.light_saturation, 0.0f, 1.0f),
                      (settings.lighting_flags & kLightingLinear) ? 1.0f : 0.0f};
    return u;
}

int PointLightBudget(const Settings &settings)
{
    if (!(settings.lighting_flags & kLightingPoint))
        return 0;
    return settings.point_lights > 4 ? 8 : 4;
}

} // namespace storm::fx
