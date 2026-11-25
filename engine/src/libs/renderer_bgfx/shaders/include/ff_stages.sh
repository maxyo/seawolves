// The fixed-function texture stages (ff_fragment.sh): their uniforms and samplers, the town light ratio and the stage
// interpreter FF_STAGE. STAGEn_SAMPLE(tex) defined before ff_fragment.sh selects a stage's sampler type (2D otherwise).

// per stage i: [3i] colour op, arg1, arg2, arg0   [3i+1] alpha op, arg1, arg2, arg0
//              [3i+2] x: result goes to TEMP  y: projected  z: texture bound (1 + its fade share)  w: point sprite
uniform vec4 u_ffStage[15];
// A specialised program's (FF_SETUP) only stage uniform: x, y the [3i+2].z of stages 0 and 1, the ones that fade.
uniform vec4 u_ffFade;
SAMPLER2D(s_tex0, 0);
SAMPLER2D(s_tex1, 1);
SAMPLER2D(s_tex2, 2);
SAMPLER2D(s_tex3, 3);
SAMPLER2D(s_tex4, 4);
#ifdef STAGE0_CUBE
SAMPLERCUBE(s_cube0, 5);
#endif
#ifdef STAGE1_CUBE
SAMPLERCUBE(s_cube1, 6);
#endif
#ifdef STAGE2_CUBE
SAMPLERCUBE(s_cube2, 7);
#endif

// The town light (src/effects/town_light.hpp): the stage that samples a town's lightmap multiplies it by the hour's sun
// over the bake's, a light ratio map (fx/sun_relight.hpp) the shim binds at a fixed-function unit the draw leaves
// free, read with the lightmap's own coordinates. x: the lightmap's stage (-1: none), y: the ratio's unit (2, 3, 4),
// z: its weight (0 -> 1 while it fades in), w: 0.
uniform vec4 u_ffTownRatio;
// The ratio map's texel at the lightmap's coordinates: read once per pixel in main() (FF_TOWN_RATIO_READ), not in every
// stage. Expanded in each of the five stages it was 15 texture reads in branches of the fixed-function fragment
// programs, the largest the game has.
float ffTownRatioTexel(vec2 uv)
{
    if (u_ffTownRatio.y < 2.5)
        return texture2D(s_tex2, uv).r;
    if (u_ffTownRatio.y < 3.5)
        return texture2D(s_tex3, uv).r;
    return texture2D(s_tex4, uv).r;
}
vec3 ffTownRatio(vec3 lightmap, float e)
{
    // fx/sun_relight.hpp: 3^((255 e - 127) / 127).
    float ratio = exp2((e * 255.0 - 127.0) * (1.5849625 / 127.0));
    return min(lightmap * mix(1.0, ratio, u_ffTownRatio.z), vec3_splat(1.0));
}

// An argument by its decoded fields: the D3DTA selector (0 diffuse .. 5 temp), complement, alpha replicate (0 / 1).
// diffuse: FF_DIFFUSE; specular: v_color1 (stage inputs, passed in: storm_common.sh).
vec4 ffArgSel(float sel, float complement, float alphaRep, vec4 current, vec4 tex, vec4 temp, vec4 diffuse,
              vec4 specular)
{
    vec4 r;
    if (sel < 0.5) r = diffuse;
    else if (sel < 1.5) r = current;
    else if (sel < 2.5) r = tex;
    else if (sel < 3.5) r = u_ffFs[0];
    else if (sel < 4.5) r = specular;
    else r = temp;
    if (complement > 0.5) r = vec4_splat(1.0) - r;
    if (alphaRep > 0.5) r = r.aaaa;
    return r;
}
// An argument as the shim encodes it: selector + 16 * (complement + 2 * alpha replicate).
vec4 ffArg(float arg, vec4 current, vec4 tex, vec4 temp, vec4 diffuse, vec4 specular)
{
    float flags = floor(arg / 16.0);
    float alphaRep = floor(flags / 2.0);
    return ffArgSel(arg - flags * 16.0, flags - 2.0 * alphaRep, alphaRep, current, tex, temp, diffuse, specular);
}

vec4 ffCombine(float op, vec4 a1, vec4 a2, vec4 a0, vec4 current, vec4 tex, vec4 diffuse)
{
    if (op < 2.5) return a1;
    if (op < 3.5) return a2;
    if (op < 4.5) return a1 * a2;
    if (op < 5.5) return a1 * a2 * 2.0;
    if (op < 6.5) return a1 * a2 * 4.0;
    if (op < 7.5) return a1 + a2;
    if (op < 8.5) return a1 + a2 - vec4_splat(0.5);
    if (op < 9.5) return (a1 + a2 - vec4_splat(0.5)) * 2.0;
    if (op < 10.5) return a1 - a2;
    if (op < 11.5) return a1 + a2 - a1 * a2;
    if (op < 12.5) return mix(a2, a1, diffuse.a);
    if (op < 13.5) return mix(a2, a1, tex.a);
    if (op < 14.5) return mix(a2, a1, u_ffFs[0].a);
    if (op < 15.5) return a1 + a2 * (1.0 - tex.a);
    if (op < 16.5) return mix(a2, a1, current.a);
    if (op < 17.5) return a1;
    if (op < 18.5) return vec4(a1.rgb + a1.a * a2.rgb, a1.a);
    if (op < 19.5) return vec4(a1.rgb * a2.rgb + a1.aaa, a1.a);
    if (op < 20.5) return vec4((1.0 - a1.a) * a2.rgb + a1.rgb, a1.a);
    if (op < 21.5) return vec4((vec3_splat(1.0) - a1.rgb) * a2.rgb + a1.aaa, a1.a);
    if (op < 23.5) return a1;
    if (op < 24.5) return vec4_splat(clamp(4.0 * dot(a1.rgb - vec3_splat(0.5), a2.rgb - vec3_splat(0.5)), 0.0, 1.0));
    if (op < 25.5) return a0 + a1 * a2;
    return mix(a2, a1, a0);
}

// The stage setups that draw most of the game's pixels, recognised by their codes (each case keeps the interpreter's
// arithmetic for its operation and arguments): a stage that matches skips the interpreter's choice of them.
bool ffCommonStage(int i, vec4 cs, vec4 as, vec4 current, vec4 tex, vec4 diffuse, out vec4 r)
{
    r = current;
    bool alphaKeeps = as.x < 1.5;                                    // alpha disabled: current's
    bool alphaModulates = as.x == 4.0 && as.y == 0.0 && as.z == 2.0; // diffuse * texture
    if (i == 0 && cs.x == 5.0 && cs.y == 0.0 && cs.z == 2.0 && alphaModulates)
        r = vec4((diffuse * tex * 2.0).rgb, (diffuse * tex).a); // 2x diffuse * texture
    else if (i == 0 && cs.x == 4.0 && cs.y == 2.0 && cs.z == 0.0 && alphaKeeps)
        r = vec4((tex * diffuse).rgb, current.a); // texture * diffuse
    else if (i == 0 && cs.x == 3.0 && cs.z == 2.0 && alphaModulates)
        r = vec4(tex.rgb, (diffuse * tex).a); // the texture (a lightmap's first stage)
    else if (i == 1 && cs.x == 14.0 && cs.y == 2.0 && cs.z == 1.0 && alphaKeeps)
        r = vec4(mix(current, tex, u_ffFs[0].a).rgb, current.a); // texture over current by the factor's alpha
    else if (i == 1 && cs.x == 5.0 && cs.y == 1.0 && cs.z == 2.0 && alphaKeeps)
        r = vec4((current * tex * 2.0).rgb, current.a); // 2x current * texture (the lightmap)
    else if (i == 2 && cs.x == 5.0 && cs.y == 1.0 && cs.z == 0.0 && alphaKeeps)
        r = vec4((current * diffuse * 2.0).rgb, current.a); // 2x current * diffuse
    else
        return false;
    r = clamp(r, 0.0, 1.0);
    return true;
}

// A stage's rows: the uniforms, or with FF_SETUP the constants of one setup (ff_setups.sh, generated from
// shaders/ff_programs.cmake), so the compiler folds the interpreter's choices away. The fade shares stay uniforms:
// a texture fading in must not change the program. Stages 2 to 4 never fade (FF_NO_FADE): their z is only the bound
// flag, a constant of the setup.
#ifdef FF_SETUP
#include "ff_setups.sh"
#define FF_SETUP_Z_0 u_ffFade.x
#define FF_SETUP_Z_1 u_ffFade.y
#define FF_SETUP_Z_2 (FF_S2_BOUND ? 1.0 : 0.0)
#define FF_SETUP_Z_3 (FF_S3_BOUND ? 1.0 : 0.0)
#define FF_SETUP_Z_4 (FF_S4_BOUND ? 1.0 : 0.0)
#define FF_STAGE_LIVE(i) true
#define FF_STAGE_COLOUR(i) FF_S##i##_COLOUR
#define FF_STAGE_ALPHA(i) FF_S##i##_ALPHA
#define FF_STAGE_MAP(i) vec4(FF_S##i##_TEMP, FF_S##i##_PROJECTED, FF_SETUP_Z_##i, 0.0)
#define FF_STAGE_BOUND(i, ms) FF_S##i##_BOUND
#define FF_STAGE_ARG(i, row, k) ffArgSel(FF_S##i##_##k, current, tex, temp, FF_DIFFUSE, v_color1)
#else
#define FF_STAGE_LIVE(i) (float(i) < u_ffFs[3].z)
#define FF_STAGE_COLOUR(i) u_ffStage[3 * i]
#define FF_STAGE_ALPHA(i) u_ffStage[3 * i + 1]
#define FF_STAGE_MAP(i) u_ffStage[3 * i + 2]
#define FF_STAGE_BOUND(i, ms) (ms.z > 0.5)
#define FF_STAGE_ARG(i, row, k) ffArg(row, current, tex, temp, FF_DIFFUSE, v_color1)
// The stages the town ratio's lightmap can be at.
#define FF_RATIO_COORDS(k, ttc, tms) \
    vec4 ttc = k < 0.5 ? v_texcoord0 : k < 1.5 ? v_texcoord1 : k < 2.5 ? v_texcoord2 : k < 3.5 ? v_texcoord3 : v_texcoord4; \
    vec4 tms = k < 0.5 ? FF_STAGE_MAP(0) : k < 1.5 ? FF_STAGE_MAP(1) : k < 2.5 ? FF_STAGE_MAP(2) : k < 3.5 ? FF_STAGE_MAP(3) : FF_STAGE_MAP(4);
#endif
// ms.z above 1: the stage's texture was swapped a moment ago (a finished transcode, its x2: src/shim/texture_fade.hpp)
// and the previous one, bound at the first unit the draw leaves free, still shows by ms.z - 1.
#define FF_FADE(tex, ms, PREV) if (ms.z > 1.0) { vec4 prev; PREV(prev) tex = mix(tex, prev, ms.z - 1.0); }
#define FF_NO_FADE(tex, ms, PREV)
// The town ratio's texel (ffTownRatioTexel) with the coordinates of the lightmap's stage u_ffTownRatio.x; stage inputs:
// only main() can name them in the SPIR-V build (storm_common.sh).
#define FF_TOWN_RATIO_READ(e) \
    float e = 1.0; \
    if (u_ffTownRatio.x > -0.5) \
    { \
        float k = u_ffTownRatio.x; \
        FF_RATIO_COORDS(k, ttc, tms) \
        e = ffTownRatioTexel(FF_UV(ttc, tms)); \
    }
#define FF_STAGE(i, SAMPLE, FADE, PREV) \
    if (running && FF_STAGE_LIVE(i)) \
    { \
        vec4 cs = FF_STAGE_COLOUR(i); \
        vec4 as = FF_STAGE_ALPHA(i); \
        vec4 ms = FF_STAGE_MAP(i); \
        vec4 tex = vec4(0.0, 0.0, 0.0, 1.0); \
        if (FF_STAGE_BOUND(i, ms)) \
        { \
            SAMPLE(tex) \
            FADE(tex, ms, PREV) \
            if (float(i) == u_ffTownRatio.x) tex.rgb = ffTownRatio(tex.rgb, townTexel); \
        } \
        FF_KEEP_TEX(i, tex) \
        vec4 r; \
        if (!ffCommonStage(i, cs, as, current, tex, FF_DIFFUSE, r)) \
        { \
            vec4 c = ffCombine(cs.x, FF_STAGE_ARG(i, cs.y, C1), FF_STAGE_ARG(i, cs.z, C2), FF_STAGE_ARG(i, cs.w, C0), \
                               current, tex, FF_DIFFUSE); \
            vec4 a = as.x < 1.5 ? current : ffCombine(as.x, FF_STAGE_ARG(i, as.y, A1), FF_STAGE_ARG(i, as.z, A2), \
                                                      FF_STAGE_ARG(i, as.w, A0), current, tex, FF_DIFFUSE); \
            r = clamp(vec4(c.rgb, cs.x > 23.5 && cs.x < 24.5 ? c.a : a.a), 0.0, 1.0); \
        } \
        if (ms.x > 0.5) temp = r; else current = r; \
    } \
    else { running = false; }

#ifdef FX_FF_PIXEL_LIGHTING
#define FF_KEEP_TEX(i, tex) if (i == 0) tex0 = tex;
#else
#define FF_KEEP_TEX(i, tex)
#endif

// ms.w: point-sprite draw (D3DRS_POINTSPRITEENABLE on a point list only). The vertex shader supplies the
// window-space centre and pixel size for every stage. Reconstruct the generated coordinates because bgfx's
// SPIR-V/HLSL profile does not expose gl_PointCoord. The centre includes inset/scaled viewport placement.
#define FF_POINT_UV(tc) ((gl_FragCoord.xy - (tc).xy) / max((tc).z, 1e-6) + vec2_splat(0.5))
#define FF_UV(tc, ms) ((ms).w > 0.5 ? FF_POINT_UV(tc) : (ms).y > 0.5 ? (tc).xy / (tc).w : (tc).xy)

// A stage's read is a statement that assigns its texel to `tex`: the SPIR-V build compiles `c ? read : read` as HLSL,
// which executes both reads, so a choice between two reads is an if / else.
#ifdef FX_LIGHT_PARALLAX
// The parallax variant is only chosen for a relief stage without projected coordinates (effects/lighting.cpp): that
// stage (u_fxFf.y) is read where the eye's ray meets the relief, the other as it is.
#ifndef STAGE0_SAMPLE
#define STAGE0_SAMPLE(tex) \
    if (u_fxFf.y > 0.5) tex = texture2D(s_tex0, FF_UV(v_texcoord0, ms)); \
    else tex = texture2DGrad(s_tex0, ffReliefUv, ffReliefDx, ffReliefDy);
#endif
#ifndef STAGE1_SAMPLE
#define STAGE1_SAMPLE(tex) \
    if (u_fxFf.y > 0.5) tex = texture2DGrad(s_tex1, ffReliefUv, ffReliefDx, ffReliefDy); \
    else tex = texture2D(s_tex1, FF_UV(v_texcoord1, ms));
#endif
#else
#ifndef STAGE0_SAMPLE
#define STAGE0_SAMPLE(tex) tex = texture2D(s_tex0, FF_UV(v_texcoord0, ms));
#endif
#ifndef STAGE1_SAMPLE
#define STAGE1_SAMPLE(tex) tex = texture2D(s_tex1, FF_UV(v_texcoord1, ms));
#endif
#endif
#ifndef STAGE2_SAMPLE
#define STAGE2_SAMPLE(tex) tex = texture2D(s_tex2, FF_UV(v_texcoord2, ms));
#endif
// The previous texture of a fading stage 0 / 1 (FF_FADE): stage 0's at unit 4, stage 1's at unit 3, read as the stage.
#ifdef FX_LIGHT_PARALLAX
#define STAGE0_PREV(tex) \
    if (u_fxFf.y > 0.5) tex = texture2D(s_tex4, FF_UV(v_texcoord0, ms)); \
    else tex = texture2DGrad(s_tex4, ffReliefUv, ffReliefDx, ffReliefDy);
#define STAGE1_PREV(tex) \
    if (u_fxFf.y > 0.5) tex = texture2DGrad(s_tex3, ffReliefUv, ffReliefDx, ffReliefDy); \
    else tex = texture2D(s_tex3, FF_UV(v_texcoord1, ms));
#else
#define STAGE0_PREV(tex) tex = texture2D(s_tex4, FF_UV(v_texcoord0, ms));
#define STAGE1_PREV(tex) tex = texture2D(s_tex3, FF_UV(v_texcoord1, ms));
#endif
#define STAGE3_SAMPLE(tex) tex = texture2D(s_tex3, FF_UV(v_texcoord3, ms));
#define STAGE4_SAMPLE(tex) tex = texture2D(s_tex4, FF_UV(v_texcoord4, ms));
