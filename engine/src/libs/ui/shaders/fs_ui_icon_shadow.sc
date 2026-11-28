$input v_color0, v_texcoord0
// A faded icon's shadow (icons.cpp Meshes::FadedShadow): the shadow cell scaled by (1 - a) / (1 - fade * a), a the
// glyph's alpha read from its occluder cell on the same page, so that shadow and glyph add up to the icon faded whole.
#include <bgfx_shader.sh>
SAMPLER2D(s_ui_tex, 0);
uniform vec4 u_ui_icon[5]; // channel of the tint factor, channel of the coverage, occluder map (xy scale, zw offset),
                           // occluder cell bounds (xy low, zw high), x fade, y the glyph colour's alpha
void main()
{
    vec4 shadow = texture2D(s_ui_tex, v_texcoord0);
    vec2 at = clamp(v_texcoord0 * u_ui_icon[2].xy + u_ui_icon[2].zw, u_ui_icon[3].xy, u_ui_icon[3].zw);
    float glyph = dot(texture2D(s_ui_tex, at), u_ui_icon[1]) * u_ui_icon[4].y;
    float keep = (1.0 - glyph) / max(1.0 - u_ui_icon[4].x * glyph, 1.0 / 1024.0);
    gl_FragColor = vec4(v_color0.rgb * dot(shadow, u_ui_icon[0]), v_color0.a * dot(shadow, u_ui_icon[1])) * keep;
}
