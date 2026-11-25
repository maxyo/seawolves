// The arithmetic of the hulls' wakes on the Sea2 pixel shader (sea_wakes.sh, ports/fs_port_sea2.sc) that needs no
// texture. Scalar arithmetic only: shared with the CPU check (fx/tests/sea_wakes_math_check.cpp).
#ifndef SEA_WAKES_MATH_SH
#define SEA_WAKES_MATH_SH

// The wakes' reach: 1 around the camera, falling to 0 over the last tenth of the way from the wake target's centre
// to its border, by the distance alone: a trail enters and leaves with the camera unseen, and where a long one ends
// does not follow the target's square. `u`, `v`: the target's texture coordinates.
float seaWakeReach(float u, float v)
{
    float away = 2.0 * sqrt((u - 0.5) * (u - 0.5) + (v - 0.5) * (v - 0.5)); // 0 at the centre, 1 at the border
    return 1.0 - smoothstep(0.9, 1.0, away);
}

// The second, finer reading of the foam texture for a wake's lace: `coordinate` is one of the first reading's, which
// is known only up to whole repeats (its origin is the sea's vertex origin, which moves with the camera and is kept
// as the fraction of a repeat). The factor is therefore a whole number: with any other, every repeat the origin
// passes would move the finer pattern by a fraction of its own (the lace jumped in place).
#define kSeaFoamFine 2.0
float seaFoamFine(float coordinate, float offset)
{
    return kSeaFoamFine * coordinate + offset;
}

// How much of a pixel the lace of a wake's foam covers. `streak`: the foam texture there (0 dark water .. 1 its
// brightest streak); `foam`: the amount of foam (0..1): the more of it, the more of the texture's streaks show.
float seaWakeLace(float streak, float foam)
{
    return smoothstep(-0.25, 0.3, streak - (1.0 - 1.1 * foam)) * min(1.6 * foam, 0.8);
}

// How much of a pixel is the pale aerated water under the lace.
float seaWakeAerated(float foam)
{
    return 0.45 * foam;
}

// The share of the light falling on it that a wake's foam gives back (foam mixed with the water it lies on).
#define kSeaFoamAlbedo 0.58
// The light of a wake's foam in one colour channel: the sky's above it (`sky`: the channel, `sky_grey`: its
// brightness; foam scatters it nearly white) and the sun's on the level sea (`sun`: the channel of the sun's colour
// times the sine of its height, nothing while clouds hide it or it is down), times the albedo. Without the sun the
// foam is never brighter than the sky that lights it.
float seaFoamLight(float sky, float sky_grey, float sun)
{
    return kSeaFoamAlbedo * min(mix(sky_grey, sky, 0.15) + sun, 1.0);
}

#endif
