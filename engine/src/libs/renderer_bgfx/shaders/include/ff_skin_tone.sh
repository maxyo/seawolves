// A character's own skin colour (fx/skin_tone.hpp, the same test as SkinLikeness: keep both in step): [0] its mean
// chromaticity r, g, its luminance's median and 95th percentile; [1] the inverse covariance of the chromaticity rr, rg,
// gg, w the lod the test reads. [0].x 0: explicitly disabled; -1: unknown colour, conservatively mask the torso.
uniform vec4 u_fxSkinTone[2];
#include "ff_skin_share.sh"
float ffSmooth(float a, float b, float x)
{
    float t = clamp((x - a) / (b - a), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
float ffSkinLikeness(vec3 c)
{
    float sum = max(c.r + c.g + c.b, 1e-4);
    vec2 d = c.rg / sum - u_fxSkinTone[0].xy;
    float d2 = d.x * d.x * u_fxSkinTone[1].x + 2.0 * d.x * d.y * u_fxSkinTone[1].y + d.y * d.y * u_fxSkinTone[1].z;
    float lum = dot(c, vec3(0.299, 0.587, 0.114));
    float inRange = ffSmooth(u_fxSkinTone[0].z * 0.45, u_fxSkinTone[0].z * 0.65, lum) *
                    (1.0 - ffSmooth(u_fxSkinTone[0].w * 1.1, u_fxSkinTone[0].w * 1.3 + 0.02, lum));
    float satTone = length(u_fxSkinTone[0].xy - vec2_splat(1.0 / 3.0));
    float sat = length(c.rg / sum - vec2_splat(1.0 / 3.0));
    float saturated = ffSmooth(0.55, 0.8, sat / max(satTone, 1e-3));
    return (1.0 - ffSmooth(2.25, 6.25, d2)) * inRange * saturated;
}
