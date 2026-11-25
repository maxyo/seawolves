// One compare between two texels, as the receivers read a pair of the lamps' edge maps (ff_lamps.sh
// ffLampVisibility): src/effects/lamp_shadows.hpp asks a device once whether it is the mean of the two.
#include "storm_common.sh"

SAMPLER2DARRAYSHADOW(s_fxLampPair, 0);
uniform vec4 u_fxLampPair; // xy: the point between the two texels, z: their layer, w: the depth compared

void main()
{
    float passed = shadow2DArray(s_fxLampPair, vec4(u_fxLampPair.xy, u_fxLampPair.z, u_fxLampPair.w));
    gl_FragColor = vec4(passed, passed, passed, 1.0);
}
