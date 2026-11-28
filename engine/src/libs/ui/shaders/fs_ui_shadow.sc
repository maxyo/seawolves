$input v_color0, v_texcoord0
// A box shadow (shadow_element.cpp): a rounded rectangle blurred by a Gaussian, worked out in closed form (the
// integral across x by erf, along y by four samples), in the element's pixels (v_texcoord0). An outer shadow is cut
// out of the element's border box, an inset one is the blurred complement of its shape, kept to the padding box.
#include <bgfx_shader.sh>
uniform vec4 u_ui_shadow[5]; // shape (xy centre, zw half size), its corner radii (tl tr br bl), the cut box and its
                             // radii as the shape's, x sigma (0: a hard edge), y 1 for an inset shadow

float CornerRadius(vec2 p, vec4 radii)
{
    return p.x > 0.0 ? (p.y > 0.0 ? radii.z : radii.y) : (p.y > 0.0 ? radii.w : radii.x);
}

// Signed distance to a rounded box centred at the origin; negative inside.
float BoxDistance(vec2 p, vec2 half_size, vec4 radii)
{
    float r = min(CornerRadius(p, radii), min(half_size.x, half_size.y));
    vec2 q = abs(p) - half_size + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

// Pixel coverage of the box, its edge smoothed over one pixel.
float BoxCoverage(vec2 p, vec2 half_size, vec4 radii)
{
    return clamp(0.5 - BoxDistance(p, half_size, radii), 0.0, 1.0);
}

vec2 Erf(vec2 x)
{
    vec2 s = sign(x);
    vec2 a = abs(x);
    x = 1.0 + (0.278393 + (0.230389 + 0.078108 * (a * a)) * a) * a;
    x *= x;
    return s - s / (x * x);
}

// The blurred box across x at the height y, for one corner radius.
float BlurredRow(float x, float y, float sigma, float corner, vec2 half_size)
{
    float delta = min(half_size.y - corner - abs(y), 0.0);
    float curved = half_size.x - corner + sqrt(max(0.0, corner * corner - delta * delta));
    vec2 integral = 0.5 + 0.5 * Erf((x + vec2(-curved, curved)) * (sqrt(0.5) / sigma));
    return integral.y - integral.x;
}

float BlurredBox(vec2 p, vec2 half_size, vec4 radii, float sigma)
{
    float corner = min(CornerRadius(p, radii), min(half_size.x, half_size.y));
    float low = p.y - half_size.y;
    float high = p.y + half_size.y;
    float start = clamp(-3.0 * sigma, low, high);
    float end = clamp(3.0 * sigma, low, high);
    float dy = (end - start) / 4.0;
    float y = start + dy * 0.5;
    float value = 0.0;
    for (int i = 0; i < 4; i++)
    {
        float gauss = exp(-(y * y) / (2.0 * sigma * sigma)) / (2.5066283 * sigma);
        value += BlurredRow(p.x, p.y - y, sigma, corner, half_size) * gauss * dy;
        y += dy;
    }
    return value;
}

void main()
{
    vec2 p = v_texcoord0;
    vec2 shape = p - u_ui_shadow[0].xy;
    float sigma = u_ui_shadow[4].x;
    float inside = sigma > 0.0 ? BlurredBox(shape, u_ui_shadow[0].zw, u_ui_shadow[1], sigma)
                               : BoxCoverage(shape, u_ui_shadow[0].zw, u_ui_shadow[1]);
    float cut = BoxCoverage(p - u_ui_shadow[2].xy, u_ui_shadow[2].zw, u_ui_shadow[3]);
    float alpha = u_ui_shadow[4].y > 0.5 ? (1.0 - inside) * cut : inside * (1.0 - cut);
    gl_FragColor = v_color0 * alpha;
}
