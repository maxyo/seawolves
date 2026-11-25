$input v_wpos, v_misc
// The churned water of one hull on a ribbon of fx::WakeTrails (include/fx_wake_trail.sh): R the foam on it, G how
// much it calms the sea, both 0..1. Blended as the maximum: where bands cross, the fresher water shows.
#include "fx_common.sh"
#include "fx_wake_trail.sh"

// x: seconds the trail's lasting foam takes to fall to 1/e, y: seconds its calmed band lasts, z: 1 = the bow wave's
// band (WakeTrails::BowBand), 0 = the trail astern, w unused
uniform vec4 u_wake;

void main()
{
    float side = v_wpos.x;
    float strength = v_wpos.y;
    float half_width = v_wpos.z;
    float age = v_misc.w;
    float clump = fxWakeClump(v_misc.x, v_misc.y);
    float band = fxWakeBand(side, half_width, v_misc.z, clump);
    vec2 water = u_wake.z > 0.5
                     ? vec2(fxWakeBowFoam(band, strength, age), 0.0)
                     : vec2(fxWakeTrailFoam(band, abs(side) / half_width, strength, age, clump, u_wake.x),
                            fxWakeCalm(band, strength, age, u_wake.y));
    gl_FragColor = vec4(water, 0.0, 0.0);
}
