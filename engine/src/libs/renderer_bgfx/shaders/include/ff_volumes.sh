// Ambient-occlusion volume of the interior (src/effects/ao_volume.hpp; stage 12 = fx::kAoVolumeStage).
// u_fxAoVolume: view space -> volume uvw. u_fxAoParams: x strength (0 = no volume for this draw; times fx_ao_gain), y contrast,
// z normal offset in metres (one cell: every cell the filter mixes lies before the surface), w 1 / what an open surface sees.
// The volume holds four cosine lobes (fx::AoLobeAxes, RGBA).
#ifdef FX_FF_AO
SAMPLER3D(s_fxAoVolume, 12);
uniform mat4 u_fxAoVolume;
uniform vec4 u_fxAoParams;
// The lobes' weights for the world-space normal Nw, computed once: a baked pixel reads the volume at three places
// with the same normal.
vec4 ffAoLobes(vec3 Nw)
{
    const float k = 0.57735027;
    return max(vec4(dot(Nw, vec3(k, k, k)), dot(Nw, vec3(k, -k, -k)), dot(Nw, vec3(-k, k, -k)), dot(Nw, vec3(-k, -k, k))), 0.0);
}
// The occlusion at P off the surface along Ng, for a draw with the volume (u_fxAoParams.x > 0); w: ffAoLobes, wSum:
// max(their sum, 1e-4).
float ffAoVolume(vec3 P, vec3 Ng, vec4 w, float wSum)
{
    vec3 uvw = mul(u_fxAoVolume, vec4(P + Ng * u_fxAoParams.z, 1.0)).xyz;
    vec4 lobes = texture3D(s_fxAoVolume, uvw);
    float seen = dot(lobes, w) / wSum;
    float ao = clamp(seen * u_fxAoParams.w, 0.0, 1.0);
    return max(mix(1.0, pow(ao, u_fxAoParams.y), u_fxAoParams.x), 0.0); // (the strength may pass 1: fx_ao_gain)
}
#endif

// How open to the sky the pixel's place is (src/effects/visibility.hpp, fx/visibility.hpp; stage 5 =
// fx::kVisibilityStage, the stage 0 cube map's unit, which no lit or baked variant has): x sky (cosine-weighted, to the
// place's bounds), y open (the share of directions more than 15° up that reach out). P and the geometric normal Ng in
// view space: the volume is read off the surface along Ng, on the side the surface faces (a wall's own cells would
// mix in the other side's). u_fxVisibility: view space -> volume uvw; u_fxVisibilityParams: x 1 = this place has a
// volume (0: everything counts as under open sky), y that offset in metres, z the
// volume's share while it fades in after it arrived (lighting.cpp kVolumeFade; open sky for the rest).
// Read once per pixel: every term that needs it takes the value.
// A stage 0 cube map takes the unit: such a draw has no lit or baked variant, and its place counts as open.
#ifndef STAGE0_CUBE
SAMPLER3D(s_fxVisibility, 5);
uniform mat4 u_fxVisibility;
uniform vec4 u_fxVisibilityParams;
#endif
vec2 ffVisibility(vec3 P, vec3 Ng)
{
#ifdef STAGE0_CUBE
    return vec2(1.0, 1.0);
#else
    if (u_fxVisibilityParams.x <= 0.0)
        return vec2(1.0, 1.0);
    vec2 seen = texture3D(s_fxVisibility, mul(u_fxVisibility, vec4(P + Ng * u_fxVisibilityParams.y, 1.0)).xyz).xy;
    return mix(vec2(1.0, 1.0), seen, u_fxVisibilityParams.z);
#endif
}
// Whether the sun can reach the pixel at all, from the volume's openness: closed under a roof (an interior, an
// arcade), open as soon as a share of the sky above 15° is. It gates the sun; it does not dim it: where the sun
// comes through, its shadow map alone says how much (the two multiplied darkened a sunlit alley twice).
float ffSunReach(float open)
{
    return smoothstep(0.0, 0.2, open);
}
