#include "ports.hpp"
#include "device.hpp"
#include "../../shaders/include/port_registers.sh"

namespace storm::shim
{
namespace
{
// The 11 vs_1_1 / ps_1_x shaders referenced by src/techniques (see shaders/ports) and the coast foam's pair
// (technique CoastFoam, src/libs/island/src/foam.cpp).
const ShaderPort ports[] = {
    {"grass_main", true, "vs_port_grass", 0, PORT_VSC_GRASS},
    {"particles_vertex_shader", true, "vs_port_particles", 0, PORT_VSC_PARTICLES},
    {"particles_pixel_shader", false, "fs_port_particles", 4, 0},
    {"postprocess_shader", false, "fs_port_postprocess", 4, 0},
    {"caustic_vertex_shader", true, "vs_port_caustic", 0, PORT_VSC_CAUSTIC},
    {"caustic_pixel_shader", false, "fs_port_caustic", 3, 0, true},
    {"sea_sea2_vertex_shader", true, "vs_port_sea2", 0, PORT_VSC_SEA, false, true, true},
    {"sea_sea2_pixel_shader", false, "fs_port_sea2", 4, 0, false, true},
    {"sea_sea3_vertex_shader", true, "vs_port_sea3", 0, PORT_VSC_SEA, false, false, true},
    {"sea_sea3_pixel_shader", false, "fs_port_sea3", 4, 0},
    {"worldmap_shader", true, "vs_port_worldmap", 0, PORT_VSC_WORLDMAP},
    {"coastfoam_foam_vertex_shader", true, "vs_port_coastfoam", 0, PORT_VSC_COASTFOAM},
    {"coastfoam_foam_pixel_shader", false, "fs_port_coastfoam", 2, 0},
};
} // namespace

uint32_t ShaderPort::shader_id() const
{
    return shader_id_by_name(shader_name);
}

const ShaderPort *find_port(std::string_view key, bool vertex)
{
    for (const ShaderPort &port : ports)
        if (port.key == key && port.vertex == vertex)
            return &port;
    return nullptr;
}
} // namespace storm::shim
