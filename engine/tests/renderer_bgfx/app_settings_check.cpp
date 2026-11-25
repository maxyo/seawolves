// The effects take every graphics key the application stores for them (tools/android/app_contract.json): each fx_*
// debug override is an option the renderer asks the application for (EffectsOptionNames) and is taken with each value
// the settings screen offers, the player's texture switches too, without an error.
#include "app_ui_names.hpp"
#include "storm/render/effects.hpp"

#include <algorithm>
#include <cstdio>
#include <string_view>

int main()
{
    int failures = 0;
    const auto take = [&](const char *key, int value) {
        for (const std::string &error : storm::render::ConfigureEffects(2, {{key, value}}).errors)
        {
            std::fprintf(stderr, "%s = %d: %s\n", key, value, error.c_str());
            ++failures;
        }
    };
    int overrides = 0;
    for (const auto &entry : storm::app::debug::all)
    {
        // The render_* overrides are read once at the renderer's start (renderer.cpp), not by the effects.
        if (!std::string_view(entry.key).starts_with("fx_"))
            continue;
        ++overrides;
        const auto &names = storm::render::EffectsOptionNames();
        if (std::none_of(names.begin(), names.end(),
                         [&](const auto &info) { return info.name == std::string_view(entry.key); }))
        {
            std::fprintf(stderr, "%s: not an effects option, the renderer never asks for it\n", entry.key);
            ++failures;
        }
        for (const int value : entry.values)
            if (value >= 0)
                take(entry.key, value);
    }
    for (const char *key : {storm::app::setting::texture_normal_maps, storm::app::setting::texture_height_maps,
                            storm::app::setting::texture_upscale})
        for (const int value : {0, 1})
            take(key, value);
    if (failures)
        return 1;
    std::printf("PASS app settings: %d fx overrides and the texture switches are effects options\n", overrides);
}
