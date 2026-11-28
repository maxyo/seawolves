$input a_position, a_texcoord0, a_color0
$output v_color0, v_color1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_fog
// The coast foam strands of src/libs/island/src/foam.cpp (CoastFoam, technique CoastFoam of
// src/techniques/weather/CoastFoam.fx), drawn from the static buffer CoastFoam::BuildBuffers fills once per foam file.
// What CoastFoam::ExecuteFoamType2 once computed for every vertex of every frame on the CPU is done here: the
// strand's place between the work part's two ends for its move, its alpha, its texture coordinates, and the sea's
// surface there: the foam floats, so its point moves with the water's (the wave field's displacement, sea_surface.sh).
//
// Vertex (CoastFoam::GpuVertex, 24 bytes):
//   a_position: the work part's ends (v[0].x, v[0].z, v[1].x, v[1].z)
//   a_texcoord0.x: the work part's tu
//   a_color0 (UBYTE4N): x row 0..7 of the strand, y the edge alpha * 4 (0..4), zw the foam's index (low, high byte)
//
// Registers (CoastFoam::DrawStrands, island/src/foam_gpu.hpp):
//   c0..c3   view * projection, as columns
//   c4..c6   the sea's cascades for world positions (sea_gpu::cascade_register)
//   c7       x, y the camera's x, z, z the sea's reach squared (beyond it the sea is flat, as WaveXZ returns), w the
//            still water's level plus the foam's lift
//   c8       x the first foam of the draw
//   c9 + i   foam (c8.x + i)'s strand of this draw: x move, y alpha, z texture offset, w 1 when drawn
//
// s_vtex (D3DVERTEXTEXTURESAMPLER0): the sea's wave field.
#include "port_registers.sh"
#define PORT_VSC_COUNT PORT_VSC_COASTFOAM
#include "port_vs.sh"
#include "sea_surface.sh"

void main()
{
    float foam = floor(a_color0.z * 255.0 + 0.5) + floor(a_color0.w * 255.0 + 0.5) * 256.0;
    vec4 strand = u_vsc[9 + int(foam - u_vsc[8].x)];
    float row = floor(a_color0.x * 255.0 + 0.5);
    float dy = row / 7.0;
    vec4 oPos = vec4(0.0, 0.0, 2.0, 1.0); // a strand not drawn: every corner here, no area
    vec4 oD0 = vec4_splat(0.0);
    vec4 oT0 = vec4(0.0, 0.0, 0.0, 1.0);
    if (strand.w > 0.5)
    {
        vec2 xz = a_position.xy + (strand.x * dy) * (a_position.zw - a_position.xy);
        vec3 offset = vec3_splat(0.0);
        vec2 d = xz - u_vsc[7].xy;
        if (dot(d, d) <= u_vsc[7].z)
        {
            vec3 normal;
            offset = seaField(xz, 4, normal);
        }
        vec3 pos = vec3(xz.x + offset.x, u_vsc[7].w + offset.y, xz.y + offset.z);
        oPos = u_vsc[0] * pos.x + u_vsc[1] * pos.y + u_vsc[2] * pos.z + u_vsc[3];
        float edge = floor(a_color0.y * 255.0 + 0.5) * 0.25;
        oD0 = vec4(1.0, 1.0, 1.0, strand.y * clamp(2.5 * (1.0 - dy) * dy, 0.0, 1.0) * edge);
        oT0.xy = vec2(strand.z + a_texcoord0.x, 1.0 - dy);
    }
    vec4 none = vec4(0.0, 0.0, 0.0, 1.0);
    PORT_VS_OUTPUT(oPos, oD0, vec4_splat(0.0), oT0, none, none, none, none, 1.0);
}
