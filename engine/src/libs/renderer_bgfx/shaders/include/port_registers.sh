// The constant registers each vertex port reads, c0..c[count-1]: the size of its u_vsc array (port_vs.sh), so a
// register past it does not compile, and the count the shim uploads (src/shim/ports.cpp includes this file).
#define PORT_VSC_CAUSTIC 16
#define PORT_VSC_COASTFOAM 73 // CoastFoam foam_gpu::kRegisters (kFoamsPerDraw = 64)
#define PORT_VSC_GRASS 82
#define PORT_VSC_PARTICLES 14
#define PORT_VSC_SEA 14 // Sea2 (src/libs/sea/src/sea.cpp)
#define PORT_VSC_WORLDMAP 7
