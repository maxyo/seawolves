// The fixed-function fragment program of fs_ff*.sc: main() and the includes of its parts (the texture stages in
// ff_stages.sh; the lit and baked variants' volumes, lamps and town walls in ff_volumes.sh, ff_lamps.sh,
// ff_town_wall.sh).
//
// Graphics effects variants (src/effects/lighting.cpp picks them; none is used by the «Оригинал» preset):
//   FX_FF_LIT       per-pixel lighting of a lit draw (vs_ff_lit): the diffuse colour the stages read is
//                   re-lit per pixel with fx_lighting.sh (sun, hemisphere, point / spot lights, rim, sunlight through sails,
//                   specular with FX_LIGHT_SPECULAR); features the settings leave off arrive with zero strength.
//   FX_FF_BAKED     geometry with baked vertex colours (lighting off, vs_ff_lit): dynamic point lights are added,
//                   as texture * light.
// Product blocks of the lit and baked families (shaders/ff_programs.cmake; ChooseProgram picks the narrowest family
// that holds every block a draw reads; a block's per-draw switch still turns it off in a family that has it):
//   FX_FF_LAMPS     the location's lamps with their shadows (ffLampLight, off per draw by u_fxLamps[0].x = 0)
//                   relight the baked colours and light the lit draws; lit draws of an interior take the room light
//                   as their ambient (ffRoomLight, u_fxLampGrid[2].w).
//   FX_FF_AO        the interior's ambient occlusion volume (ffAoVolume, off per draw by u_fxAoParams.x = 0): darkens
//                   the ambient of lit draws and the baked colours.
//   FX_FF_TOWN_WALL FX_FF_BAKED: a town's walls relit by our sun and sky (u_fxTownWall[0].w = 0: off for the draw).
//   FX_FF_WET       wet surfaces: in the rain, outdoor surfaces where the sky's rain reaches them (the visibility
//                   volume's openness: dry under roofs) are darker and glossier, their relief filled; the highlights
//                   of the sun and the lamps (FX_LIGHT_SPECULAR) take the wet gloss, the overcast sky is reflected;
//                   puddles in the relief's hollows (FX_LIGHT_PARALLAX). ff_wet.sh, off per draw by u_fxWet[0].x = 0.
// Every family reads the visibility volume (ffVisibility).
//   FX_LIGHT_NORMALMAP  with FX_FF_LIT / FX_FF_BAKED: relief from the normal map derived from stage 1's height texture
//                   or stage 0's colour texture (stage 14, that stage's coordinates: u_fxFf.y): lit draws perturb the
//                   normal, baked ones scale their colour.
//   FX_LIGHT_SPECULAR  with FX_FF_LIT / FX_FF_BAKED: highlights. With a gloss map of stage 0's texture (stage 7,
//                   fx/gloss_map.hpp, u_fxGloss.x its share while it fades in) each texel's gloss sets the highlight
//                   (fxGlossSpec), else the material's (fxMaterialSpec). u_fxGloss.w > 0 (Settings::gloss, its value the gloss strength) adds the
//                   lamps' highlights with their shadows (lit and baked) and the sun's on baked surfaces outdoors, where
//                   the bake shows the sun; u_fxGloss.y the texture's metal class (its polished texels: the highlight in
//                   the texture's colour, a little less diffuse light; times the map's metal mask, its R); u_fxGloss.z 0.
//   FX_LIGHT_PARALLAX   with FX_LIGHT_NORMALMAP (a stage 0 map whose material, stage 7, has a height): parallax occlusion mapping;
//                   stage 0's colour and the normal map are read where the eye's ray meets the relief.
//   FX_LIGHT_SHADOW with FX_FF_LIT: the sun's diffuse and specular terms are scaled by the sun shadow maps
//                   (stage 15, src/effects/shadows.hpp; one tap or the soft filter: fx_lighting.sh fxSunShadow); with
//                   FX_FF_BAKED: a town's walls likewise, other baked surfaces take the moving casters' shadows on
//                   the sun's part of their bake (u_fxShadow[3].z).
//   FX_FF_DYN       the plain combiner plus the moving casters' sun shadows (fs_ff_dyn with vs_ff[_unlit]_dyn: the
//                   lightmapped and baked ground under characters and ships; fx_sun_map.sh fxSunGround).
//   FX_FF_COVERAGE  alpha to coverage instead of the alpha-test discard (fx_coverage.sh); in the FX_FF_LIT /
//                   FX_FF_BAKED variants it is switched per draw by u_fxCoverage.z.
#include "ff_finish.sh"
#ifdef FX_FF_DYN
#include "fx_sun_map.sh"
#endif

#if defined(FX_FF_LIT) || defined(FX_FF_BAKED)
#define FX_FF_PIXEL_LIGHTING 1
#include "fx_lighting.sh"
// x: two-sided surface whose back side shows the light passing through (normal flipped towards the viewer)
// y: FX_LIGHT_NORMALMAP: the texture stage (0 or 1) whose coordinates the normal map follows
uniform vec4 u_fxFf;

#include "ff_skin_tone.sh"

#ifdef FX_LIGHT_SPECULAR
uniform vec4 u_fxGloss; // the gloss of the material texture (s_fxMaterial, fx_lighting.sh)
#endif

#include "ff_volumes.sh"

#ifdef FX_FF_TOWN_WALL
#include "ff_town_wall.sh"
#endif
#ifdef FX_FF_WET
#include "ff_wet.sh"
#endif

#ifdef FX_FF_LAMPS
#include "ff_lamps.sh"
#endif // FX_FF_LAMPS
#define FF_DIFFUSE ffDiffuse
#else
#define FF_DIFFUSE v_color0
#endif

#include "ff_stages.sh"

void main()
{
#ifdef FX_FF_PIXEL_LIGHTING
    vec3 P = v_position.xyz;
    vec3 V = normalize(-P);
    vec3 N = normalize(v_normal.xyz);
#ifdef FX_FF_LIT
    if (u_fxFf.x > 0.5)
    {
        // Two-sided thin surface (sails, flags): light the side the viewer sees; the light from behind comes
        // through as translucency. The facing comes from the triangle itself, so the row order of the target
        // does not matter.
        vec3 face = cross(dFdx(P), dFdy(P));
        if (dot(face, V) < 0.0) face = -face;
        if (dot(N, face) < 0.0) N = -N;
    }
#endif
    vec3 Ng = N;
    // The skin share (ff_skin_share.sh): faces, hands and hair, whose painted shading the relief's models read as bumps, and
    // the torso where the atlas has the character's own skin colour (fx/skin_tone.hpp, u_fxSkinTone; read at about 64
    // texels across, so the grain of leather and dark cloth averages into its own colour). This also protects generated
    // gloss in specular-only variants, before normal maps become ready.
    float ffTorso = clamp(v_position.w, 0.0, 1.0);
    float ffLikeness = 0.0;
    if (ffTorso > 0.004 && u_fxSkinTone[0].x > 0.0)
        ffLikeness = ffSkinLikeness(texture2DLod(s_tex0, v_texcoord0.xy, u_fxSkinTone[1].w).rgb);
    float ffSkin = ffSkinShare(v_normal.w, ffTorso, u_fxSkinTone[0].x, ffLikeness);
#ifdef FX_LIGHT_NORMALMAP
    // The relief follows the coordinates of its stage (u_fxFf.y); with the parallax that stage's texture is then read
    // where the relief's map is (ffReliefUv, with the derivatives of the unshifted coordinates).
    vec2 ffReliefUv = u_fxFf.y > 0.5 ? v_texcoord1.xy : v_texcoord0.xy;
    vec2 ffReliefDx = dFdx(ffReliefUv);
    vec2 ffReliefDy = dFdy(ffReliefUv);
    vec3 reliefGu;
    vec3 reliefGv;
    fxReliefFrame(Ng, P, ffReliefDx, ffReliefDy, reliefGu, reliefGv);
#ifdef FX_LIGHT_PARALLAX
    FxParallaxFrame ffPomFrame = fxParallaxFrame(dFdx(P), dFdy(P), ffReliefDx, ffReliefDy, V);
    vec3 ffPomFound;
    vec2 ffReliefUv0 = ffReliefUv;
    ffPomFound = vec3_splat(0.0);
    if (ffSkin < 0.999)
        ffReliefUv = mix(fxParallax(ffReliefUv, ffReliefDx, ffReliefDy, ffPomFrame.gu, ffPomFrame.gv,
                                  ffPomFrame.normal, V, length(P), ffPomFound),
                         ffReliefUv0, ffSkin);
    N = fxPerturbNormal(Ng, reliefGu, reliefGv, texture2DGrad(s_fxNormalMap, ffReliefUv, ffReliefDx, ffReliefDy).rg);
#else
    N = fxPerturbNormal(Ng, reliefGu, reliefGv, texture2D(s_fxNormalMap, ffReliefUv).rg);
#endif
    N = normalize(mix(N, Ng, ffSkin));
#endif
    // The highlight of this pixel: its gloss texel (stage 0's coordinates; the G of the material texture) or the
    // material's. ffGloss -1: no gloss map (ff_wet.sh may read it).
    float ffGloss = -1.0;
    vec2 ffSpec = fxMaterialSpec();
    // The metal of the pixel (0..1): a metal texture's polished texels; their highlight takes the texture's colour and
    // their diffuse light is a little less (subtle: no chrome look). Rust, carriages and grime are less glossy, so no
    // metal.
    float ffMetal = 0.0;
#ifdef FX_LIGHT_SPECULAR
    if (u_fxGloss.x > 0.0)
    {
        // G the gloss; R a gloss map's metal mask (fx::GlossMaterial, u_fxGloss.y > 0) or a relief's height (u_fxGloss.y
        // < 0 on metal: its polished texels by their gloss, which the metal mask floored at 0.5).
        vec2 ffMaterial = texture2D(s_fxMaterial, v_texcoord0.xy).rg;
        ffGloss = ffMaterial.g;
        float glossShare = u_fxGloss.x * (1.0 - ffSkin); // skin keeps the material's highlight
        ffMetal = (u_fxGloss.y > 0.0 ? u_fxGloss.y * ffMaterial.r : -u_fxGloss.y * smoothstep(0.4, 0.55, ffGloss)) * glossShare;
        // Metal reflects several times what lacquer or glazing do.
        ffSpec = mix(ffSpec, fxGlossSpec(ffGloss) * vec2(1.0, (1.0 + 2.5 * ffMetal) * u_fxGloss.w), glossShare);
    }
#endif
    vec3 ffSpecV = normalize(mul(u_fxLampView, vec4(V, 0.0)).xyz);
    vec3 Pw = mul(u_fxLampView, vec4(P, 1.0)).xyz;
    vec3 Nw = normalize(mul(u_fxLampView, vec4(Ng, 0.0)).xyz);
    // How open the pixel is to the sky (the visibility volume): x the sky it sees, y whether the sun can reach it.
    // Under a roof the sun and the weather's ambient do not arrive and the room's own occlusion counts in full; in
    // a doorway, by a window or under a hatch each arrives by its share.
    vec2 skySeen = ffVisibility(P, Ng);
    float sunReach = ffSunReach(skySeen.y);
    // The rain (ff_wet.sh): where it reaches (the openness above 15°: dry under roofs), the surface is wet, its relief
    // filled with water and its gloss raised, before the lamps and the sun take them; puddles in the relief's hollows.
#ifdef FX_FF_WET
    float ffHeight = 1.0;
#ifdef FX_LIGHT_PARALLAX
    if (u_fxWet[0].y > 0.5)
    {
        // An if / else, not `?:`: the SPIR-V build executes both sides of `?:` (ff_stages.sh).
        if (ffPomFound.x > 0.0)
            ffHeight = 1.0 - ffPomFound.y;
        else
            ffHeight = texture2DLod(s_fxMaterial, ffReliefUv, ffPomFound.z).r;
    }
#endif
    vec4 ffWet = ffWetPrepare(Nw, skySeen.y, ffGloss, ffMetal, ffHeight);
    if (ffWet.x > 0.0)
    {
        N = ffWetNormal(N, Ng, ffWet);
        ffSpec = ffWetSpec(ffSpec, ffWet, ffMetal);
    }
#else
    vec4 ffWet = vec4(0.0, 0.0, 0.0, 1.0); // dry (ffWetPrepare's)
#endif
    // The lamps' highlights: with the gloss on (u_fxGloss.w), or wet.
    vec2 ffLampSpecOf = ffSpec;
#ifdef FX_LIGHT_SPECULAR
    if (u_fxGloss.w <= 0.0 && ffWet.x <= 0.0)
        ffLampSpecOf.y = 0.0;
#endif
    vec3 ffPointDiffuse, ffPointSpecular;
    fxPointTerms(Ng,N,P,V,ffSpec,ffPointDiffuse,ffPointSpecular);
    // The place's lamps with their shadows (nothing where there are none).
#ifdef FX_FF_LAMPS
    vec3 ffLampMoved;
    vec3 ffLampSpec;
    vec4 lamps = ffLampLight(Pw, Nw, normalize(mul(u_fxLampView, vec4(N, 0.0)).xyz), ffSpecV, ffLampSpecOf, ffLampMoved, ffLampSpec);
#else
    vec3 ffLampMoved = vec3_splat(0.0);
    vec3 ffLampSpec = vec3_splat(0.0);
    vec4 lamps = vec4_splat(0.0);
#endif
#ifdef FX_FF_AO
    // Read once: the baked colours' relative occlusion below takes the same value and the same lobe weights.
    float aoHere = 1.0;
    vec4 aoLobes = vec4_splat(0.0);
    float aoLobeSum = 1.0;
    if (u_fxAoParams.x > 0.0)
    {
        aoLobes = ffAoLobes(Nw);
        aoLobeSum = max(dot(aoLobes, vec4_splat(1.0)), 1e-4);
        aoHere = ffAoVolume(P, Ng, aoLobes, aoLobeSum);
    }
    float aoVolume = mix(aoHere, 1.0, skySeen.x);
#else
    float aoVolume = 1.0;
#endif
    // The relief's cavity: an occlusion at the texture's scale, under the light as the volume's is (ambient
    // in full, half of the direct light); a puddle's water fills it.
    float cavity = 1.0;
#ifdef FX_LIGHT_PARALLAX
    cavity = mix(fxReliefCavity(ffReliefUv, ffPomFound), 1.0, max(ffWet.y, ffSkin));
#endif
    float cavityDirect = mix(1.0, cavity, 0.5);
#ifdef FX_FF_LIT
    // The volume occludes the ambient part (the per-vertex sum carries the global ambient and the lights not lit per
    // pixel); the per-pixel sun and the lamps keep their own visibility.
    // Read only where the sun's light or highlight can show: within its reach and facing it (fxSunDiffuse's wrap; the
    // highlight needs N.L > 0).
    float sunShadow = 0.0;
    if (sunReach > 0.0 && dot(N, u_fxLight[0].xyz) + u_fxLight[3].w > 0.0)
        sunShadow = fxSunShadow(P, Ng) * sunReach * cavityDirect;
#ifdef FX_LIGHT_PARALLAX
    sunShadow *= mix(fxParallaxShadow(ffReliefUv, ffPomFound, ffPomFrame.gu, ffPomFrame.gv,
                                    ffPomFrame.normal, u_fxLight[0].xyz), 1.0, ffSkin);
#endif
    vec3 ambient = fxAmbient(N);
    float lampSeen = dot(lamps.rgb, vec3(0.299, 0.587, 0.114));
#ifdef FX_FF_LAMPS
    if (u_fxLampGrid[2].w > 0.5)
    {
        // Indoors the model stands in the room's own light: the baked colours around it are its ambient (the global
        // ambient is the weather's, of the street). Where lamps light the pixel directly, the part of them the bake
        // holds leaves the ambient, as on the baked surfaces.
        vec3 room = ffRoomLight(Pw, Nw);
        float level = max(dot(room, vec3(0.299, 0.587, 0.114)), 0.03);
        // The room's level, the models' lift over it and the lamps' gain are scales of light: squared in linear light,
        // as the baked colours' relight below takes them, so models and the room around them stay as bright as each
        // other in either space.
        float lift = u_fxLampGrid[2].w;
        float lampGain = u_fxLamps[0].z;
        if (fxLinear())
        {
            level *= level;
            lift *= lift;
            lampGain *= lampGain;
        }
        vec3 roomAmbient = fxLightColor(room) * (lift * level / (level + u_fxLamps[0].w * lampGain * lamps.a));
        // The sky's light where the sky is seen: fxAmbient is the weather's ambient, the hemisphere's or a flat one
        // (src/effects/lighting.cpp), since the vertex sum's is switched off here.
        ambient = mix(roomAmbient, ambient, skySeen.x);
    }
#endif
    // A lamp's light on a model is at most that of a fully lit surface: above half of it the sum is compressed
    // towards 1 (a model at a candle's flame is lit, not burnt out).
    float lampShown = lampSeen <= 0.5 ? lampSeen : 0.5 + (lampSeen - 0.5) / (1.0 + 2.0 * (lampSeen - 0.5));
    vec3 lampLight = lamps.rgb * (lampShown / max(lampSeen, 1e-4)) * cavityDirect;
    // The occlusion of the ambient, as on the vertex sum (squared with it in linear light: fxLit).
    float ambientShade = aoVolume * cavity;
    if (fxLinear())
        ambientShade *= ambientShade;
    vec4 ffDiffuse = vec4(clamp(fxLit(v_color0.rgb * (aoVolume * cavity), v_matDiffuse.rgb,
                                 fxSunDiffuse(N, V) * sunShadow + (ffPointDiffuse + ffLampMoved) * cavityDirect +
                                     lampLight,
                                 v_matAmbient.rgb, ambient * ambientShade), 0.0, 1.0), v_color0.a);
#else
    vec4 ffDiffuse = v_color0;
#endif
    vec4 tex0 = vec4_splat(0.0); // stage 0's texel: the baked relight, the tint of a metal highlight
#endif
    vec4 current = FF_DIFFUSE;
    vec4 temp = vec4_splat(0.0);
    FF_TOWN_RATIO_READ(townTexel)
    bool running = true;
    FF_STAGE(0, STAGE0_SAMPLE, FF_FADE, STAGE0_PREV)
    // A setup's program has only its stages.
#if !defined(FF_SETUP) || FF_SETUP_STAGES > 1
    FF_STAGE(1, STAGE1_SAMPLE, FF_FADE, STAGE1_PREV)
#endif
#if !defined(FF_SETUP) || FF_SETUP_STAGES > 2
    FF_STAGE(2, STAGE2_SAMPLE, FF_NO_FADE, tex)
#endif
#if !defined(FF_SETUP) || FF_SETUP_STAGES > 3
    FF_STAGE(3, STAGE3_SAMPLE, FF_NO_FADE, tex)
#endif
#if !defined(FF_SETUP) || FF_SETUP_STAGES > 4
    FF_STAGE(4, STAGE4_SAMPLE, FF_NO_FADE, tex)
#endif
#ifdef FX_FF_DYN
    current.rgb *= fxSunGround(v_shadow.xyz, v_shadow.w);
#endif
#ifdef FX_FF_PIXEL_LIGHTING
    // The rain's water in the pores (ff_wet.sh): the surface's colour darker, before any highlight is added.
    current.rgb *= ffWet.w;
    tex0.rgb *= ffWet.w;
    // A metal's highlight takes the colour of its texture; the rest is white (the fixed-function material has no
    // usable specular colour).
    vec3 specTint = vec3_splat(1.0);
#ifdef FX_LIGHT_SPECULAR
    specTint = mix(specTint, tex0.rgb / max(max(tex0.r, max(tex0.g, tex0.b)), 0.04), ffMetal);
    // Metal scatters less of its light diffusely: 15 % less at full metal (subtle: the game's textures hold its shading).
    current.rgb *= 1.0 - 0.15 * ffMetal;
#endif
#endif
#ifdef FX_FF_LIT
    // Highlights and the rim light.
    current.rgb = fxAddSpecular(current.rgb, specTint * (fxSunSpecular(N, V, ffSpec) * sunShadow + ffPointSpecular +
                                                         ffLampSpec) + fxRim(N, V));
#ifdef FX_LIGHT_TRANSLUCENT
    // Sails and flags with the sun behind them: its light comes through the canvas (fx_lighting.sh). The visibility
    // is the sun's on the sail's sunlit side, so a sail does not shadow itself; a sail behind another one still gets
    // part of it (canvas passes light on, the shadow map cannot tell canvas from wood).
    if (u_fxLight[2].w > 0.0)
        current.rgb = fxAddSpecular(current.rgb, fxSunTransmission(Ng, V, dot(tex0.rgb, vec3(0.299, 0.587, 0.114))) *
                                                     mix(0.4, 1.0, fxSunShadow(P, -Ng)));
#endif
#endif
#ifdef FX_FF_BAKED
    // How lit the bake shows this texel: its stages' result against the texture alone (with a 2x modulate or a
    // light map alike). The sun's highlight shows where that is the light of the open sun, not in the shade; the
    // relief's sun-side shading and its own shadow likewise: in a shadow the game painted (and in the shade of a
    // wall) no sun lights the relief, so the painted dark stays as deep as the game's.
    float bakedSun = smoothstep(0.55, 0.9, dot(current.rgb, vec3(0.299, 0.587, 0.114)) / (1.0 - 0.15 * ffMetal) /
                                               max(dot(tex0.rgb, vec3(0.299, 0.587, 0.114)), 0.04));
#ifdef FX_LIGHT_NORMALMAP
    current.rgb *= mix(1.0, fxBakedRelief(Ng, N, V), bakedSun);
#ifdef FX_LIGHT_PARALLAX
    // The baked colour holds the sun and the sky: the relief's own shadow takes the sun's part (about half of it).
    current.rgb *= 1.0 - 0.5 * bakedSun * (1.0 - fxParallaxShadow(ffReliefUv, ffPomFound, ffPomFrame.gu, ffPomFrame.gv,
                                                                  ffPomFrame.normal, u_fxLight[0].xyz));
#endif
#endif
#ifdef FX_FF_AO
    if (u_fxAoParams.x > 0.0)
    {
        // The interior's occlusion on a baked surface: only what the bake lacks. The volume's occlusion here relative to
        // the open room before the surface, the brighter of half a metre and a metre and a half out (above a table or
        // a shelf): an open wall sees what the room before it sees and takes nothing (the bake's level holds the
        // room's enclosure); a corner, the floor under a table take their own. Over the darkening the bake already
        // shows against the room's baked light there (ffRoomLight, with the lamps): a corner the game painted dark
        // keeps its paint and takes nothing more; one it painted as bright as the room takes the volume's.
        // The ratio aoRel / max(held, aoRel) is exactly 1 where nothing occludes here (aoHere = 1: aoRel clamps to 1)
        // or nothing more than in the open room (aoRel = 1), held being at most 1: those reads are skipped there.
        float ratio = 1.0;
        if (aoHere < 1.0)
        {
            float aoOpen = max(ffAoVolume(P + Ng * 0.5, Ng, aoLobes, aoLobeSum),
                               ffAoVolume(P + Ng * 1.5, Ng, aoLobes, aoLobeSum));
            float aoRel = clamp(aoHere / max(aoOpen, 1e-3), 0.0, 1.0);
            if (aoRel < 1.0)
            {
                float held = 1.0;
#ifdef FX_FF_LAMPS
                if (u_fxLampGrid[2].w > 0.5)
                    held = clamp(dot(v_color0.rgb, vec3(0.299, 0.587, 0.114)) /
                                     max(dot(ffRoomLight(Pw, Nw), vec3(0.299, 0.587, 0.114)), 0.03), 0.0, 1.0);
#endif
                ratio = aoRel / max(held, aoRel);
            }
        }
        aoVolume = mix(ratio, 1.0, skySeen.x);
    }
#endif
    // The baked colour holds the sky and the sun together: three quarters of the cavity.
    aoVolume *= mix(1.0, cavity, 0.75);
    // The moving casters' shadows (characters, ships) take the sun's part of the bake, not the lamps' light.
#ifdef FX_LIGHT_SHADOW
    float movingShade = fxSunGroundAt(P, Ng);
#else
    float movingShade = 1.0;
#endif
#ifdef FX_FF_LAMPS
    // The place's lamps relight the baked colour b: b * (b + G * lit) / (b + k * G * unshadowed). Where no lamp
    // reaches, the colour is the bake; the side towards a lamp gets its light, what the lamp cannot see loses the
    // part of that light the bake is taken to hold (k). The AO volume occludes all of it: the room's own look stays.
    float baked = max(dot(v_color0.rgb, vec3(0.299, 0.587, 0.114)), 0.03);
    float gain = u_fxLamps[0].z;
    // The baked colours already carry the tone the game gave its lamp light on these surfaces: the relight adds
    // and takes light in that tone, with only a share of the lamp's own, far more saturated colour
    // (u_fxLampGrid[4].z; with all of it a candle-lit wall goes orange-red where the game's is warm white).
    vec3 lampTone = mix(vec3_splat(dot(lamps.rgb, vec3(0.299, 0.587, 0.114))), lamps.rgb, u_fxLampGrid[4].z);
    // The lamp-free part and the lamps' part add, the latter within the picture's headroom (ffLampRoll).
    vec3 surface = current.rgb * aoVolume;
    // The bake as it is (without the relight). Where it is darker than the share of the lamps it is taken to hold
    // (k G unshadowed), the game painted no lamp light there (a painted shadow, dark paint): the relight may darken it
    // (the lamp's shadow) but not lighten it above the bake.
    vec3 bakeOnly = surface * movingShade;
    float holds;
    if (fxLinear())
    {
        float share = u_fxLamps[0].w * gain * gain * lamps.a;
        holds = smoothstep(0.5, 1.0, baked * baked / max(share, 1e-4));
        surface *= surface / (baked * baked + share);
        vec3 base = surface * (baked * baked * movingShade * movingShade);
        vec3 lamp = surface * (gain * gain * lampTone);
        current.rgb = sqrt(base + lamp * ffLampRoll(base, lamp));
    }
    else
    {
        float share = u_fxLamps[0].w * gain * lamps.a;
        holds = smoothstep(0.5, 1.0, baked / max(share, 1e-4));
        surface /= baked + share;
        vec3 base = surface * (baked * movingShade);
        vec3 lamp = surface * (gain * lampTone);
        current.rgb = base + lamp * ffLampRoll(base, lamp);
    }
    current.rgb = mix(min(current.rgb, bakeOnly), current.rgb, holds);
    // The lamps' highlights on the baked colours, at the lamps' gain on them.
    vec3 ffLampBakedSpec = ffLampSpec * (fxLinear() ? gain * gain : gain);
#else
    // Without the lamps the relight leaves the bake: only the moving casters' shadow takes its sun.
    current.rgb *= aoVolume * movingShade;
    vec3 ffLampBakedSpec = vec3_splat(0.0);
#endif
#ifdef FX_FF_TOWN_WALL
    if (u_fxTownWall[0].w > 0.0)
    {
        // A town's wall (src/effects/town_light.hpp TownWallTone): its baked colour carries the game's sun along the
        // direction the game baked with and the game's sky, which takes no account of alleys and gaps between
        // houses. Relit as the lightmaps are (the town light's ratio, fx/sun_relight.hpp RelightTone): a + b sun + c sky
        // with our sun's shadow (where the sun reaches at all) at the bake's cosine and our sky now, over the game's sun
        // and sky then. A face turned more to the hour's sun than to the bake's gets more light only by the lift.
        float sunThen = max(0.0, dot(N, u_fxTownWall[2].xyz));
        float facingNow = sunThen + max(dot(N, u_fxTownWall[1].xyz) - sunThen, 0.0) * u_fxTownWall[2].w;
        // A face turned away from the hour's sun is in its own shadow (the lightmaps' rays: no sun below 0.02 of it).
        float sunNow = facingNow * fxSunShadow(P, Ng) * sunReach * clamp(dot(Ng, u_fxTownWall[1].xyz) * 50.0, 0.0, 1.0);
        float now = u_fxTownWall[0].x + u_fxTownWall[0].y * sunNow + u_fxTownWall[0].z * skySeen.x;
        float then = u_fxTownWall[0].x + u_fxTownWall[0].y * sunThen + u_fxTownWall[0].z * ffWallSky(skySeen.x);
        current.rgb *= mix(1.0, clamp(now / max(then, 0.02), 0.33, 3.0), u_fxTownWall[0].w);
    }
#endif
    current.rgb = fxLit(current.rgb, tex0.rgb, (ffPointDiffuse + ffLampMoved) * cavityDirect, vec3_splat(0.0),
                        vec3_splat(0.0));
#ifdef FX_LIGHT_SPECULAR
    // Highlights the bake cannot hold: the sun's (outdoors: u_fxLight[1] is black indoors), the lamps' with their
    // shadows (at the lamps' gain on baked colours), the dynamic lights'; with a gloss map, or wet in the rain.
    if (u_fxGloss.w > 0.0 || ffWet.x > 0.0)
        current.rgb = fxAddSpecular(current.rgb, specTint * (fxSunSpecular(N, V, ffSpec) * bakedSun * sunReach + ffLampBakedSpec +
                                                             ffPointSpecular));
#endif
#endif
#ifdef FX_FF_PIXEL_LIGHTING
#ifdef FX_FF_WET
    // The overcast sky in the wet surfaces outdoors, on the lit colour.
    current.rgb = ffWetFinish(current.rgb, N, V, mul(u_fxLampView, vec4(reflect(-V, N), 0.0)).y, ffWet);
#endif
#endif
    gl_FragColor = ffFinish(current);
    FF_SLOPE_DEPTH();
}
