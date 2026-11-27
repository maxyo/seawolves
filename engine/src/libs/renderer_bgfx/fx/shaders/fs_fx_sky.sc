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
#if FX_SKY_CUBES
// A game's textured cloud layers (fx::Sky::SetCloudLayers): A the response to light from +x, +y, +z with the opacity
// in alpha, B the response to light from -x, -y, -z with the ambient response in alpha.
SAMPLERCUBE(s_fxCloudBaseA, 4);
SAMPLERCUBE(s_fxCloudBaseB, 5);
SAMPLERCUBE(s_fxCloudUpperA, 6);
SAMPLERCUBE(s_fxCloudUpperB, 7);
SAMPLERCUBE(s_fxCloudRainA, 8);
SAMPLERCUBE(s_fxCloudRainB, 9);
#endif

// [0] xyz towards the sun, w the sky's daylight (1 day .. 0 night)
// [1] xyz towards the moon, w the scene's exposure
// [2] rgb the day sky's radiance, w the stars' density (0..1: the share of cells without a star)
// [3] rgb the night sky's radiance
// [4] rgb the sun's light (linear), w its disc's half size (tangent of the angle; 0: no disc)
// [5] rgb the moon's light, w its disc's half size
// [6] haze: x opacity at the horizon, y 1 / the sine of its height, z 1 / its edge's fade
// [7] fog: x density per metre at its base, y the base's height, z 1 / the layer's thickness (0: even), w most opacity
// [8] fog: x where it begins, y how far the sky stands (0: no fog over the sky), z the eye's height, w its colour is a
//     tint of the atmosphere's
// [9] rgb the fog's colour
// [10] xy cos and sin of the camera's yaw (the sun's sprite turns with it)
// [11] xy the clouds' drift (noise tiles), z the fair-weather clouds' cover, w the rain clouds' cover
// [12] x the noise octaves (2..4; from 3 a tap towards the light shades the clouds), y noise tiles per cloud height
// [13] the clouds' look: x their radiance multiplier, y how much of the textured base and upper layers shows, z the
//      thin edges' light multiplier, w the share of dawn and sunset in the hour
// [14] transmission: x the sun's strength, y the moon's, z the sun's radius, w the moon's (degrees)
// [15] the low sun: x its light multiplier, y its light on the clouds' bases, z its warmth, w its colour on the edges
// [16] the day: x the sun's light, y the sky's light, z the sky's tint of it, w the sun's light's contrast
// [17] the twilight: x its strength away from the sun, y the width of that side (degrees), z its ambient light;
//      w the sky's light on the clouds
// [18] rgb the twilight's tint, w the moon's light on the clouds
// [19] rgb the fill light's colour, w its intensity
// [20..22] the upper, base and rain layers: xy the cosine and sine of its turn, z its radiance scale (0: no cubes),
//      w its ambient response's scale (below 0: the mean of its six responses)
// [23..26] rgb the six-way responses of the clouds without textures: fair-weather +x+y+z, -x-y-z, rain +, rain -
// [27..34] rgb the haze band's colour at the azimuths 0, 45 .. 315 degrees from +x towards +z
uniform vec4 u_fxSky[35];

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

// ---- cloud lighting (Caribbean Legend's textured clouds, its period-driven terms) ----
// A layer's response to light along `l` from its responses to light from the six axes.
float fxSixWay(vec3 positive, vec3 negative, vec3 l)
{
    return dot(mix(negative, positive, step(vec3_splat(0.0), l)), l * l);
}

// The response towards its peak raised to the contrast.
float fxShapeResponse(float response, float peak, float contrast)
{
    float top = max(peak, 0.0001);
    return top * pow(clamp(response / top, 0.0, 1.0), clamp(contrast, 0.5, 2.0));
}

// Light shining through a cloud near the sun or the moon (`solar` 1) : a halo and a core within the radius.
float fxTransmission(float opacity, float alignment, float height, float strength, float radiusDeg, float solar)
{
    float spread = max(1.0 - cos(radians(clamp(radiusDeg, 0.5, 45.0))), 0.000001);
    float distance = max(1.0 - clamp(alignment, -1.0, 1.0), 0.0) / spread;
    float mask = mix(0.75, 0.20, solar) * exp2(-4.0 * distance) +
                 mix(1.25, 3.80, solar) * exp2(-mix(24.0, 80.0, solar) * distance);
    float presence = clamp(opacity, 0.0, 1.0) * (2.0 - clamp(opacity, 0.0, 1.0));
    return max(strength, 0.0) * mask * presence * smoothstep(-0.02, 0.02, height);
}

// The larger of the two, with a soft shoulder where they meet.
float fxBlendTransmission(float response, float transmission)
{
    float width = transmission * 0.5;
    float overlap = max(width - abs(response - transmission), 0.0) / max(width, 0.000001);
    return max(response, transmission) + overlap * overlap * width * 0.25;
}

float fxMax3(vec3 v)
{
    return max(v.r, max(v.g, v.b));
}

// A cloud texel lit by the sun, the sky's fill, the twilight and the moon: `positive`, `negative` its six responses
// (times its radiance scale), `ambient` its response to the sky's light, `shadow` what of the direct light reaches it;
// `l`, `m` towards the sun and the moon in the layer's frame, `view` the view direction, `sky` the atmosphere behind.
vec4 fxCloudShade(vec3 positive, vec3 negative, float opacity, float ambient, float shadow, vec3 l, vec3 m, vec3 view,
                  vec3 sky)
{
    vec4 look = u_fxSky[13];
    vec4 low = u_fxSky[15];
    vec4 day = u_fxSky[16];
    vec4 dusk = u_fxSky[17];
    vec4 tint = u_fxSky[18];
    vec4 fill = u_fxSky[19];
    vec3 sun = u_fxSky[0].xyz;
    vec3 source = u_fxSky[4].rgb;
    float twilight = clamp(look.w, 0.0, 1.0);
    float edge = mix(1.0, clamp(look.z, 0.0, 4.0), 1.0 - smoothstep(0.20, 0.95, clamp(opacity, 0.0, 1.0)));
    float peak = fxMax3(max(positive, negative));
    float baked = fxSixWay(positive, negative, l) * shadow;
    float response = fxShapeResponse(baked, peak, day.w);
    response = fxBlendTransmission(response, fxTransmission(opacity, dot(view, sun), sun.y, u_fxSky[14].x,
                                                            u_fxSky[14].z, 1.0));

    // The sun's light, reddened and warmed by a low sun, and its light on the clouds' bases.
    float lowLight = mix(1.0, max(low.x, 0.0), twilight);
    float sunlight = max(day.x, 0.0) * lowLight;
    float brightness = fxMax3(source);
    vec3 sunsetColour =
        mix(vec3(1.0, 0.52, 0.16), vec3(1.0, 0.26, 0.025), 1.0 - smoothstep(0.02, 0.22, sun.y)) * brightness;
    vec3 direct = mix(source, mix(source, sunsetColour, twilight), low.z);
    float base = max(negative.g, 0.0) * max(low.y, 0.0) * smoothstep(-0.35, 1.0, dot(view, sun)) *
                 smoothstep(-0.02, 0.02, sun.y) * twilight * (1.0 - smoothstep(0.0, 0.18, sun.y));
    vec3 baseColour = mix(source, vec3(1.0, 0.10, 0.015) * brightness, clamp(low.z, 0.0, 1.0));
    vec3 sunRadiance = response * direct * sunlight * edge;

    // The sky's light: the fill, tinted towards the sky's hue at its luminance.
    vec3 luma = vec3(0.2126, 0.7152, 0.0722);
    float skyLuma = dot(max(sky, vec3_splat(0.0)), luma);
    vec3 skyHue = max(sky, vec3_splat(0.0)) * (dot(fill.rgb, luma) / max(skyLuma, 0.0001));
    vec3 ambientColour = mix(fill.rgb, skyHue, clamp(day.z, 0.0, 1.0) * smoothstep(0.0, 0.01, skyLuma));
    float strength = dusk.w * max(day.y, 0.0);
    vec3 radiance = sunRadiance + ambient * ambientColour * fill.w * strength;

    // A low sun's colour on the lit edges, at the brightness they have.
    float sunLuma = dot(max(sunRadiance, vec3_splat(0.0)), luma);
    if (sunLuma > 0.00001)
    {
        float total = dot(max(radiance, vec3_splat(0.0)), luma);
        float share = clamp(sunLuma / max(total, 0.00001), 0.0, 1.0);
        float lit = clamp(baked / max(peak, 0.0001), 0.0, 1.0);
        radiance = mix(radiance, sunRadiance * (total / sunLuma), clamp(low.w, 0.0, 1.0) * lit * share * twilight);
    }
    radiance += base * baseColour * sunlight * edge;

    // Away from a low sun the clouds take the twilight's tinted sky light, and at the horizon the sky itself.
    float across = sqrt(max(sun.x * sun.x + sun.z * sun.z, 0.000001));
    float towards = (view.x * sun.x + view.z * sun.z) / across;
    float side = sin(radians(clamp(dusk.y, 15.0, 180.0) * 0.5));
    float away = (1.0 - smoothstep(-side, side, towards)) * twilight * clamp(dusk.x, 0.0, 1.0);
    vec3 dim = clamp(tint.rgb, 0.0, 1.0);
    dim *= min(dot(max(ambientColour, vec3_splat(0.0)), luma), skyLuma) * max(dusk.z, 0.0) /
           max(dot(dim, luma), 0.0001);
    radiance = mix(radiance, ambient * dim * fill.w * strength, away);
    radiance = mix(radiance, sky * opacity, 0.25 * away * (1.0 - smoothstep(0.0, 0.18, abs(view.y))));

    // The moon's light, through the clouds round it too.
    vec3 moonLight = u_fxSky[5].rgb;
    if (fxMax3(moonLight) > 0.0)
    {
        vec3 moon = u_fxSky[1].xyz;
        float lunar = fxShapeResponse(fxSixWay(positive, negative, m) * shadow, peak, day.w);
        lunar = fxBlendTransmission(lunar, fxTransmission(opacity, dot(view, moon), moon.y, u_fxSky[14].y,
                                                          u_fxSky[14].w, 0.0));
        radiance += lunar * moonLight * edge * max(tint.w, 0.0);
    }
    return vec4(radiance * max(look.x, 0.0), opacity);
}

// A direction turned about the vertical by a layer's turn (cosine, sine).
vec3 fxTurnLayer(vec3 d, vec2 turn)
{
    return vec3(turn.x * d.x - turn.y * d.z, d.y, turn.y * d.x + turn.x * d.z);
}

// The rain clouds' reveal: three octaves of value noise over the direction, past a threshold that falls as their
// cover grows.
float fxValueNoise(vec3 p)
{
    vec3 cell = floor(p);
    vec3 f = p - cell;
    f = f * f * (3.0 - 2.0 * f);
    float a = mix(fxCellHash(cell).x, fxCellHash(cell + vec3(1.0, 0.0, 0.0)).x, f.x);
    float b = mix(fxCellHash(cell + vec3(0.0, 1.0, 0.0)).x, fxCellHash(cell + vec3(1.0, 1.0, 0.0)).x, f.x);
    float c = mix(fxCellHash(cell + vec3(0.0, 0.0, 1.0)).x, fxCellHash(cell + vec3(1.0, 0.0, 1.0)).x, f.x);
    float d = mix(fxCellHash(cell + vec3(0.0, 1.0, 1.0)).x, fxCellHash(cell + vec3(1.0, 1.0, 1.0)).x, f.x);
    return mix(mix(a, b, f.y), mix(c, d, f.y), f.z);
}

float fxRainReveal(vec3 dir, float cover)
{
    if (cover <= 0.0)
        return 0.0;
    if (cover >= 1.0)
        return 1.0;
    vec3 p = normalize(dir) * 5.0 + vec3(17.3, 9.2, -4.7);
    float n = fxValueNoise(p) * 0.65 + fxValueNoise(p * 2.03 + vec3(3.1, -7.2, 5.4)) * 0.25 +
              fxValueNoise(p * 4.11 + vec3(-8.4, 2.7, 11.6)) * 0.10;
    float feather = 0.055;
    float threshold = mix(1.0 + feather, -feather, cover);
    return smoothstep(threshold - feather, threshold + feather, n);
}

// A cloud layer over a background: its light added, the background dimmed by its opacity.
vec3 fxOver(vec3 background, vec4 cloud)
{
    return cloud.rgb + background * (1.0 - cloud.a);
}

#if FX_SKY_CUBES
// One textured layer along `dir` (its uniform `layer`, the cubes A and B sampled at the turned direction).
vec4 fxShadeLayer(vec4 a, vec4 b, vec4 layer, vec3 dir, vec3 sky)
{
    vec3 l = fxTurnLayer(u_fxSky[0].xyz, layer.xy);
    vec3 m = fxTurnLayer(u_fxSky[1].xyz, layer.xy);
    vec3 positive = a.rgb * layer.z;
    vec3 negative = b.rgb * layer.z;
    float ambient = layer.w >= 0.0 ? b.a * layer.w : dot(positive + negative, vec3_splat(1.0 / 6.0));
    return fxCloudShade(positive, negative, clamp(a.a, 0.0, 1.0), ambient, 1.0, l, m, dir, sky);
}

// The rain layer revealed by its cover (opaque, it replaces the sky), the base layer over the upper one, both shown
// by the hour's opacity.
vec3 fxTexturedClouds(vec3 background, vec3 sky, vec3 dir)
{
    vec4 rain = vec4_splat(0.0);
    vec4 rainLayer = u_fxSky[22];
    float cover = clamp(u_fxSky[11].w, 0.0, 1.0);
    if (cover > 0.0 && rainLayer.z > 0.0)
    {
        vec3 local = fxTurnLayer(dir, rainLayer.xy);
        float reveal = fxRainReveal(local, cover);
        if (reveal > 0.0)
        {
            rain = fxShadeLayer(textureCubeLod(s_fxCloudRainA, local, 0.0), textureCubeLod(s_fxCloudRainB, local, 0.0),
                                rainLayer, dir, sky) * reveal;
            if (rain.a >= 1.0)
                return rain.rgb;
        }
    }
    vec3 result = background;
    float shown = clamp(u_fxSky[13].y, 0.0, 1.0);
    if (shown > 0.0)
    {
        vec4 baseLayer = u_fxSky[21];
        vec3 local = fxTurnLayer(dir, baseLayer.xy);
        vec4 lower = vec4_splat(0.0);
        if (baseLayer.z > 0.0)
            lower = fxShadeLayer(textureCubeLod(s_fxCloudBaseA, local, 0.0),
                                 textureCubeLod(s_fxCloudBaseB, local, 0.0), baseLayer, dir, sky);
        vec4 upperLayer = u_fxSky[20];
        if (lower.a < 1.0 && upperLayer.z > 0.0)
        {
            local = fxTurnLayer(dir, upperLayer.xy);
            result = fxOver(result, fxShadeLayer(textureCubeLod(s_fxCloudUpperA, local, 0.0),
                                                 textureCubeLod(s_fxCloudUpperB, local, 0.0), upperLayer, dir, sky));
        }
        result = mix(background, fxOver(result, lower), shown);
    }
    return fxOver(result, rain);
}
#endif

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

#if !FX_SKY_CUBES
// The clouds over `dir` lit as the textured layers are: the rain clouds revealed by their cover (opaque, they
// replace the sky), and a layer at the cloud height seen from below, its noise thresholded by the fair-weather cover,
// a tap towards the light shading where it thins (a directional derivative: Quilez, "Directional derivative",
// iquilezles.org).
vec3 fxNoiseClouds(vec3 background, vec3 sky, vec3 dir)
{
    vec4 cover = u_fxSky[11];
    vec3 sun = u_fxSky[0].xyz;
    vec3 moon = u_fxSky[1].xyz;
    vec4 rain = vec4_splat(0.0);
    vec2 turn = u_fxSky[22].xy;
    float reveal = fxRainReveal(fxTurnLayer(dir, turn), clamp(cover.w, 0.0, 1.0));
    if (reveal > 0.0)
    {
        float ambient = dot(u_fxSky[25].rgb + u_fxSky[26].rgb, vec3_splat(1.0 / 6.0));
        rain = fxCloudShade(u_fxSky[25].rgb, u_fxSky[26].rgb, 1.0, ambient, 1.0, fxTurnLayer(sun, turn),
                            fxTurnLayer(moon, turn), dir, sky) * reveal;
        if (rain.a >= 1.0)
            return rain.rgb;
    }
    vec3 result = background;
    if (dir.y > 0.0 && cover.z > 0.0)
    {
        vec2 uv = dir.xz / (dir.y + 0.12) * u_fxSky[12].y + cover.xy;
        float octaves = u_fxSky[12].x;
        float density = fxCloudDensity(fxCloudNoise(uv, octaves), cover.z);
        float shadow = 1.0;
        if (octaves > 2.5)
        {
            vec3 toLight = sun.y > 0.0 ? sun : moon;
            float towards = fxCloudDensity(fxCloudNoise(uv + toLight.xz * 0.03, 2.0), cover.z);
            shadow = clamp(0.6 + 1.5 * (density - towards), 0.15, 1.0) / 0.6;
        }
        // Thinning towards the horizon, where the layer is seen edge on.
        float opacity = density * smoothstep(0.0, 0.1, dir.y);
        float ambient = dot(u_fxSky[23].rgb + u_fxSky[24].rgb, vec3_splat(1.0 / 6.0)) * opacity;
        result = fxOver(result, fxCloudShade(u_fxSky[23].rgb * opacity, u_fxSky[24].rgb * opacity, opacity,
                                             ambient, shadow, sun, moon, dir, sky));
    }
    return fxOver(result, rain);
}
#endif

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
            vec2 cs = u_fxSky[10].xy;
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

#if FX_SKY_CUBES
    sky = fxTexturedClouds(sky, atmosphere.rgb, dir);
#else
    sky = fxNoiseClouds(sky, atmosphere.rgb, dir);
#endif

    // The haze band at the horizon, in the atmosphere's colour a little above it or in its own.
    vec4 haze = u_fxSky[6];
    if (haze.x > 0.0)
    {
        float t = clamp((1.0 - abs(dir.y) * haze.y) * haze.z, 0.0, 1.0);
        // Its colour round the horizon (fx::SkyState::haze: the sky's a little above the horizon under the rain
        // clouds, tinted, or its own), between the two nearest azimuths.
        float at = atan2(dir.z, dir.x) * (8.0 / 6.2831853);
        at = at < 0.0 ? at + 8.0 : at;
        float k = min(floor(at), 7.0);
        int i = int(k);
        vec3 colour = mix(u_fxSky[27 + i].rgb, u_fxSky[27 + (i + 1) - 8 * ((i + 1) / 8)].rgb, at - k);
        sky = mix(sky, colour, haze.x * t * t * (3.0 - 2.0 * t));
    }

    // The height fog between the eye and the sky's distance.
    vec4 fog = u_fxSky[7];
    vec4 reach = u_fxSky[8];
    if (fog.x > 0.0 && fog.w > 0.0 && reach.y > reach.x)
    {
        float depth = fog.x * fxFogIntegral(reach.z + dir.y * reach.x, reach.z + dir.y * reach.y, reach.y - reach.x,
                                            fog.y, fog.z);
        vec3 colour = u_fxSky[9].rgb;
        if (reach.w > 0.5)
            colour *= fxSkyAtmosphere(normalize(vec3(dir.x, max(dir.y, 0.02), dir.z))).rgb;
        sky = mix(sky, colour, min(1.0 - exp(-depth), fog.w));
    }

    gl_FragColor = vec4(fxEncode(fxCompressHdr(max(sky, vec3_splat(0.0)) * u_fxSky[1].w)), 1.0);
}
