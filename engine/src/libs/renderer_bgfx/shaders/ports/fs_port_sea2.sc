$input v_color0, v_color1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_fog, v_foam
// Port of the technique shader sea_sea2_pixel_shader (ps_1_1) of src/techniques/weather/sea.fx, on the wave field.
// Stage 0: the field's slopes (a volume, one slice per cascade: SEA, src/libs/sea), stage 1: the game's foam texture
// (weather\\sea\\pena\\pena.tga), stage 3: environment cube map.
//
// Instead of the original's animated bump texture in the vertex normal's frame, the normal is the field's, per pixel:
// the slopes of every cascade summed, each fading out as one pixel covers more of its samples (the detail a pixel
// cannot resolve would alias at the horizon).
// On top of the original (effects/sea.hpp sets u_seaFx):
// - the reflected ray never points below the horizon: the cube's -Y face is never rendered above water, and the
//   original sampled its black there (dark specks near the horizon); a ray reflected downwards meets the sea again,
//   whose colour at the horizon the side faces hold;
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
SAMPLER3D(s_vol0, 8); // the field's slopes per cascade: hx, hz, dxx, dzz (storm::sea::ocean::Slope)
SAMPLER2D(s_tex1, 1);
SAMPLERCUBE(s_cube2, 7); // the game's sun road cube (sea.cpp, stage 2): the glint's colour
SAMPLERCUBE(s_cube3, 11);
SAMPLER2D(s_seaDepth, 14);
// [0] y: soft edges, z: specular (each 0 or 1), w: far plane of the packed scene distance
// [1] xyz: towards the sun (world), w: specular power   [2] x: specular strength, w: 1 / edge depth (metres)
// [3] rgb: current game water colour, w: extinction per metre (0 disables turbidity).
// [4] xyz: actual shim camera forward (row-vector view third column).
uniform vec4 u_seaFx[5];
// u_psc (SEA): [0] x the game's foam layer drawn; [1..3] cascade 0..2: xy the slope coordinate of the mesh's origin,
// z the slope coordinates per metre (1 / patch), w the cascade's slice coordinate (< 0: no such cascade);
// [4] x samples per cascade side, y the foam texture's coordinates per metre, zw its coordinate of the mesh's origin.

// The slopes of the cascades at the still point `still` (metres from the mesh's origin): `filtered` fades each cascade
// with the samples one pixel covers, `sharp` does not.
void seaSlopes(vec2 still, out vec4 filtered, out vec4 sharp)
{
    filtered = vec4_splat(0.0);
    sharp = vec4_splat(0.0);
    for (int c = 1; c <= 3; c++)
    {
        vec4 cascade = u_psc[c];
        if (cascade.w < 0.0)
            continue;
        vec2 uv = still * cascade.z + cascade.xy;
        vec4 slope = texture3D(s_vol0, vec3(uv, cascade.w));
        vec2 footprint = fwidth(uv) * u_psc[4].x;
        float keep = 1.0 - clamp(log2(max(max(footprint.x, footprint.y), 1.0)) / 3.0, 0.0, 1.0);
        filtered += slope * keep;
        sharp += slope;
    }
}

// The displaced surface's normal from summed slopes (storm::sea::ocean::SurfacePoint::Normal without the cross terms).
vec3 seaNormal(vec4 slope)
{
    float x = max(1.0 + slope.z, 0.1);
    float z = max(1.0 + slope.w, 0.1);
    return normalize(vec3(-slope.x * z, x * z, -slope.y * x));
}

void main()
{
    vec4 v0 = v_color0;
    vec4 v1 = v_color1;
    vec4 r0 = vec4_splat(0.0);

    vec4 filtered, sharp;
    seaSlopes(v_texcoord0.xy, filtered, sharp);
    vec2 wake = seaWake(v_texcoord4.zw); // x foam, y calm
    // The wake's churned water damps the small waves: the field's detail fades.
    vec3 n0 = normalize(mix(vec3(0.0, 1.0, 0.0), seaNormal(filtered), 1.0 - 0.6 * wake.y));
    vec3 eye = vec3(v_texcoord1.w, v_texcoord2.w, v_texcoord3.w); // the eye vector from the w components
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
        vec2 foam_uv = v_texcoord0.xy * u_psc[4].y + u_psc[4].zw;
        vec4 lace = ffFinish(clamp(vec4(texture2D(s_tex1, foam_uv).rgb, v_foam.z), 0.0, 1.0));
        gl_FragColor.rgb += lace.rgb * lace.a;
    }
    // Sea2_SunRoad (sea_sea2sunroad_pixel_shader, no fog): the sun road cube (stage 2 here) in the ray reflected
    // about the unfiltered normal.
    gl_FragColor.rgb += clamp(textureCube(s_cube2, portReflect(seaNormal(sharp), eye)).rgb, 0.0, 1.0);
}
