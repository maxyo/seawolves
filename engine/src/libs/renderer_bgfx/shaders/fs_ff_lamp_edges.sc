// A layer of the edge maps of the lamps' still faces (renderer_bgfx/README.md, "Lamps"; src/effects/lamp_shadows.hpp):
// per texel the nearest (even columns) or the farthest (odd) depth of the still face around its cell.
#include "storm_common.sh"

SAMPLER2D(s_fxLampDepth, 0); // the still atlas, its depth as stored (no compare)
// x: the face size in texels, y: what this layer's filter reads beyond a cell's own texels (the 5x5 three, the 3x3
// two), zw: the atlas's last texel
uniform vec4 u_fxLampEdges;

// A depth read as a float and stored again may round by one step of a 24-bit map each way: the pair stays on the
// safe side of both.
#define LAMP_EDGE_SLACK (2.0 / 16777216.0)

void main()
{
    float size = u_fxLampEdges.x;
    vec2 at = floor(gl_FragCoord.xy);
    float pair = floor(at.x * 0.5);
    // A slot's row holds its faces 0..2 over the first half of its height, 3..5 over the second.
    float row = floor(at.y / size);
    float lower = at.y - row * size < 0.5 * size ? 0.0 : 1.0;
    vec2 cell = vec2(pair + lower * 1.5 * size, at.y - (row + lower) * 0.5 * size);
    int reach = int(u_fxLampEdges.y + 0.5);
    float nearest = 1.0;
    float farthest = 0.0;
    for (int dy = -reach; dy <= reach; ++dy)
    {
        for (int dx = -reach; dx <= reach; ++dx)
        {
            // (A cell at a face's border takes texels of the next face: no receiver reads it, fx::kLampFaceGuard.)
            vec2 texel = clamp(2.0 * cell + vec2(float(dx), float(dy)), vec2_splat(0.0), u_fxLampEdges.zw);
            float depth = texelFetch(s_fxLampDepth, ivec2(texel), 0).x;
            nearest = min(nearest, depth);
            farthest = max(farthest, depth);
        }
    }
    if (at.x - 2.0 * pair > 0.5)
        gl_FragDepth = min(farthest + LAMP_EDGE_SLACK, 1.0);
    else
        gl_FragDepth = max(nearest - LAMP_EDGE_SLACK, 0.0);
    gl_FragColor = vec4_splat(0.0);
}
