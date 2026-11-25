// The sun's shadow maps (fx/shadows.hpp): one D16 array on sampler stage 15 (fx::kShadowMapStage), layer 0 the
// place's still geometry (drawn when it changes), layer 1 the moving casters (every frame). One projection: the
// dynamic layer's uv is the static one's scaled and shifted. Included by fx_lighting.sh (FX_LIGHT_SHADOW: lit
// receivers read both layers) and by the fixed-function ground variant (fs_ff_dyn: the moving casters only).
#ifndef FX_SUN_MAP_SH
#define FX_SUN_MAP_SH

SAMPLER2DARRAYSHADOW(s_fxShadowMap, 15);
uniform mat4 u_fxShadowMtx; // view space -> the static layer's uv and depth
// [0] x: one texel in uv, y: shadow strength, z: the static layer's normal offset in view units, w: its depth bias
// [1] x, yz: the dynamic layer's uv = static uv * x + yz, w: its depth bias for receivers that cast into it
// [2] x: the dynamic layer's normal offset, y: the static layer holds casters, z: a moving caster's shadow can
//     reach this draw, w: facing scale (the light-space depth of a unit normal times it: 1 facing the sun)
// [3] ground receivers. x: the surface's light without the sun over its light with it, y: depth step of the dynamic
//     layer, z: 1 = this draw is a ground receiver (baked variants), w: unused
uniform vec4 u_fxShadow[4];

// Shadows fade out over the last tenth of the dynamic layer's square (the shadowed region) and of the depth range.
float fxSunInside(vec2 uvDynamic, float depth)
{
    vec2 edge = abs(uvDynamic - vec2_splat(0.5)) * 2.0;
    return clamp((1.0 - max(max(edge.x, edge.y), depth)) * 10.0, 0.0, 1.0);
}

// The moving casters' layer at `uv`, `depth`: 1 = lit. A 3x3 texel tent of depth comparisons from four hardware-filtered
// taps, weighted so their bilinear footprints add up to the tent: its edge runs over two texels without stair steps (one
// tap's edge spans one texel and shows the texels' squares along a character's or a ship's outline).
float fxSunDynamic(vec2 uv, float depth)
{
    float texel = u_fxShadow[0].x; // both layers have the same size
    vec2 t = uv / texel + vec2_splat(0.5);
    vec2 base = floor(t);
    vec2 s = t - base;
    base = (base - vec2_splat(0.5)) * texel;
    vec2 w0 = vec2_splat(3.0) - 2.0 * s;
    vec2 w1 = vec2_splat(1.0) + 2.0 * s;
    vec2 o0 = ((vec2_splat(2.0) - s) / w0 - vec2_splat(1.0)) * texel;
    vec2 o1 = (s / w1 + vec2_splat(1.0)) * texel;
    float lit = w0.x * w0.y * shadow2DArray(s_fxShadowMap, vec4(base + vec2(o0.x, o0.y), 1.0, depth));
    lit += w1.x * w0.y * shadow2DArray(s_fxShadowMap, vec4(base + vec2(o1.x, o0.y), 1.0, depth));
    lit += w0.x * w1.y * shadow2DArray(s_fxShadowMap, vec4(base + vec2(o0.x, o1.y), 1.0, depth));
    lit += w1.x * w1.y * shadow2DArray(s_fxShadowMap, vec4(base + vec2(o1.x, o1.y), 1.0, depth));
    return lit * (1.0 / 16.0);
}

// A surface that holds its own static light (a lightmap, baked colours) under the moving casters: 1 = untouched.
// `c`: its static layer uv and depth (no offset: it is not in the dynamic layer, so it cannot shadow itself);
// `facing`: 0 turned away from the sun .. 1. The shadow takes the sun's share of the surface's light
// (u_fxShadow[3].x: what is left) wherever it falls, over the shade the surface's own light already shows.
float fxSunGround(vec3 c, float facing)
{
    if (u_fxShadow[2].z < 0.5)
        return 1.0;
    vec2 uvDynamic = c.xy * u_fxShadow[1].x + u_fxShadow[1].yz;
    // Outside the shadowed region or turned away from the sun no shadow falls: the layer is not read.
    float reach = fxSunInside(uvDynamic, c.z) * u_fxShadow[0].y * facing;
    if (reach <= 0.0)
        return 1.0;
    float dark = 1.0 - fxSunDynamic(uvDynamic, c.z - u_fxShadow[3].y);
    dark *= reach;
    return 1.0 - dark * (1.0 - u_fxShadow[3].x);
}

#endif
