#include "graphics_options.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>

namespace storm::render
{
namespace
{
using Out = std::vector<EffectsOption>;

constexpr int kToggle[] = {0, 1};
constexpr int kThree[] = {0, 1, 2};
constexpr int kFour[] = {0, 1, 2, 3};
constexpr int kAnisotropy[] = {1, 2, 4, 8, 16};
constexpr int kLampCounts[] = {0, 8, 16};
constexpr int kPointLights[] = {0, 4, 8};

void One(Out &out, const char *name, int value)
{
    out.push_back({name, value});
}

// The desktop's defaults are the presets' «Высокая» with the scene at the window's size, except where a row says.
constexpr GraphicsOption kOptions[] = {
    {"render_scale",
     OptionKind::Number,
     50,
     100,
     {},
     100,
     OptionTakes::Live,
     false,
     "the 3D scene's size, % of the window's height (FSR 1 upscale below 100)",
     [](int v, uint32_t height, Out &out) {
         One(out, "fx_render_height", v >= 100 ? 0 : static_cast<int>(std::lround(height * (v / 100.0))));
     }},
    {"full_screen", OptionKind::Toggle, 0, 1, kToggle, 1, OptionTakes::NextStart, false, "0 a window, 1 full screen",
     nullptr},
    {"vsync", OptionKind::Toggle, 0, 1, kToggle, 1, OptionTakes::Live, false, "0 off, 1 in step with the display",
     nullptr},
    {"anti_aliasing", OptionKind::Choice, 0, 3, kFour, 3, OptionTakes::Live, false,
     "0 off, 1 SMAA, 2 MSAA 2x, 3 MSAA 4x (MSAA with alpha to coverage for leaves and rigging)",
     [](int v, uint32_t, Out &out) {
         One(out, "fx_msaa", v == 3 ? 4 : v == 2 ? 2 : 1);
         One(out, "fx_smaa", v == 1);
         One(out, "fx_alpha_to_coverage", v >= 2);
     }},
    {"anisotropic_filtering", OptionKind::Choice, 1, 16, kAnisotropy, 16, OptionTakes::Live, false,
     "1 off, 2, 4, 8, 16: anisotropic filtering of the world's textures",
     [](int v, uint32_t, Out &out) { One(out, "fx_anisotropy", v); }},
    {"texture_quality", OptionKind::Choice, 0, 2, kThree, 2, OptionTakes::NextStart, false,
     "0 low, 1 medium, 2 high: the textures from their second mip, their first, their full size", nullptr},
    {"sun_shadows", OptionKind::Choice, 0, 2, kThree, 2, OptionTakes::Live, false, "0 off, 1 low, 2 soft",
     [](int v, uint32_t, Out &out) { One(out, "fx_shadows", v); }},
    {"lamp_shadows", OptionKind::Choice, 0, 16, kLampCounts, 16, OptionTakes::Live, false,
     "0, 8, 16: lamps around the camera that light with their shadows",
     [](int v, uint32_t, Out &out) { One(out, "fx_lamp_shadows", v); }},
    // Every lamp of a pixel takes a filtered shadow: an unfiltered one shows a hard line on a character between lamps.
    {"lamps_per_pixel",
     OptionKind::Number,
     1,
     8,
     {},
     3,
     OptionTakes::Live,
     false,
     "1..8: the most lamps that light one pixel with their shadows",
     [](int v, uint32_t, Out &out) {
         One(out, "fx_lamp_per_pixel", v);
         One(out, "fx_lamp_filtered", v);
     }},
    // «Высокая»'s level: the strong one left the rooms too dark.
    {"ambient_occlusion", OptionKind::Choice, 0, 3, kFour, 2, OptionTakes::Live, false,
     "interiors: 0 off, 1 as the game's data, 2 moderate, 3 strong",
     [](int v, uint32_t, Out &out) { One(out, "fx_ao_volume", v); }},
    {"point_lights", OptionKind::Choice, 0, 8, kPointLights, 8, OptionTakes::Live, false,
     "0 the game's own, 4, 8: lights per draw lit per pixel",
     [](int v, uint32_t, Out &out) {
         One(out, "fx_light_point", v > 0);
         if (v > 0)
             One(out, "fx_point_lights", v);
     }},
    {"flash_lights",
     OptionKind::Number,
     0,
     8,
     {},
     8,
     OptionTakes::Live,
     false,
     "0..8: shots and explosions that light the scene at once",
     [](int v, uint32_t, Out &out) { One(out, "fx_flash_lights", v); }},
    {"specular", OptionKind::Toggle, 0, 1, kToggle, 1, OptionTakes::Live, false,
     "0 off, 1 highlights of the sun and the lamps (shaped by gloss maps with the normal maps)",
     [](int v, uint32_t, Out &out) { One(out, "fx_light_specular", v); }},
    {"town_light", OptionKind::Toggle, 0, 1, kToggle, 1, OptionTakes::Live, false,
     "0 the game's town lightmaps, 1 towns relit for the live sun",
     [](int v, uint32_t, Out &out) {
         One(out, "fx_town_light", v);
         One(out, "fx_town_walls", v);
     }},
    {"aerial_perspective", OptionKind::Toggle, 0, 1, kToggle, 1, OptionTakes::Live, false,
     "0 the game's fog, 1 aerial perspective and height fog",
     [](int v, uint32_t, Out &out) { One(out, "fx_aerial", v); }},
    {"bloom", OptionKind::Toggle, 0, 1, kToggle, 1, OptionTakes::Live, false, "0 off, 1 on",
     [](int v, uint32_t, Out &out) { One(out, "fx_bloom", v); }},
    {"sun_shafts", OptionKind::Toggle, 0, 1, kToggle, 1, OptionTakes::Live, false, "0 off, 1 on (part of the bloom)",
     [](int v, uint32_t, Out &out) { One(out, "fx_sun_shafts", v); }},
    {"lens_flare", OptionKind::Toggle, 0, 1, kToggle, 1, OptionTakes::Live, false, "0 off, 1 the sun's glare",
     [](int v, uint32_t, Out &out) { One(out, "fx_lens_flare", v); }},
    {"tone_mapping", OptionKind::Toggle, 0, 1, kToggle, 1, OptionTakes::Live, false, "0 off, 1 a tone curve",
     [](int v, uint32_t, Out &out) { One(out, "fx_tonemap", v); }},
    {"colour_grade", OptionKind::Choice, 0, 2, kThree, 1, OptionTakes::Live, false,
     "0 the original, 1 Caribbean (restrained), 2 overcast",
     [](int v, uint32_t, Out &out) { One(out, "fx_grade", v); }},
    {"colour_temperature",
     OptionKind::Number,
     0,
     200,
     {},
     100,
     OptionTakes::Live,
     false,
     "0..200: 100 the grade's warmth, 0 the coolest, 200 the warmest",
     [](int v, uint32_t, Out &out) { One(out, "fx_temperature", v); }},
    {"colour_saturation",
     OptionKind::Number,
     0,
     200,
     {},
     100,
     OptionTakes::Live,
     false,
     "0..200 % of the grade's saturation",
     [](int v, uint32_t, Out &out) { One(out, "fx_saturation", v); }},
    {"sharpen", OptionKind::Toggle, 0, 1, kToggle, 1, OptionTakes::Live, false,
     "0 off, 1 FSR 1 sharpening after the upscale (render scale below 100)",
     [](int v, uint32_t, Out &out) { One(out, "fx_sharpen", v); }},
    {"sea_detail", OptionKind::Choice, 0, 2, kThree, 2, OptionTakes::Live, false,
     "0 the sea without the sun's glint, 1 the sun's glint, 2 also soft shores",
     [](int v, uint32_t, Out &out) {
         One(out, "fx_sea_specular", v >= 1);
         One(out, "fx_sea_soft_edges", v >= 2);
     }},
    {"sea_wakes", OptionKind::Toggle, 0, 1, kToggle, 1, OptionTakes::Live, false, "0 off, 1 foam astern and at the bow",
     [](int v, uint32_t, Out &out) { One(out, "fx_sea_wakes", v); }},
    // «Высокая»'s pixel: a simplification no larger than a pixel is not seen.
    {"far_lod",
     OptionKind::Number,
     0,
     4,
     {},
     1,
     OptionTakes::Live,
     false,
     "0 every model whole, 1..4 pixels a far model's simplified level may differ by",
     [](int v, uint32_t, Out &out) { One(out, "fx_far_lod", v * 10); }},
    {"wet_surfaces", OptionKind::Choice, 0, 2, kThree, 2, OptionTakes::Live, false,
     "0 off, 1 wet in the rain, 2 also puddles", [](int v, uint32_t, Out &out) { One(out, "fx_wet_surfaces", v); }},
    // Off until the owner has looked at a town and the jungle: a guess (renderer_bgfx README, "Wind").
    {"vegetation_sway", OptionKind::Toggle, 0, 1, kToggle, 0, OptionTakes::Live, true,
     "0 off, 1 plants of a game without wind marks bend in the wind by their height (a guess from their textures)",
     [](int v, uint32_t, Out &out) { One(out, "fx_sway_guess", v); }},
    // The three texture-generation switches are off as on phones: background GPU work that delays the first scenes,
    // with no desktop measurement to turn it on.
    {"texture_normal_maps", OptionKind::Toggle, 0, 1, kToggle, 0, OptionTakes::Live, true,
     "0 off, 1 relief from normal maps made of the game's textures",
     [](int v, uint32_t, Out &out) { One(out, "texture_normal_maps", v); }},
    {"texture_height_maps", OptionKind::Toggle, 0, 1, kToggle, 0, OptionTakes::Live, true,
     "0 off, 1 parallax from height maps made with the normal maps (drawn through them)",
     [](int v, uint32_t, Out &out) { One(out, "texture_height_maps", v); }},
    {"texture_upscale", OptionKind::Toggle, 0, 1, kToggle, 0, OptionTakes::Live, true,
     "0 off, 1 neural x2 textures near the camera", [](int v, uint32_t, Out &out) { One(out, "texture_upscale", v); }},
};

} // namespace

const char *GraphicsOptionKindName(OptionKind kind)
{
    return kind == OptionKind::Toggle ? "toggle" : kind == OptionKind::Choice ? "choice" : "number";
}

std::span<const GraphicsOption> GraphicsOptions()
{
    return kOptions;
}

const GraphicsOption *FindGraphicsOption(std::string_view name)
{
    for (const GraphicsOption &option : kOptions)
        if (name == option.name)
            return &option;
    return nullptr;
}

bool GraphicsOptionAllows(const GraphicsOption &option, int value)
{
    if (option.kind == OptionKind::Number)
        return value >= option.min && value <= option.max;
    return std::ranges::find(option.choices, value) != option.choices.end();
}

std::vector<EffectsOption> GraphicsOptionEffects(const GraphicsOption &option, int value, uint32_t display_height)
{
    std::vector<EffectsOption> out;
    if (option.effects)
        option.effects(value, display_height, out);
    return out;
}

std::string GraphicsOptionsMarkdown()
{
    std::string text = "| option | values | desktop default | applies |\n| --- | --- | --- | --- |\n";
    for (const GraphicsOption &option : kOptions)
        text += fmt::format("| `{}` | {}{} | {} | {} |\n", option.name, option.values,
                            option.generated_maps ? " (old games only)" : "", option.desktop,
                            option.takes == OptionTakes::Live ? "at once" : "next start");
    return text;
}
} // namespace storm::render
