$input v_wpos
// The drawn sky (fx::Sky::Draw): the panorama of the sky model, the sun's and the moon's discs, stars and the haze
// band at the horizon, into the gamma-encoded scene.
#include "fx_common.sh"

SAMPLER2D(s_fxSkyLut, 0);

// [0] xyz towards the sun, w its disc's visibility
// [1] rgb the sun disc's colour (linear)
// [2] xyz towards the moon, w its disc's visibility
// [3] rgb the moon's colour (linear), w the stars' brightness
// [4] rgb the haze colour (linear, or a tint of the sky's with [5].y), w its opacity at the horizon
// [5] x 1 / the sine of the haze band's height, y the haze takes the sky's colour
uniform vec4 u_fxSky[6];

// The panorama (fx::SkyColor): u by the azimuth from the sun, v the square root of the elevation; sqrt-encoded.
vec3 fxSkyPanorama(vec3 dir, vec3 sun)
{
    vec2 h = dir.xz;
    vec2 s = sun.xz;
    float lh = length(h);
    float ls = length(s);
    float cosAz = (lh > 1e-5 && ls > 1e-5) ? dot(h, s) / (lh * ls) : 1.0;
    vec2 uv = vec2(0.5 - 0.5 * cosAz, sqrt(clamp(dir.y, 0.0, 1.0)));
    return fxDecode(texture2DLod(s_fxSkyLut, uv, 0.0).rgb);
}

// A soft disc of angular radius `radius` (radians) around `centre`.
float fxDisc(vec3 dir, vec3 centre, float radius)
{
    float angle = acos(clamp(dot(dir, centre), -1.0, 1.0));
    return 1.0 - smoothstep(radius * 0.8, radius * 1.2, angle);
}

// Three pseudo-random numbers of a cell, from arithmetic only (no sine: its precision differs between GPUs).
vec3 fxCellHash(vec3 p)
{
    p = fract(p * vec3(0.1031, 0.11369, 0.13787));
    p += dot(p, p.yzx + 19.19);
    return fract(vec3((p.x + p.y) * p.z, (p.x + p.z) * p.y, (p.y + p.z) * p.x));
}

// Stars on a grid of cells over the faces of a cube: about one cell in sixty holds a star of its own brightness.
float fxStars(vec3 dir)
{
    vec3 a = abs(dir);
    float major = max(a.x, max(a.y, a.z));
    vec3 face = dir / major;
    const float kCells = 220.0;
    vec3 cell = floor(face * kCells);
    vec3 r = fxCellHash(cell + step(a, vec3_splat(major - 1e-6)) * 517.0);
    vec3 centre = (cell + 0.25 + 0.5 * r) / kCells;
    float d = length((face - centre) * kCells);
    float star = step(0.984, r.z) * (1.0 - smoothstep(0.05, 0.35, d));
    return star * (0.3 + 0.7 * fract(r.x * 7.31));
}

void main()
{
    vec3 dir = normalize(v_wpos);
    vec3 sky = fxSkyPanorama(dir, u_fxSky[0].xyz);
    float above = smoothstep(-0.02, 0.05, dir.y);
    sky += u_fxSky[3].w * above * fxStars(dir) * vec3(0.9, 0.95, 1.0);
    // The sun's disc is about 1.2 degrees across, the moon's 1.4.
    sky = mix(sky, u_fxSky[1].rgb, u_fxSky[0].w * above * fxDisc(dir, u_fxSky[0].xyz, 0.0105));
    sky = mix(sky, u_fxSky[3].rgb, u_fxSky[2].w * above * fxDisc(dir, u_fxSky[2].xyz, 0.0122));
    // The haze band, thickest at the horizon.
    float band = clamp(1.0 - abs(dir.y) * u_fxSky[5].x, 0.0, 1.0);
    vec3 horizon = fxSkyPanorama(vec3(dir.x, 0.0, dir.z), u_fxSky[0].xyz);
    vec3 haze = u_fxSky[5].y > 0.5 ? horizon * u_fxSky[4].rgb : u_fxSky[4].rgb;
    sky = mix(sky, haze, u_fxSky[4].w * band * band * (3.0 - 2.0 * band));
    gl_FragColor = vec4(fxEncode(sky), 1.0);
}
