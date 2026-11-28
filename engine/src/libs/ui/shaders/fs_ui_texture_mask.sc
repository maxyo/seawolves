$input v_color0, v_texcoord0
// The icon atlas's mask page (icons.cpp): R how much of the vertex colour shows; premultiplied out.
#include <bgfx_shader.sh>
SAMPLER2D(s_ui_tex, 0);
void main()
{
    gl_FragColor = v_color0 * texture2D(s_ui_tex, v_texcoord0).r;
}
