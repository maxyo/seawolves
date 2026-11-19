$input v_color0, v_color1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_fog, v_shadow
// The plain fixed-function combiner for surfaces that hold their own static light (lightmaps, baked colours) and
// stand under moving casters: characters and ships shade them from the sun map's dynamic layer
// (fx_sun_map.sh fxSunGround, its 3x3 tent; src/effects/lighting.cpp picks it per draw).
#define FX_FF_DYN 1
#include "ff_fragment.sh"
