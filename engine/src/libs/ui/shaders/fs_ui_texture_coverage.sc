$input v_color0, v_texcoord0
// The icon atlas's coverage page (icons.cpp): R how much of the vertex colour shows, G coverage; premultiplied out.
#include <bgfx_shader.sh>
SAMPLER2D(s_ui_tex, 0);
void main()
{
    vec2 texel = texture2D(s_ui_tex, v_texcoord0).rg;
    gl_FragColor = vec4(v_color0.rgb * texel.r, v_color0.a * texel.g);
}
