$input v_wpos
// The drawn sky (fx::Sky::Draw): the panorama of the sky model, the sun's and the moon's discs, stars, the clouds and
// the haze band at the horizon, into the gamma-encoded scene.
#include "fx_common.sh"

SAMPLER2D(s_fxSkyLut, 0);
SAMPLER2D(s_fxCloudNoise, 1); // fx::CloudNoise: one octave of gradient noise that tiles

// [0] xyz towards the sun, w its disc's visibility
// [1] rgb the sun disc's colour (linear)
// [2] xyz towards the moon, w its disc's visibility
// [3] rgb the moon's colour (linear), w the stars' brightness
// [4] rgb the haze colour (linear, or a tint of the sky's with [5].y), w its opacity at the horizon
// [5] x 1 / the sine of the haze band's height, y the haze takes the sky's colour
// [6] xy the clouds' drift (noise tiles), z the fair-weather clouds' cover, w the rain clouds' cover
// [7] rgb the light on the clouds (linear), w the noise octaves (2..4; from 3 a tap towards the light shades them)
// [8] xyz towards that light, w noise tiles per cloud height
uniform vec4 u_fxSky[9];

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

// Fractal sum of the noise, each octave twice the frequency, turned by 37 degrees and shifted.
float fxCloudNoise(vec2 uv, float octaves)
{
    float sum = 0.0;
    float weight = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < 4; ++i)
    {
        if (float(i) >= octaves)
            break;
        sum += amplitude * texture2D(s_fxCloudNoise, uv).r;
        weight += amplitude;
        uv = vec2(1.6 * uv.x - 1.2 * uv.y, 1.2 * uv.x + 1.6 * uv.y) + 0.37;
        amplitude *= 0.5;
    }
    return sum / weight;
}

// The cover keeps the noise above a threshold that falls as the cover grows (the coverage remap of Schneider,
// "The real-time volumetric cloudscapes of Horizon Zero Dawn", 2015).
float fxCloudDensity(float noise, float cover)
{
    float t = 0.78 - 0.56 * cover;
    return smoothstep(t, t + 0.22, noise);
}

// The clouds over `dir` (rgb linear, a opacity): a layer at the cloud height seen from below, its noise thresholded
// by the cover; lit by the sky above and around (`horizon`: the panorama there) and by the light where the layer
// thins towards it (a directional derivative: Quilez, "Directional derivative", iquilezles.org), with a silver
// lining round the light. The rain clouds grow from the noise's high values down as their cover rises: grey, thick.
vec4 fxClouds(vec3 dir, vec3 sunDir, vec3 horizon)
{
    vec4 cover = u_fxSky[6];
    if (dir.y <= 0.0 || cover.z + cover.w <= 0.0)
        return vec4_splat(0.0);
    vec2 uv = dir.xz / (dir.y + 0.12) * u_fxSky[8].w + cover.xy;
    float octaves = u_fxSky[7].w;
    float noise = fxCloudNoise(uv, octaves);
    float density = fxCloudDensity(noise, cover.z);
    float t = mix(1.1, -0.1, cover.w);
    float rain = smoothstep(t - 0.1, t + 0.1, noise) * step(0.0001, cover.w);

    vec3 light = u_fxSky[7].rgb;
    vec3 toLight = u_fxSky[8].xyz;
    float lit = 0.75;
    if (octaves > 2.5)
    {
        float towards = fxCloudDensity(fxCloudNoise(uv + toLight.xz * 0.03, 2.0), cover.z);
        lit = clamp(0.6 + 1.5 * (density - towards), 0.15, 1.0);
    }
    float thick = max(density, rain);
    float lining = pow(max(dot(dir, toLight), 0.0), 12.0) * (1.0 - thick) * 1.5;
    vec3 ambient = 0.5 * (fxSkyPanorama(vec3(0.0, 1.0, 0.0), sunDir) + horizon);
    vec3 fair = ambient * mix(1.0, 0.8, thick) + light * (lit * mix(0.95, 0.6, thick) + lining);
    vec3 grey = ambient * 0.45 + light * 0.12;
    vec3 colour = mix(fair, grey, rain);
    // Thinning towards the horizon, where the layer is seen edge on.
    return vec4(colour, max(density, rain) * smoothstep(0.0, 0.1, dir.y));
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
    vec3 horizon = fxSkyPanorama(vec3(dir.x, 0.0, dir.z), u_fxSky[0].xyz);
    vec4 clouds = fxClouds(dir, u_fxSky[0].xyz, horizon);
    sky = mix(sky, clouds.rgb, clouds.a);
    // The haze band, thickest at the horizon.
    float band = clamp(1.0 - abs(dir.y) * u_fxSky[5].x, 0.0, 1.0);
    vec3 haze = u_fxSky[5].y > 0.5 ? horizon * u_fxSky[4].rgb : u_fxSky[4].rgb;
    sky = mix(sky, haze, u_fxSky[4].w * band * band * (3.0 - 2.0 * band));
    gl_FragColor = vec4(fxEncode(sky), 1.0);
}
