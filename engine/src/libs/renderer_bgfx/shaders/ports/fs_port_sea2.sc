$input v_texcoord0, v_texcoord1, v_texcoord4, v_fog, v_foam
// The sea's pixel program (technique Sea2 of src/techniques/weather/sea.fx, blended source + destination * alpha: the
// alpha is how much of the scene behind the water shows through). Lit from the wave field's slopes per pixel:
// - filtered slopes (LEAN mapping, Olano and Baker 2010): each cascade's texture holds the mean slope and mean square
//   slope per level (src/libs/sea, PackSlopeMoments); the normal follows the mean of what the pixel covers and the
//   variance it hides widens the sun's highlight (Bruneton, Neyret and Holzschuch 2010), so detail too fine for a
//   pixel becomes roughness instead of flicker;
// - Schlick's Fresnel term with the game's Sea.Frenel; reflection of the scene's cube (s_cube3) along the mean normal,
//   never below the horizon (the cube's lower face is never drawn above water; a ray reflected downwards meets the
//   sea again, whose colour at the horizon the side faces hold);
// - the body of the water in the game's colour (Sea2.WaterColor), its transparency by the view's slant
//   (Sea2.Attenuation, Transparency) and by the depth behind it (the scene distance capture, s_seaDepth);
// - the sun's or moon's glint: a GGX microfacet lobe (Walter et al. 2007) whose roughness is the hidden slope
//   variance, in the sun's colour, or the moon's as the game draws it into its sun road cube (s_cube2);
// - soft edges, the hulls' wakes (sea_wakes.sh) and the game's height foam (technique Sea2_Foam, folded in).
#include "port_ps.sh"
#include "sea_wakes.sh"
#include "sea_optics.sh"
SAMPLER2D(s_tex1, 1);     // the game's foam texture (weather\\sea\\pena\\pena.tga)
SAMPLER2D(s_tex4, 4);     // cascade 0's slope moments (stage 4; the shim names units by their usual stage kind)
SAMPLER2D(s_cube0, 5);    // cascade 1's (stage 5)
SAMPLER2D(s_cube1, 6);    // cascade 2's (stage 6)
SAMPLERCUBE(s_cube2, 7);  // the game's sun road cube (stage 2): the moon's colour
SAMPLERCUBE(s_cube3, 11); // the scene reflected (stage 3)
SAMPLER2D(s_seaDepth, 14);
// [0] y: soft edges, z: glint (each 0 or 1), w: far plane of the packed scene distance
// [1] xyz: towards the sun or moon (world)   [2] x: glint strength, w: 1 / edge depth (metres)
// [3] rgb: current game water colour, w: extinction per metre (0 disables turbidity).
// [4] xyz: actual shim camera forward (row-vector view third column).
// [5] rgb: the sun's light, w: 1 when the light is the sun (else the moon, in the colour of the sun road cube).
uniform vec4 u_seaFx[6];
// u_psc (src/libs/sea/src/sea.cpp): [0] x the game's foam layer drawn; [1..3] cascade 0..2: xy the slope coordinate
// of the mesh's origin, z the slope coordinates per metre (1 / patch), w < 0: no such cascade; [4] y the foam
// texture's coordinates per metre, zw its coordinate of the mesh's origin; [5] rgb Sea2.WaterColor, w Reflection;
// [6] rgb Sea2.SkyColor, w Transparency; [7] x Sea2.Attenuation, y Fresnel reflectance at normal incidence.

// Slope variance (per axis) of the ripples finer than the last cascade's samples, which no texture holds: keeps the
// glint of a calm sea from shrinking to a mirror's point.
#define SEA_RIPPLE_VARIANCE 0.0008
#define SEA_PI 3.14159265
// The fixed-function finish (fog, alpha test); the technique has no vertex specular to add.
#define seaFinish(color) ffFinishWith(color, v_fog, v_texcoord0.xy, vec3_splat(0.0))

// Adds one cascade's moments: x, y the mean slope; z, w the variance of the slope along x and z.
void seaAddMoments(vec4 moments, inout vec4 sum)
{
    sum += vec4(moments.xy, max(moments.zw - moments.xy * moments.xy, 0.0));
}

float seaSchlick(float f0, float cosine)
{
    float m = 1.0 - clamp(cosine, 0.0, 1.0);
    float m2 = m * m;
    return f0 + (1.0 - f0) * m2 * m2 * m;
}

// Smith's masking of the GGX distribution (alpha2 its roughness squared) for one direction.
float seaSmithG1(float cosine, float alpha2)
{
    return 2.0 * cosine / (cosine + sqrt(alpha2 + (1.0 - alpha2) * cosine * cosine));
}

void main()
{
    vec2 still = v_texcoord0.xy;
    vec4 slope = vec4(0.0, 0.0, SEA_RIPPLE_VARIANCE, SEA_RIPPLE_VARIANCE);
    if (u_psc[1].w > 0.0)
        seaAddMoments(texture2D(s_tex4, still * u_psc[1].z + u_psc[1].xy), slope);
    if (u_psc[2].w > 0.0)
        seaAddMoments(texture2D(s_cube0, still * u_psc[2].z + u_psc[2].xy), slope);
    if (u_psc[3].w > 0.0)
        seaAddMoments(texture2D(s_cube1, still * u_psc[3].z + u_psc[3].xy), slope);
    vec2 wake = seaWake(v_texcoord4.zw); // x foam, y calm
    // The wake's churned water damps the waves' slopes.
    slope *= vec4(vec2_splat(1.0 - 0.6 * wake.y), vec2_splat(1.0 - 0.8 * wake.y));

    vec3 n = normalize(vec3(-slope.x, 1.0, -slope.y));
    vec3 e = normalize(v_texcoord1.xyz);
    float nv = max(dot(n, e), 0.001);
    float f0 = u_psc[7].y;
    float fresnel = seaSchlick(f0, nv);
    vec3 r = reflect(-e, n);
    r.y = max(r.y, 0.0);
    vec3 sky = textureCube(s_cube3, r).rgb;
    // The slant's transparency: the game's 2^(-Attenuation / |view y|).
    float clear = exp2(-u_psc[7].x / max(abs(e.y), 0.01));
    vec3 water = u_psc[5].rgb * (1.0 - fresnel) * (1.0 - clear);
    vec4 r0 = vec4(fresnel * u_psc[5].w * u_psc[6].rgb * sky + water, (1.0 - fresnel) * clear * u_psc[6].w);

    if (u_seaFx[0].z > 0.5)
    {
        vec3 l = u_seaFx[1].xyz;
        float nl = dot(n, l);
        if (nl > 0.0)
        {
            vec3 h = normalize(l + e);
            float nh = max(dot(n, h), 0.0);
            float alpha2 = slope.z + slope.w;
            float d = nh * nh * (alpha2 - 1.0) + 1.0;
            float ggx = alpha2 / (SEA_PI * d * d);
            float g = seaSmithG1(nl, alpha2) * seaSmithG1(nv, alpha2);
            float glint = ggx * g * seaSchlick(f0, dot(e, h)) / (4.0 * nv);
            vec3 light = u_seaFx[5].w > 0.5 ? u_seaFx[5].rgb : textureCube(s_cube2, l).rgb;
            r0.rgb += glint * u_seaFx[2].x * light;
        }
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
        // Only the transmitted fraction becomes water-colour scattering; reflection stays as calculated.
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
        vec3 above = textureCube(s_cube3, vec3(0.0, 1.0, 0.0)).rgb;
        float above_grey = dot(above, vec3(0.3, 0.5, 0.2));
        vec3 sun = u_seaWake[2].rgb;
        vec3 white = vec3(seaFoamLight(above.r, above_grey, sun.r), seaFoamLight(above.g, above_grey, sun.g),
                          seaFoamLight(above.b, above_grey, sun.b));
        // The aerated water: the water's own colour, paler, never brighter than the foam on it.
        r0.rgb = mix(r0.rgb, min(mix(water * 1.6 + 0.25 * white, white, 0.25), white), aerated);
        r0.rgb = mix(r0.rgb, white * (0.8 + 0.2 * streak), lace);
        r0.a *= (1.0 - aerated) * (1.0 - lace);
    }

    gl_FragColor = seaFinish(clamp(r0, 0.0, 1.0));
    if (u_seaFx[0].y > 0.5)
    {
        // Fading to the scene keeps destination * 1.
        float edge = gap < 0.0 ? 1.0 : clamp(gap * u_seaFx[2].w, 0.0, 1.0);
        gl_FragColor.rgb *= edge;
        gl_FragColor.a = 1.0 - edge * (1.0 - gl_FragColor.a);
    }

    // The game's foam layer (its technique Sea2_Foam, source * source alpha + destination), folded in: the foam
    // texture, its alpha from the height (v_foam), fogged as this draw.
    if (u_psc[0].x > 0.5)
    {
        vec2 foam_uv = v_texcoord0.xy * u_psc[4].y + u_psc[4].zw;
        vec4 lace = seaFinish(clamp(vec4(texture2D(s_tex1, foam_uv).rgb, v_foam.z), 0.0, 1.0));
        gl_FragColor.rgb += lace.rgb * lace.a;
    }
}
