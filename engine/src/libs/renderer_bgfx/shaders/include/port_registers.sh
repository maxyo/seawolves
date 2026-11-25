// The constant registers each vertex port reads, c0..c[count-1]: the size of its u_vsc array (port_vs.sh), so a
// register past it does not compile, and the count the shim uploads (src/shim/ports.cpp includes this file).
#define PORT_VSC_CAUSTIC 16
#define PORT_VSC_COASTFOAM 72 // CoastFoam kFoamsPerDraw = 64
#define PORT_VSC_GRASS 82
#define PORT_VSC_PARTICLES 14
#define PORT_VSC_SEA 59 // Sea2 and Sea3
#define PORT_VSC_WORLDMAP 7
