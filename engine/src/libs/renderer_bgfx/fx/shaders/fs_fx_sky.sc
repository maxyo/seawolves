$input v_wpos
// The drawn sky (fx::Sky::Draw; renderer_bgfx/README.md, "Sky"): the atmosphere about the sun and about the moon, the
// stars, the sun's and the moon's discs, the clouds, the haze band at the horizon and the height fog, in linear light
// times the scene's exposure, compressed into the picture as the material programs are. fx::SkyBackground is its CPU
// side; change both together.
#include "fx_common.sh"
#include "fx_tonemap.sh"

SAMPLER2D(s_fxCloudNoise, 1); // fx::CloudNoise: one octave of gradient noise that tiles
SAMPLER2D(s_fxSunDisc, 2);    // the game's sun and moon sprites (fx::Sky::SetDiscs)
SAMPLER2D(s_fxMoonDisc, 3);

// [0] xyz towards the sun, w the sky's daylight (1 day .. 0 night)
// [1] xyz towards the moon, w the scene's exposure
// [2] rgb the day sky's radiance, w the stars' density (0..1: the share of cells without a star)
// [3] rgb the night sky's radiance
// [4] rgb the sun's light (linear), w its disc's half size (tangent of the angle; 0: no disc)
// [5] rgb the moon's light, w its disc's half size
// [6] haze: x opacity at the horizon, y 1 / the sine of its height, z 1 / its edge's fade, w the tangent of the
//     elevation its colour is taken at
// [7] rgb the haze colour (with w > 0.5 a tint of the atmosphere's)
// [8] fog: x density per metre at its base, y the base's height, z 1 / the layer's thickness (0: even), w most opacity
// [9] fog: x where it begins, y how far the sky stands (0: no fog over the sky), z the eye's height, w its colour is a
//     tint of the atmosphere's
// [10] rgb the fog's colour
// [11] xy cos and sin of the camera's yaw (the sun's sprite turns with it)
// [12] xy the clouds' drift (noise tiles), z the fair-weather clouds' cover, w the rain clouds' cover
// [13] rgb the light on the clouds (linear, exposed), w the noise octaves (2..4; from 3 a tap towards the light shades them)
// [14] xyz towards that light, w noise tiles per cloud height
uniform vec4 u_fxSky[15];

// ---- the atmosphere: three shells of air over a planet (fx::SkyAtmosphere) ----
#define FX_EARTH_RADIUS 6371000.0
#define FX_RAYLEIGH_DEPTH 8228.0
#define FX_MIE_DEPTH 1800.0
#define FX_OZONE_HEIGHT 30000.0
#define FX_MIE 16e-6
#define FX_LN2 0.6931472

// From the ground to the top of a shell `depth` thick along a ray of elevation sine `y`; written without the
// difference of two squares of the planet's radius, which a float cannot hold.
float fxShellPath(float y, float depth)
{
    float yr = y * FX_EARTH_RADIUS;
    float k = depth * (2.0 * FX_EARTH_RADIUS + depth);
    float root = sqrt(yr * yr + k);
    return yr > 0.0 ? k / (yr + root) : root - yr;
}

// The path lengths through the Rayleigh shell, the Mie shell and the ozone layer.
vec3 fxThickness(float y)
{
    float yr = y * FX_EARTH_RADIUS;
    float low = FX_OZONE_HEIGHT * (2.0 * FX_EARTH_RADIUS + FX_OZONE_HEIGHT);
    float high = (FX_OZONE_HEIGHT + FX_RAYLEIGH_DEPTH) * (2.0 * FX_EARTH_RADIUS + FX_OZONE_HEIGHT + FX_RAYLEIGH_DEPTH);
    float ozone = (high - low) / (sqrt(yr * yr + high) + sqrt(yr * yr + low));
    return vec3(fxShellPath(y, FX_RAYLEIGH_DEPTH), fxShellPath(y, FX_MIE_DEPTH), ozone);
}

// Optical depth in powers of two of a path of these lengths.
vec3 fxOpticalDepth(vec3 thickness)
{
    vec3 rayleigh = vec3(5.6e-6, 1.25e-5, 2.9e-5);
    vec3 ozone = vec3(3.426, 8.298, 0.356) * 6e-7;
    return (rayleigh * thickness.x + vec3_splat(FX_MIE * thickness.y) + ozone * thickness.z) * (1.0 / FX_LN2);
}

// The sky along `dir` lit from `light` with the day's radiance (`solar`) or the night's; w the light's optical depth
// (red, in powers of two), which dims the moon's halo.
vec4 fxAtmosphere(vec3 dir, vec3 light, bool solar)
{
    vec3 thickness = fxThickness(dir.y);
    float mu = max(dot(dir, light), 0.0);
    vec3 scatter = thickness.x * (0.4 * mu * mu + 1.12) * vec3(5.6e-6, 1.25e-5, 2.9e-5);
    float g = 0.8 - light.y * 0.15 - FX_MIE * 1500.0;
    float g2 = g * g;
    float a = inversesqrt(1.0 + g2 - 2.0 * g * mu);
    scatter += vec3_splat(thickness.y * (0.4 + light.y * 0.1 + FX_MIE * 7000.0) * (1.0 - g2) * a * a * a * FX_MIE);

    vec3 toLight = fxOpticalDepth(fxThickness(light.y));
    vec3 toEye = fxOpticalDepth(thickness);
    // The light's path and the eye's differ: the mean transmission over that difference.
    vec3 d = abs(toLight - toEye) * FX_LN2;
    vec3 mean = vec3(d.x < 0.01 ? 1.0 - d.x * 0.5 + d.x * d.x / 6.0 : (1.0 - exp(-d.x)) / d.x,
                     d.y < 0.01 ? 1.0 - d.y * 0.5 + d.y * d.y / 6.0 : (1.0 - exp(-d.y)) / d.y,
                     d.z < 0.01 ? 1.0 - d.z * 0.5 + d.z * d.z / 6.0 : (1.0 - exp(-d.z)) / d.z);
    vec3 lit = scatter * exp2(-min(toEye, toLight)) * mean * (0.6 + light.y * 0.3);
    vec3 night = clamp(u_fxSky[3].rgb * lit, 0.0, 100.5);
    if (!solar)
        return vec4(night, toLight.x);
    vec3 sky = clamp(u_fxSky[2].rgb * lit, 0.0, 100.5);

    // A low sun reddens the horizon on its side and darkens the other side towards the night's sky.
    float up = max(dir.y, 0.0);
    float low = 1.0 - smoothstep(0.08, 0.35, light.y);
    float lowest = 1.0 - smoothstep(0.025, 0.10, light.y);
    float red = 1.0 - smoothstep(0.02, 0.22, light.y);
    float band = 1.0 - smoothstep(0.0, 0.45, up);
    float wide = 1.0 - smoothstep(0.0, 0.72, up);
    float side = smoothstep(-0.60, 0.70, dot(dir.xz, light.xz) / max(length(dir.xz) * length(light.xz), 0.0001));
    vec3 sunset = max(sky.r, max(sky.g, sky.b)) * mix(vec3(1.1, 0.30, 0.025), vec3(1.1, 0.10, 0.008), red);
    sky = mix(sky, night, low * (1.0 - side) * mix(0.35, 0.70, wide));
    sky = mix(sky, sunset, max(low * band * 0.85, lowest * wide * 0.95) * mix(0.08, 1.0, side));
    return vec4(sky, toLight.x);
}

// The sky about the sun by day, about the moon by night, blended by the daylight.
vec4 fxSkyAtmosphere(vec3 dir)
{
    float day = u_fxSky[0].w;
    vec4 solar = day > 0.0 ? fxAtmosphere(dir, u_fxSky[0].xyz, true) : vec4_splat(0.0);
    vec4 lunar = day < 1.0 ? fxAtmosphere(dir, u_fxSky[1].xyz, false) : vec4_splat(0.0);
    return mix(lunar, solar, day);
}

// ---- stars ----
// Three pseudo-random numbers of a cell, from arithmetic only (no sine: its precision differs between GPUs).
vec3 fxCellHash(vec3 p)
{
    p = fract(p * vec3(0.1031, 0.11369, 0.13787));
    p += dot(p, p.yzx + 19.19);
    return fract(vec3((p.x + p.y) * p.z, (p.x + p.z) * p.y, (p.y + p.z) * p.x));
}

// A cell of 1/2500 of a unit direction holds a star when its number passes the density; the star is as bright as how
// far past, to the 60th power, each a little brighter or dimmer.
float fxStarSample(vec3 dir, float density)
{
    vec3 r = fxCellHash(floor(dir * 2500.0));
    if (density >= 1.0 || r.x < density)
        return 0.0;
    float twinkle = 0.6 + 0.4 * sin(r.y * 6.28318);
    return pow((r.x - density) / (1.0 - density), 60.0) * twinkle;
}

// Four samples within the pixel; where a pixel spans many cells, their mean.
float fxStars(vec3 dir, vec3 dx, vec3 dy)
{
    float density = clamp(u_fxSky[2].w, 0.0, 1.0);
    float mean = (1.0 - density) * (0.6 / 61.0);
    float unresolved = smoothstep(4.0, 8.0, 2500.0 * max(length(dx), length(dy)));
    float stars = mean;
    if (unresolved < 1.0)
    {
        dx *= 0.25;
        dy *= 0.25;
        float sum = fxStarSample(normalize(dir - dx - dy), density) + fxStarSample(normalize(dir + dx - dy), density) +
                    fxStarSample(normalize(dir - dx + dy), density) + fxStarSample(normalize(dir + dx + dy), density);
        stars = mix(sum * 0.25, mean, unresolved);
    }
    return stars * smoothstep(0.0, 0.4, dir.y);
}

// ---- the sun's and the moon's discs ----
// Where `dir` falls on a sprite of half size `size` facing the eye from `centre`; outside: below 0 or above 1.
vec2 fxDiscUv(vec3 dir, vec3 centre, float size)
{
    if (dot(dir, centre) <= 0.0)
        return vec2_splat(-1.0);
    vec3 up = abs(centre.y) > 0.99 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
    vec3 right = normalize(cross(up, centre));
    vec3 top = cross(centre, right);
    // The sprite's top row up, as the game drew it.
    return vec2(dot(dir, right), -dot(dir, top)) / size * 0.5 + 0.5;
}

vec2 fxTurn(vec2 v, vec2 cs)
{
    return vec2(cs.x * v.x + cs.y * v.y, cs.x * v.y - cs.y * v.x);
}

// ---- clouds without a texture: fractal noise on a layer ----
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
// by the cover; lit by the sky above and around (`horizon`: the atmosphere there) and by the light where the layer
// thins towards it (a directional derivative: Quilez, "Directional derivative", iquilezles.org), with a silver
// lining round the light. The rain clouds grow from the noise's high values down as their cover rises: grey, thick.
vec4 fxClouds(vec3 dir)
{
    vec4 cover = u_fxSky[12];
    if (dir.y <= 0.0 || cover.z + cover.w <= 0.0)
        return vec4_splat(0.0);
    vec2 uv = dir.xz / (dir.y + 0.12) * u_fxSky[14].w + cover.xy;
    float octaves = u_fxSky[13].w;
    float noise = fxCloudNoise(uv, octaves);
    float density = fxCloudDensity(noise, cover.z);
    float t = mix(1.1, -0.1, cover.w);
    float rain = smoothstep(t - 0.1, t + 0.1, noise) * step(0.0001, cover.w);

    vec3 light = u_fxSky[13].rgb;
    vec3 toLight = u_fxSky[14].xyz;
    float lit = 0.75;
    if (octaves > 2.5)
    {
        float towards = fxCloudDensity(fxCloudNoise(uv + toLight.xz * 0.03, 2.0), cover.z);
        lit = clamp(0.6 + 1.5 * (density - towards), 0.15, 1.0);
    }
    float thick = max(density, rain);
    float lining = pow(max(dot(dir, toLight), 0.0), 12.0) * (1.0 - thick) * 1.5;
    vec3 level = vec3(dir.x, 0.0, dir.z);
    level = dot(level, level) > 1e-8 ? normalize(level) : vec3(1.0, 0.0, 0.0);
    vec3 ambient = 0.5 * (fxSkyAtmosphere(vec3(0.0, 1.0, 0.0)).rgb + fxSkyAtmosphere(level).rgb);
    vec3 fair = ambient * mix(1.0, 0.8, thick) + light * (lit * mix(0.95, 0.6, thick) + lining);
    vec3 grey = ambient * 0.45 + light * 0.12;
    vec3 colour = mix(fair, grey, rain);
    // Thinning towards the horizon, where the layer is seen edge on.
    return vec4(colour, max(density, rain) * smoothstep(0.0, 0.1, dir.y));
}

// ---- height fog over the sky ----
// The fog's optical depth along `interval` metres between two heights, the density falling by 1/e per layer above its
// base and even below it.
float fxFogIntegral(float startHeight, float endHeight, float interval, float base, float inverseLayer)
{
    float low = min(startHeight, endHeight) - base;
    float high = max(startHeight, endHeight) - base;
    float below = high <= 0.0 ? 1.0 : clamp(-low / max(high - low, 0.000001), 0.0, 1.0);
    float x = max(high - max(low, 0.0), 0.0) * inverseLayer;
    float average = x < 0.01 ? 1.0 - x * 0.5 + x * x / 6.0 : (1.0 - exp(-x)) / x;
    return interval * (below + (1.0 - below) * exp(-max(low, 0.0) * inverseLayer) * average);
}

void main()
{
    vec3 dir = normalize(v_wpos);
    vec3 dx = dFdx(dir);
    vec3 dy = dFdy(dir);
    float day = u_fxSky[0].w;
    vec4 atmosphere = fxSkyAtmosphere(dir);
    vec3 sky = atmosphere.rgb;

    if (dir.y > 0.0)
    {
        float stars = day < 1.0 ? fxStars(dir, dx, dy) * (1.0 - day) : 0.0;
        sky += vec3_splat(stars);

        vec3 sun = u_fxSky[0].xyz;
        float sunSize = u_fxSky[4].w;
        float sunUp = smoothstep(-0.04, 0.03, sun.y);
        vec2 uv = fxDiscUv(dir, sun, max(sunSize, 1e-6));
        if (sunUp > 0.0 && sunSize > 0.0 && all(greaterThanEqual(uv, vec2_splat(0.0))) &&
            all(lessThanEqual(uv, vec2_splat(1.0))))
        {
            // The sprite turns with the camera's yaw.
            vec2 cs = u_fxSky[11].xy;
            vec2 uvDx = fxTurn(fxDiscUv(dir + dx, sun, sunSize) - uv, cs);
            vec2 uvDy = fxTurn(fxDiscUv(dir + dy, sun, sunSize) - uv, cs);
            uv = fxTurn(uv - 0.5, cs) + 0.5;
            float sunset = 1.0 - smoothstep(0.02, 0.22, sun.y);
            vec3 tint = mix(u_fxSky[4].rgb, vec3(1.0, 0.52, 0.22), sunset * 0.65);
            vec4 disc = texture2DGrad(s_fxSunDisc, uv, uvDx, uvDy);
            sky += (disc.rgb * tint - vec3_splat(stars * disc.a)) * sunUp;
        }

        vec3 moon = u_fxSky[1].xyz;
        float moonSize = u_fxSky[5].w;
        float moonUp = smoothstep(-0.04, 0.03, moon.y);
        uv = fxDiscUv(dir, moon, max(moonSize, 1e-6));
        if (moonUp > 0.0 && moonSize > 0.0 && all(greaterThanEqual(uv, vec2_splat(0.0))) &&
            all(lessThanEqual(uv, vec2_splat(1.0))))
        {
            vec4 disc = texture2DGrad(s_fxMoonDisc, uv, fxDiscUv(dir + dx, moon, moonSize) - uv,
                                      fxDiscUv(dir + dy, moon, moonSize) - uv);
            sky += (disc.rgb * mix(1.0, 0.25, day) - vec3_splat(stars * disc.a)) * moonUp;
        }
        // The moon's halo, dimmed by the air between it and the eye.
        float align = max(dot(dir, moon), 0.0);
        sky += u_fxSky[5].rgb *
               (10.0 * pow(align, 10000.0) * (1.0 - smoothstep(-0.05, 0.2, moon.y)) + pow(align, 3500.0)) *
               exp2(-atmosphere.a * 0.5) * 0.005 * moonUp * (1.0 - day);
    }

    vec4 clouds = fxClouds(dir);
    sky = mix(sky, clouds.rgb, clouds.a);

    // The haze band at the horizon, in the atmosphere's colour a little above it or in its own.
    vec4 haze = u_fxSky[6];
    if (haze.x > 0.0)
    {
        float t = clamp((1.0 - abs(dir.y) * haze.y) * haze.z, 0.0, 1.0);
        vec3 colour = u_fxSky[7].rgb;
        if (u_fxSky[7].w > 0.5)
        {
            vec2 azimuth = dir.xz * inversesqrt(max(dot(dir.xz, dir.xz), 0.000001));
            colour *= fxSkyAtmosphere(normalize(vec3(azimuth.x, haze.w, azimuth.y))).rgb;
        }
        sky = mix(sky, colour, haze.x * t * t * (3.0 - 2.0 * t));
    }

    // The height fog between the eye and the sky's distance.
    vec4 fog = u_fxSky[8];
    vec4 reach = u_fxSky[9];
    if (fog.x > 0.0 && fog.w > 0.0 && reach.y > reach.x)
    {
        float depth = fog.x * fxFogIntegral(reach.z + dir.y * reach.x, reach.z + dir.y * reach.y, reach.y - reach.x,
                                            fog.y, fog.z);
        vec3 colour = u_fxSky[10].rgb;
        if (reach.w > 0.5)
            colour *= fxSkyAtmosphere(normalize(vec3(dir.x, max(dir.y, 0.02), dir.z))).rgb;
        sky = mix(sky, colour, min(1.0 - exp(-depth), fog.w));
    }

    gl_FragColor = vec4(fxEncode(fxCompressHdr(max(sky, vec3_splat(0.0)) * u_fxSky[1].w)), 1.0);
}
