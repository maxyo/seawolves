// The hulls' wakes on the Sea2 pixel shader (effects::Host::prepare_sea sets everything here): the target of
// fx::SeaWakes (fx/include/storm/fx/sea_wakes.hpp) around the camera, the churned water of the hulls (R foam, G how
// much it calms the sea, 0..1): foam and a smoother band on the pixels. Positions are those of the sea's vertices
// (a_position: world minus the sea's own camera offset, small numbers).
#ifndef SEA_WAKES_SH
#define SEA_WAKES_SH
#include "sea_wakes_math.sh"

SAMPLER2D(s_seaWake, 12); // the shim's stage-3 volume unit: Sea2's stage 3 is a cube (unit 11)
// [0] xy: wake target uv at the sea's vertex origin, z: 1 / metres it covers, w: 1 = wakes drawn this frame
// [1] xy: foam texture uv at the sea's vertex origin, z: foam texture repeats per metre, w: 0
// [2] rgb: the sun's light on the level sea (its colour times the sine of its height; 0 while clouds hide it)
uniform vec4 u_seaWake[3];

// The wakes at `pos` (the sea's vertex position: world minus its vertex origin): x the foam of the churned water
// behind and beside the hulls, y how much that water calms the sea's own small waves (0..1); zero where the target
// holds none or ends.
vec2 seaWake(vec2 pos)
{
    if (u_seaWake[0].w <= 0.0)
        return vec2_splat(0.0);
    vec2 uv = pos * u_seaWake[0].z + u_seaWake[0].xy;
    return texture2DLod(s_seaWake, uv, 0.0).rg * seaWakeReach(uv.x, uv.y);
}

// The foam texture's uv at `pos`: in world metres (the sea's vertex origin moves with the camera).
vec2 seaWakeFoamUv(vec2 pos)
{
    return pos * u_seaWake[1].z + u_seaWake[1].xy;
}

#endif
