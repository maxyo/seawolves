// What GEOM tells the renderer about an object that bends in the wind (render::SetEffectsSway in effects.hpp).
#pragma once

#include <cstdint>

namespace storm::render
{

// How the vertices of the draws that follow bend in the wind (renderer_bgfx README, "Wind"); null: they stand still.
// Plant and Tent: a game's marks (its material's animation, the vertex colours and second texture coordinates).
// Height: a game without marks, a plant told by its texture (with Settings::sway_guess), bent by its height above the
// object's lowest point (model space, y up).
struct EffectsSway
{
    enum class Kind : uint8_t
    {
        Plant,
        Tent,
        Height,
    };
    Kind kind = Kind::Plant;
    float base = 0.0f, height = 0.0f; // Height: the object's lowest y and its extent
};

} // namespace storm::render
