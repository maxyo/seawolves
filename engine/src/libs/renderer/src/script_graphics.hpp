#pragma once

// The graphics options a game's scripts set through the Graphics_* natives (Caribbean Legend's options screen), as
// the engine's graphics options (graphics_options.hpp). Free of the engine: tests/script_graphics runs it as it is.
// README.md, "Graphics options of the game's scripts".

#include "graphics_options.hpp"
#include "os_window.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace storm::render
{
// Each field as the scripts last set it; unset ones leave the option as it is.
struct ScriptGraphics
{
    std::optional<int> shadows;           // Graphics_SetShadows: 0 off, 1..3 the game's levels
    std::optional<int> lamp_shadows;      // Graphics_SetDynamicShadows: 0 off, 1..3 the shadow map's size
    std::optional<int> ambient_occlusion; // Graphics_SetSSAO: 0 off, else on
    std::optional<int> fxaa;              // Graphics_SetFXAA: 0 off, else its quality + 1
    std::optional<int> ssaa;              // Graphics_SetSSAA: 0 off, else its scale + 1
    bool temporal = false;                // Graphics_SetTAA, Graphics_SetDLSS: its TAA (its default) or DLAA asked
    std::optional<int> anisotropy;        // Graphics_(Preview)AnisotropicFiltering: 0 off, else the level
    std::optional<float> temperature;     // Graphics_SetTemperature: -1 cool .. 1 warm
    std::optional<float> saturation;      // Graphics_SetSaturation: 0 grey .. 1 as it is .. 2
};

// The game's anti-aliasing by what each of its modes is for, as the anti_aliasing option. Its TAA (its default) and
// DLAA smooth every edge of the scene, the alpha-tested leaves and the rigging among them: our MSAA 4x with alpha to
// coverage does that, so it stands in for them. Its FXAA, a pass over the finished picture, is our SMAA. Its
// supersampling is MSAA, 2x for its Normal and 4x above. Off is off.
inline int ScriptAntiAliasingOf(const ScriptGraphics &g)
{
    if (g.temporal)
        return 3;
    if (g.fxaa.value_or(0) > 0)
        return 1;
    if (const int ssaa = g.ssaa.value_or(0); ssaa > 0)
        return ssaa == 1 ? 2 : 3;
    return 0;
}

struct ScriptOptionValue
{
    const char *option;
    int value;
    bool operator==(const ScriptOptionValue &) const = default;
};

inline int DesktopDefault(const char *option)
{
    return FindGraphicsOption(option)->desktop;
}

// The game's shadow levels 1..3 on our two sun shadow qualities; its lamp shadow sizes and occlusion levels are ours
// (the defaults), only off is its own. Anti-aliasing: ScriptAntiAliasingOf; anisotropy: the nearest level not below.
inline std::vector<ScriptOptionValue> ScriptGraphicsOptions(const ScriptGraphics &g)
{
    std::vector<ScriptOptionValue> options;
    if (g.shadows)
        options.push_back({"sun_shadows", std::clamp(*g.shadows, 0, 2)});
    if (g.lamp_shadows)
        options.push_back({"lamp_shadows", *g.lamp_shadows == 0 ? 0 : DesktopDefault("lamp_shadows")});
    if (g.ambient_occlusion)
        options.push_back({"ambient_occlusion", *g.ambient_occlusion == 0 ? 0 : DesktopDefault("ambient_occlusion")});
    if (g.fxaa || g.ssaa || g.temporal)
        options.push_back({"anti_aliasing", ScriptAntiAliasingOf(g)});
    if (g.anisotropy)
    {
        const auto levels = FindGraphicsOption("anisotropic_filtering")->choices;
        const auto level = std::ranges::find_if(levels, [&](int l) { return l >= *g.anisotropy; });
        options.push_back({"anisotropic_filtering", level == levels.end() ? levels.back() : *level});
    }
    if (g.temperature)
        options.push_back({"colour_temperature",
                           static_cast<int>(std::lround(std::clamp(*g.temperature, -1.0f, 1.0f) * 100.0f)) + 100});
    if (g.saturation)
        options.push_back(
            {"colour_saturation", static_cast<int>(std::lround(std::clamp(*g.saturation, 0.0f, 2.0f) * 100.0f))});
    return options;
}

// Registers the natives (the renderer's script library).
void RegisterScriptGraphics();

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
