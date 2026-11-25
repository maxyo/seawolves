$input v_color0, v_color1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_fog, v_foam
// Port of the technique shader sea_sea2_pixel_shader (ps_1_1) of src/techniques/weather/sea.fx.
// Stage 0: animated bump volume texture, stage 1: the game's foam texture (weather\\sea\\pena\\pena.tga; here read only
// by the hulls' wakes), stage 3: environment cube map.
//
// On top of the original (effects/sea.hpp sets u_seaFx; all off reproduces the D3D9 shader except the first point):
// - the reflected ray never points below the horizon: the cube's -Y face is never rendered above water, and the
//   original sampled its black there (dark specks near the horizon); a ray reflected downwards meets the sea again,
//   whose colour at the horizon the side faces hold;
// - band limit: the bump detail fades towards the flat surface as one pixel covers more texels of the bump
//   texture (far and grazing water), so the reflection does not shimmer at the horizon;
// - soft edges: the water fades out where it is shallower than the edge depth in front of the scene behind it
//   (s_seaDepth, the scene's view distance per pixel packed into RGB8 by fs_fx_sea_depth). Where that distance is
//   nearer than the water itself, the pixel belongs to an object in front of the sea (its edge: a rope, a sail, a
//   hull, partly covering a multisampled pixel) and the water is not faded: fading it there showed what lies under
//   the sea as bright fringes along every edge over the water;
// - specular: the sun's glint from the bumped normal: the same reflected ray as the game's sun road, so it lies on it;
// - ship wakes (sea_wakes.sh): the churned water behind and beside the hulls: its small waves are damped (the bump
//   detail fades), pale aerated water and a lace of the game's foam texture lie on it, lit by the sky and the sun.
#include "port_ps.sh"
#include "sea_wakes.sh"
#include "sea_optics.sh"
SAMPLER3D(s_vol0, 8);
SAMPLER2D(s_tex1, 1);
SAMPLERCUBE(s_cube2, 7); // the game's sun road cube (sea.cpp, stage 2): the glint's colour
SAMPLERCUBE(s_cube3, 11);
SAMPLER2D(s_seaDepth, 14);
// [0] x: band limit, y: soft edges, z: specular (each 0 or 1), w: far plane of the packed scene distance
// [1] xyz: towards the sun (world), w: specular power   [2] x: specular strength, w: 1 / edge depth (metres)
// [3] rgb: current game water colour, w: extinction per metre (0 disables turbidity).
// [4] xyz: actual shim camera forward (row-vector view third column).
uniform vec4 u_seaFx[5];

void main()
{
    vec4 v0 = v_color0;
    vec4 v1 = v_color1;
    vec4 r0 = vec4_splat(0.0);

    vec4 t0 = texture3D(s_vol0, v_texcoord0.xyz);                           // tex t0
    vec3 n = PS_BX2(t0.rgb);                                                //   t0_bx2
    // Texels of the bump texture's top level under one pixel: 1 close up, many at the horizon.
    vec2 footprint = fwidth(v_texcoord0.xy) * 128.0;
    float detail = 1.0 - u_seaFx[0].x * clamp(log2(max(max(footprint.x, footprint.y), 1.0)) * 0.3, 0.0, 0.85);
    vec2 wake = seaWake(v_texcoord4.zw); // x foam, y calm
    detail *= 1.0 - 0.6 * wake.y;
    n = mix(vec3(0.0, 1.0, 0.0), n, detail);
    float m1 = dot(v_texcoord1.xyz, n);                                     // texm3x3pad t1, t0_bx2
    float m2 = dot(v_texcoord2.xyz, n);                                     // texm3x3pad t2, t0_bx2
    float m3 = dot(v_texcoord3.xyz, n);                                     // texm3x3vspec t3, t0_bx2
    vec3 eye = vec3(v_texcoord1.w, v_texcoord2.w, v_texcoord3.w);           //   eye vector from the w components
    vec3 n0 = normalize(vec3(m1, m2, m3));
    vec3 R = portReflect(n0, eye);
    vec3 e = normalize(eye);
    R.y = max(R.y, 0.0);
    vec4 t3 = textureCube(s_cube3, R);
    r0.rgb = PS11(v0.rgb * t3.rgb + v1.rgb);                                // mad r0.xyz, v0, t3, v1
    r0.a = v0.a;                                                            // mov r0.w, v0.wwww
    if (u_seaFx[0].z > 0.5)
    {
        // The sun's glint: the same reflected ray as the game's sun road, so it lies on it, in the colour of the sun
        // the game draws into that road's cube (warm at dawn and dusk, none where the game shows no sun).
        vec3 sun = textureCube(s_cube2, u_seaFx[1].xyz).rgb;
        r0.rgb += pow(max(dot(normalize(R), u_seaFx[1].xyz), 0.0), u_seaFx[1].w) * u_seaFx[2].x * sun;
    }
    float gap = -1.0;
    if (u_seaFx[0].y > 0.5 || u_seaFx[3].w > 0.0)
    {
        vec3 packed = texture2D(s_seaDepth, gl_FragCoord.xy * u_viewTexel.xy).rgb;
        float behind = dot(packed, vec3(1.0, 1.0 / 255.0, 1.0 / 65025.0)) * u_seaFx[0].w;
        gap = behind - v_fog.z;
    }
    if (u_seaFx[3].w > 0.0 && gap >= 0.0)
    {
        // View-z distance to ray distance; neither surface normal nor wave strength changes.
        float rayCos = seaViewRayCos(e, u_seaFx[4].xyz);
        float transmission = seaOpticalTransmission(gap, rayCos, u_seaFx[3].w);
        // Sea2 blends source + destination*alpha. Only the transmitted fraction
        // becomes water-colour scattering; reflection/Fresnel remain as calculated.
        float scattered = r0.a * (1.0 - transmission);
        r0.rgb += clamp(u_seaFx[3].rgb, 0.0, 1.0) * scattered;
        r0.a *= transmission;
    }
    if (wake.x > 0.0)
    {
        // The churned water of a hull: pale aerated water under a lace of foam. The lace is the foam texture read at
        // two scales (it repeats with the texture, every 10 m; the trail's patches and its ageing are what vary
        // along a wake), lit by the sky above and the sun (sea_wakes_math.sh).
        vec2 foam_uv = seaWakeFoamUv(v_texcoord4.zw);
        vec2 fine_uv = vec2(seaFoamFine(foam_uv.y, 0.37), seaFoamFine(foam_uv.x, 0.11));
        float fine = dot(texture2D(s_tex1, fine_uv).rgb, vec3(0.3, 0.5, 0.2));
        float streak = dot(texture2D(s_tex1, foam_uv).rgb, vec3(0.3, 0.5, 0.2));
        streak = clamp((0.55 * streak + 0.45 * fine - 0.15) / 0.6, 0.0, 1.0);
        float lace = seaWakeLace(streak, wake.x);
        float aerated = seaWakeAerated(wake.x);
        vec3 sky = textureCube(s_cube3, vec3(0.0, 1.0, 0.0)).rgb;
        float sky_grey = dot(sky, vec3(0.3, 0.5, 0.2));
        vec3 sun = u_seaWake[2].rgb;
        vec3 white = vec3(seaFoamLight(sky.r, sky_grey, sun.r), seaFoamLight(sky.g, sky_grey, sun.g),
                          seaFoamLight(sky.b, sky_grey, sun.b));
        // The aerated water: the water's own colour, paler, never brighter than the foam on it.
        r0.rgb = mix(r0.rgb, min(mix(v1.rgb * 1.6 + 0.25 * white, white, 0.25), white), aerated);
        r0.rgb = mix(r0.rgb, white * (0.8 + 0.2 * streak), lace);
        r0.a *= (1.0 - aerated) * (1.0 - lace);
    }

    gl_FragColor = ffFinish(clamp(r0, 0.0, 1.0));
    if (u_seaFx[0].y > 0.5)
    {
        // Blended as Sea2: source + destination * alpha; fading to the scene keeps destination * 1.
        float edge = gap < 0.0 ? 1.0 : clamp(gap * u_seaFx[2].w, 0.0, 1.0);
        gl_FragColor.rgb *= edge;
        gl_FragColor.a = 1.0 - edge * (1.0 - gl_FragColor.a);
    }

    // The game's two layers over the sea, drawn by its technique as two more passes over the whole sea and folded in
    // here: both add to what this draw leaves (Sea2_Foam: source * source alpha + destination; Sea2_SunRoad:
    // source + destination), so their colours add to this draw's source, which the destination * alpha keeps. Each is
    // saturated as its own pass's output was; the sum saturates in the target as the passes' blends did. Not faded by
    // the soft edges (they never were). The target's alpha is no longer their sum: nothing reads the scene's alpha.
    if (u_psc[0].x > 0.5)
    {
        // Sea2_Foam (sea_sea2foam_pixel_shader): the foam texture (stage 1 here) at the vertex shader's uv, the
        // layer's alpha from the height (v_foam), fogged as this draw (both techniques leave the fog to the state).
        vec4 lace = ffFinish(clamp(vec4(texture2D(s_tex1, v_foam.xy).rgb, v_foam.z), 0.0, 1.0));
        gl_FragColor.rgb += lace.rgb * lace.a;
    }
    // Sea2_SunRoad (sea_sea2sunroad_pixel_shader, no fog): the sun road cube (stage 2 here) in the ray reflected
    // about the unfiltered bump normal (no band limit).
    vec3 bump = PS_BX2(t0.rgb);
    vec3 road = normalize(vec3(dot(v_texcoord1.xyz, bump), dot(v_texcoord2.xyz, bump), dot(v_texcoord3.xyz, bump)));
    gl_FragColor.rgb += clamp(textureCube(s_cube2, portReflect(road, eye)).rgb, 0.0, 1.0);
}
