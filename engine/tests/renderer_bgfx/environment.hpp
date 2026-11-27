// The scene environment the renderer checks set (storm/render/effects.hpp SetSceneEnvironment): a white light, a grey
// fill and a blue-grey fog, no sun above the horizon until a check gives one.
#pragma once

#include "storm/environment/state.hpp"

inline storm::environment::State CheckEnvironment()
{
    storm::environment::State state;
    state.sun_dir = {0.0f, -1.0f, 0.0f};
    state.light_is_sun = false;
    state.light_color = glm::vec3(1.0f);
    state.ambient = glm::vec3(0.16f);        // 0.4 gamma-encoded
    state.fog_color = {0.25f, 0.36f, 0.49f}; // 0.5, 0.6, 0.7
    state.fog_density = 0.003f;
    state.wind_speed = 5.0f;
    return state;
}

// The sun towards `dir` above the horizon, lighting and casting.
inline storm::environment::State SunlitEnvironment(const glm::vec3 &dir)
{
    storm::environment::State state = CheckEnvironment();
    state.sun_dir = state.light_dir = dir;
    state.light_is_sun = true;
    return state;
}
