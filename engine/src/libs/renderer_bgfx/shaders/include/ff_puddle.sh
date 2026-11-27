// One puddle look for every game (renderer_bgfx README, "Puddles"). Included by ff_wet.sh. A game's puddle meshes
// give the bed as their mask (fs_ff_pbr.sc); a game without them gets the relief's hollows (ff_wet.sh). Growth,
// edges and colours are Caribbean Legend's: the water covers the bed where the mask passes a threshold falling from
// 1 to 0.01 as wetness^0.65 rises, a soaked ring lies just outside it, and both lie over the ground as dark water.

// x: the water 0..1, y: the soaked ring 0..1. mask: the bed (1 the deepest, the first to fill); wetness: the pixel's.
vec2 ffPuddle(float mask, float wetness)
{
    if (wetness <= 1e-4)
        return vec2_splat(0.0);
    float threshold = mix(1.0, 0.01, pow(wetness, 0.65));
    float ring = max(threshold - 0.045, 0.001);
    float water = smoothstep(threshold, threshold + 0.025, mask);
    return vec2(water, clamp(smoothstep(ring, ring + 0.025, mask) - water, 0.0, 1.0));
}

// What lies over the ground: rgb the colour (linear), a its share. The water is nearly black and covers 70 % of the
// ground's colour, the ring is black at 45 %.
vec4 ffPuddleCover(vec2 puddle)
{
    float water = 0.7 * puddle.x;
    float a = clamp(water + 0.45 * puddle.y, 0.0, 1.0);
    return vec4(vec3(0.055, 0.052, 0.045) * (water / max(a, 1e-5)), a);
}

// The smoothness of the puddle's water.
#define FF_PUDDLE_ROUGHNESS 0.02

float ffPuddleHash(vec2 p)
{
    vec3 q = fract(vec3(p.xyx) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}

// One layer of drops: in each cell (1 x 1 in p) a drop's ring spreads from a point of it and fades out, `rate` rings
// a second. The slope of the water there (along p).
vec2 ffRippleLayer(vec2 p, float time, float rate, float seed)
{
    vec2 cell = floor(p);
    float h = ffPuddleHash(cell + seed);
    vec2 d = p - cell - (0.25 + 0.5 * vec2(h, ffPuddleHash(cell.yx + seed + 7.0)));
    float life = fract(time * rate + h);
    float r = max(length(d), 1e-4);
    float x = (r - 0.25 * life) * 40.0; // across the ring, in radians of its wave
    float wave = cos(x) * clamp(1.0 - abs(x) / 6.2832, 0.0, 1.0) * (1.0 - life) * (1.0 - life);
    return d / r * wave;
}

// The rain's rings on the water at world xz: its slope (dh/dx, dh/dz). rain: falling now 0..1; time: seconds, wrapped
// at effects::kRippleSeconds, which the rates repeat over.
vec2 ffPuddleRipples(vec2 xz, float rain, float time)
{
    vec2 p = xz * 3.0; // drops a third of a metre apart
    return (ffRippleLayer(p, time, 0.9, 0.0) + ffRippleLayer(p + vec2(0.5, 0.5), time, 1.1, 13.0)) * (0.12 * rain);
}
