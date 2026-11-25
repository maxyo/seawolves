$input a_position, a_normal, a_texcoord0
$output v_wpos, v_misc
// A ribbon of fx::WakeTrails in the wake target of fx::SeaWakes (wake_trail.hpp: WakeVertex): a_position xy clip,
// z the water's age; a_normal: metres off the band's centre line, how hard the water was churned, the band's half
// width; a_texcoord0: the pattern's coordinates (x, z), how far off the centre line the foam is whole.
#include "fx_common.sh"

void main()
{
    gl_Position = vec4(a_position.xy, 0.0, 1.0);
    v_wpos = a_normal;
    v_misc = vec4(a_texcoord0, a_position.z);
}
