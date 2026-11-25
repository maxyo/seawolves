// The churned water of a hull by its age (fx::SeaWakes, fs_fx_wake.sc): how much foam lies on it and how much it
// calms the sea. Scalar arithmetic only: shared with the CPU check (tests/wake_trail_check.cpp).
#ifndef FX_WAKE_TRAIL_SH
#define FX_WAKE_TRAIL_SH

// A smooth pattern (0..1) fixed in the water, about 10 m and 25-35 m across: where foam gathers and where it breaks
// up first. `x`, `z`: metres from fx::WakeTrails::PatternBase; whole waves over its 1024 m tile along both.
float fxWakeClump(float x, float z)
{
    float k = 6.2831853 / 1024.0;
    float n1 = sin((103.0 * x + 44.0 * z) * k + 2.0 * sin((-28.0 * x + 66.0 * z) * k));
    float n2 = sin((-18.0 * x + 36.0 * z) * k + 1.7 * sin((27.0 * x + 13.0 * z) * k) + 1.0);
    return 0.5 + 0.25 * (n1 + n2);
}

// A band's cross-section: 1 within `inner` metres of its centre line, falling to 0 at `outer` metres, the edge pushed
// in and out by the pattern by up to a quarter of that fall (the ribbon reaches farther: fx::WakeTrails::kUneven).
// `side`: metres off the centre line.
float fxWakeBand(float side, float outer, float inner, float clump)
{
    return 1.0 - smoothstep(inner, outer, abs(side) + 0.5 * (clump - 0.5) * (outer - inner));
}

#define kWakeFreshLife 2.5 // seconds the dense foam right behind the stern takes to fall to 1/e
#define kWakeYoung 3.0     // seconds: until then the foam lies evenly over the band
#define kWakeOld 14.0      // seconds: from then it lies along the band's edges, in patches
#define kWakeBowLife 2.0   // seconds the white at the stem takes to fall to 1/e of what it thins by

// Foam (0..1) on the trail astern: dense for the first seconds, then what lasts (`life` seconds to 1/e); as it ages
// it gathers along the band's two edges and breaks into patches. `across`: 0 on the centre line .. 1 at the edge.
float fxWakeTrailFoam(float band, float across, float strength, float age, float clump, float life)
{
    float old = smoothstep(kWakeYoung, kWakeOld, age);
    float rim = smoothstep(0.3, 0.65, across) * band;
    float foam =
        strength * (0.5 * exp(-age / kWakeFreshLife) + 0.6 * exp(-age / life)) * mix(band, max(0.5 * band, rim), old);
    return clamp(foam * mix(1.0, smoothstep(0.25, 0.7, clump), 0.85 * old), 0.0, 1.0);
}

// How much (0..1) the trail calms the sea's own small waves: the band of damped water that outlasts the foam, gone
// after `life` seconds.
float fxWakeCalm(float band, float strength, float age, float life)
{
    return clamp(sqrt(strength) * band * (1.0 - smoothstep(0.5 * life, life, age)), 0.0, 1.0);
}

// Foam (0..1) of the bow wave beside the hull: white where the stem cuts the water, thinning within a few seconds
// to a streak that runs along the waterline into the trail. `age`: seconds since the stem met the water there.
float fxWakeBowFoam(float band, float strength, float age)
{
    return clamp(strength * (0.55 + 0.45 * exp(-age / kWakeBowLife)) * band, 0.0, 1.0);
}

#endif
