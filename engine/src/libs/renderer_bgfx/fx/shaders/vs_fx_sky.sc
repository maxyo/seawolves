$input a_position, a_texcoord0
$output v_wpos
// Full-screen triangle at the far plane; a_texcoord0: the world direction through the pixel (fx::Sky::Draw).
#include "fx_common.sh"

void main()
{
    gl_Position = vec4(a_position.xy, 1.0, 1.0);
    v_wpos = a_texcoord0;
}
