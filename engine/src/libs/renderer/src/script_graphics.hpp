#pragma once

// The graphics options a game's scripts set through the Graphics_* natives (Caribbean Legend's options screen), as
// effects options over the preset. Free of the engine: tests/script_graphics runs it as it is. README.md, "Graphics
// options of the game's scripts".

#include "os_window.hpp"
#include "storm/render/effects.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace storm::render
{
// Each field as the scripts last set it; unset ones leave the preset's value.
struct ScriptGraphics
{
    std::optional<int> shadows;           // Graphics_SetShadows: 0 off, 1..3 the game's levels
    std::optional<int> lamp_shadows;      // Graphics_SetDynamicShadows: 0 off, 1..3 the shadow map's size
    std::optional<int> ambient_occlusion; // Graphics_SetSSAO: 0 off, else on
    std::optional<int> fxaa;              // Graphics_SetFXAA: 0 off, else its quality + 1
    std::optional<int> ssaa;              // Graphics_SetSSAA: 0 off, else its scale + 1
    std::optional<int> anisotropy;        // Graphics_PreviewAnisotropicFiltering, engine.ini anisotropic_filtering
    std::optional<float> temperature;     // Graphics_SetTemperature: -1 cool .. 1 warm
    std::optional<float> saturation;      // Graphics_SetSaturation: 0 grey .. 1 as it is .. 2
};

// The game's shadow levels 1..3 on our two sun shadow qualities; lamp shadows and occlusion only switch off (their
// sizes and levels are the preset's). The game's post anti-aliasing (FXAA) is our SMAA, its supersampling our MSAA
// (2 for its x2, 4 above).
inline std::vector<EffectsOption> ScriptGraphicsOptions(const ScriptGraphics &g)
{
    std::vector<EffectsOption> options;
    if (g.shadows)
        options.push_back({"fx_shadows", std::clamp(*g.shadows, 0, 2)});
    if (g.lamp_shadows == 0)
        options.push_back({"fx_lamp_shadows", 0});
    if (g.ambient_occlusion == 0)
        options.push_back({"fx_ao_volume", 0});
    if (g.fxaa || g.ssaa)
    {
        const int ssaa = g.ssaa.value_or(0);
        options.push_back({"fx_smaa", g.fxaa.value_or(0) > 0 && ssaa <= 0 ? 1 : 0});
        options.push_back({"fx_msaa", ssaa <= 0 ? 1 : ssaa == 1 ? 2 : 4});
    }
    if (g.anisotropy)
        options.push_back({"fx_anisotropy", std::clamp(*g.anisotropy, 1, 16)});
    if (g.temperature)
        options.push_back(
            {"fx_temperature", static_cast<int>(std::lround(std::clamp(*g.temperature, -1.0f, 1.0f) * 100.0f)) + 100});
    if (g.saturation)
        options.push_back(
            {"fx_saturation", static_cast<int>(std::lround(std::clamp(*g.saturation, 0.0f, 2.0f) * 100.0f))});
    return options;
}

// Registers the natives (the renderer's script library); engine.ini anisotropic_filtering, which they keep, applies.
void RegisterScriptGraphics();
// renderer.cpp: the scripts' options over the preset, under the developer's fx_* keys. Desktop only: on Android the
// app's graphics settings decide (logged once).
void ApplyScriptGraphics(const ScriptGraphics &graphics);
// The preset's anisotropic filtering level.
int PresetAnisotropy();

// The sizes the game offers of the display's modes: at least 800x600 at 60 Hz or more (or an unknown rate), each
// once, by width then height.
inline std::vector<std::pair<int, int>> ScriptDisplaySizes(std::span<const DisplayMode> modes)
{
    std::vector<std::pair<int, int>> sizes;
    for (const DisplayMode &mode : modes)
        if (mode.width >= 800 && mode.height >= 600 && (mode.refresh_hz >= 60 || mode.refresh_hz == 0))
            sizes.emplace_back(mode.width, mode.height);
    std::sort(sizes.begin(), sizes.end());
    sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());
    return sizes;
}
} // namespace storm::render
