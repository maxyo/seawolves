$input v_color0, v_color1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_fog, v_normal, v_position, v_matDiffuse, v_matAmbient
// The material programs: a game's own materials (renderer_bgfx/README.md, "Materials"), lit by the sun with its
// shadow, the point lights, the place's lamps with theirs and the ambient of the lit programs, in linear light, the sum
// times the scene's exposure compressed into the picture as the post chain expands it again (fxCompressHdr). Compiled
// only as the pbr families' variants (shaders/ff_programs.cmake), which define FX_FF_PBR and FX_LIGHT_SPECULAR.
#define FX_FF_COVERAGE 1
#define FX_FF_PIXEL_LIGHTING 1
#define FX_LIGHT_PER_PIXEL 1
#define FX_LIGHT_HEMISPHERE 1
#define FX_LIGHT_POINT 1
#define FX_LIGHT_POINT_MAX 8
#include "ff_finish.sh"
#include "fx_lighting.sh"
#include "fx_tonemap.sh"
#include "ff_volumes.sh"
#ifdef FX_FF_LAMPS
#include "ff_lamps.sh"
#endif

SAMPLER2D(s_tex0, 0);       // the albedo (stage 0)
SAMPLER2D(s_tex1, 1);       // the occlusion on the second texture coordinates (stage 1), with u_fxPbr[0].w
SAMPLER2D(s_fxEmission, 2); // the emission (fx::kEmissionStage), with u_fxPbr[1].w
// [0] x roughness, y metal (the material's, without a map), z 1 = the rma map is bound (s_fxMaterial: R roughness,
//     G metal, B occlusion), w 1 = stage 1 is the occlusion
// [1] x the sun's, y the ambient's scale over their linear light, z the exposure, w the emission's strength (0: no map)
uniform vec4 u_fxPbr[2];

void main()
{
    vec3 P = v_position.xyz;
    vec3 V = normalize(-P);
    vec3 Ng = normalize(v_normal.xyz);
    vec2 uv = v_texcoord0.xy;
    vec4 albedo = texture2D(s_tex0, uv);
    albedo.a *= v_color0.a;
    // The second texture's occlusion multiplies the colour as the modulate-2x stage the models were made for.
    if (u_fxPbr[0].w > 0.5)
        albedo.rgb *= 2.0 * texture2D(s_tex1, v_texcoord1.xy).g;
    vec3 base = albedo.rgb * albedo.rgb; // gamma 2.0, as every target (fx_common.sh)

    vec3 N = Ng;
#ifdef FX_LIGHT_NORMALMAP
    vec3 gu;
    vec3 gv;
    fxReliefFrame(Ng, P, dFdx(uv), dFdy(uv), gu, gv);
    N = fxPerturbNormal(Ng, gu, gv, texture2D(s_fxNormalMap, uv).rg);
#endif
    float roughness = u_fxPbr[0].x;
    float metal = u_fxPbr[0].y;
    float occlusion = 1.0;
    if (u_fxPbr[0].z > 0.5)
    {
        vec3 rma = texture2D(s_fxMaterial, uv).rgb;
        roughness = rma.r;
        metal = rma.g;
        occlusion = rma.b;
    }
    // GGX stays finite; the highlight fades out towards full roughness, as the materials were authored (a map's
    // roughness 1 is a matte surface without one).
    roughness = clamp(roughness, 0.045, 1.0);
    vec2 spec = vec2(roughness, 1.0 - roughness);

    vec3 Pw = mul(u_fxLampView, vec4(P, 1.0)).xyz;
    vec3 Nw = normalize(mul(u_fxLampView, vec4(Ng, 0.0)).xyz);
    vec2 skySeen = ffVisibility(P, Ng);
    float sunReach = ffSunReach(skySeen.y);
    vec3 pointDiffuse;
    vec3 pointSpecular;
    fxPointTerms(Ng, N, P, V, spec, pointDiffuse, pointSpecular);
#ifdef FX_FF_LAMPS
    vec3 lampMoved;
    vec3 lampSpecular;
    vec4 lamps = ffLampLight(Pw, Nw, normalize(mul(u_fxLampView, vec4(N, 0.0)).xyz),
                             normalize(mul(u_fxLampView, vec4(V, 0.0)).xyz), spec, lampMoved, lampSpecular);
#else
    vec3 lampMoved = vec3_splat(0.0);
    vec3 lampSpecular = vec3_splat(0.0);
    vec4 lamps = vec4_splat(0.0);
#endif
    float aoVolume = 1.0;
#ifdef FX_FF_AO
    if (u_fxAoParams.x > 0.0)
    {
        vec4 aoLobes = ffAoLobes(Nw);
        aoVolume = mix(ffAoVolume(P, Ng, aoLobes, max(dot(aoLobes, vec4_splat(1.0)), 1e-4)), 1.0, skySeen.x);
    }
#endif
    vec3 L = u_fxLight[0].xyz;
    float sunShadow = 0.0;
    if (sunReach > 0.0 && dot(N, L) > 0.0)
        sunShadow = fxSunShadow(P, Ng) * sunReach;
    vec3 sun = fxLightColor(u_fxLight[1].rgb) * u_fxPbr[1].x;
    vec3 direct = sun * (max(dot(N, L), 0.0) * sunShadow) + pointDiffuse + lamps.rgb + lampMoved;
    float ambientShade = occlusion * aoVolume;
    vec3 ambient = fxAmbient(N) * (u_fxPbr[1].y * ambientShade);
    vec3 color = base * (1.0 - metal) * (direct + ambient);
    // Schlick's Fresnel at N.V for every light, F0 0.04 for dielectrics and the albedo for metals.
    vec3 F0 = mix(vec3_splat(0.04), base, metal);
    float nv = max(dot(N, V), 1e-4);
    vec3 fresnel = F0 + (vec3_splat(1.0) - F0) * pow(1.0 - nv, 5.0);
    color += fresnel * (fxSunSpecular(N, V, spec) * (u_fxPbr[1].x * sunShadow) + pointSpecular + lampSpecular);
    // The ambient's reflection: the hemisphere along the reflected ray through the split sum's analytic fit.
    color += fxEnvSpecular(F0, roughness, nv) * fxAmbient(reflect(-V, N)) * (u_fxPbr[1].y * ambientShade * spec.y);
    if (u_fxPbr[1].w > 0.0)
    {
        vec3 emission = texture2D(s_fxEmission, uv).rgb;
        color += emission * emission * u_fxPbr[1].w;
    }
    color = fxCompressHdr(color * u_fxPbr[1].z);
    gl_FragColor = ffFinish(vec4(sqrt(clamp(color, 0.0, 1.0)), albedo.a));
    FF_SLOPE_DEPTH();
}
