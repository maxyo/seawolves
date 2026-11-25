$input v_color0, v_color1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_fog, v_normal, v_position, v_matDiffuse, v_matAmbient
// The depth pre-pass copy of a draw with vs_ff_lit (Device::submit_prepass): depth only, the colour is not written.
#include "storm_common.sh"

void main()
{
    gl_FragColor = vec4_splat(0.0);
}
