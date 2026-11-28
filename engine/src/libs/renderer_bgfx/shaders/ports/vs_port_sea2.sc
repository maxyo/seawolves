$input a_position, a_normal, a_texcoord0
$output v_texcoord0, v_texcoord1, v_texcoord4, v_fog, v_foam
// The sea's vertex program (technique Sea2 of src/techniques/weather/sea.fx): the wave field's surface
// (sea_surface.sh); the pixel program shades it from the field's slopes per pixel (fs_port_sea2.sc).
// Registers (src/libs/sea/src/sea.cpp): c0 x the fog's exp2 factor per clip-space depth, y Sea.FoamV, z Sea.FoamK;
// c4..c8 the surface (sea_surface_gpu.hpp); c9..c12 world * view * projection; c13 the camera.
#include "port_registers.sh"
#define PORT_VSC_COUNT PORT_VSC_SEA
#include "port_vs.sh"
#include "sea_surface.sh"

void main()
{
    vec4 pos, normal;
    seaVertex(a_position, a_normal, pos, normal);
    // The point of the still water the vertex is displaced from (metres from the mesh's origin): the flat grid's,
    // or the CPU's in the texture coordinates.
    vec2 still = u_vsc[8].w != 0.0 ? a_position.xz : a_texcoord0.xy;
    vec4 clip = vec4(dot(pos, u_vsc[9]), dot(pos, u_vsc[10]), dot(pos, u_vsc[11]), dot(pos, u_vsc[12]));

    v_texcoord0 = vec4(still, 0.0, 0.0);
    v_texcoord1 = vec4(u_vsc[13].xyz - pos.xyz, 0.0); // towards the eye
    v_texcoord4 = vec4(0.0, 0.0, pos.xz);              // the sea's own position, for the wakes (sea_wakes.sh)
    v_fog = vec4(exp2(clip.z * u_vsc[0].x), 1.0, clip.w, 0.0);
    // The game's foam layer (technique Sea2_Foam, folded into this draw): its alpha rises with the height.
    v_foam = vec4(0.0, 0.0, max((pos.y - u_vsc[0].y) * u_vsc[0].z, 0.0), 0.0);
    gl_Position = mul(u_ffProj, clip);
}
