// The lamps of a location, per pixel and with their shadows (src/effects/lamp_shadows.hpp; fx/lamp_shadows.hpp for
// the face convention). All of it in absolute world space.
// u_fxLampView: view space -> world.
// u_fxLamps[0]: x lamp slots (0 = no lamps for this draw), y 1 = GL texture origin (bottom-left), z the lamps' gain
//               on baked colours, w the part of the lamp light the baked colours already hold
// u_fxLamps[1]: x 1 / faces per atlas row, y 1 / atlas rows, z static face size in texels, w dynamic face size
// per slot i at 2 + 3i: xyz position, w radius | rgb colour (with the slot's fade), w 2 = a light that moves (a
//               carried lantern: no bake holds its light, it only adds), else 0 | intensity, core², unused, unused
// u_fxLampGrid[0]: xyz the lamp grid's corner, w 1 = this draw has the lamps' edge maps at the sun map's unit
//                  (renderer_bgfx/README.md, "Lamps"), else 0
// u_fxLampGrid[1]: xyz 1 / the grid's extent, w cells along x
// u_fxLampGrid[2]: xyz the room light's corner, w > 0 = this draw takes its ambient from the room light, times w
// u_fxLampGrid[3]: xyz 1 / the room light's extent, w 1 = 5x5 filter on the static faces of the strongest lamp (else 3x3)
// u_fxLampGrid[4]: x the first slot of the lights that move, y how many of their slots to look up: up to the last one
//                  that holds a light (they are not in the grid),
//                  z the share of a lamp's own colour in what it adds to baked surfaces (the rest: the bake's tone),
//                  w the light under which a lamp is not filtered against its shadow (Settings::lamp_shadow_light
//                  over the strongest gain, gamma space)
// u_fxLampGrid[5..7], [8..10]: per such slot, the rows of the turn from the world into the space of its faces
// u_fxLampGrid[11]: x the most grid lamps per pixel (Settings::lamp_per_pixel), y the faces' near plane
//                   (fx::kLampShadowNear), z the most lamps per pixel whose shadows are filtered (Settings::lamp_filtered;
//                   the others take one tap), w the light (gamma space) under which the strongest lamp takes the 3x3
//                   filter instead of the 5x5 (Settings::lamp_soft_light over the strongest gain)
// u_fxLampFaces[i] (a frame uniform, the value at the end of the frame for every draw of it), per slot:
//                   x the faces (bit f = face f) of its dynamic atlas that hold a caster this frame; the others are
//                   not read; y 1 = its edge maps are drawn, else 0.
uniform vec4 u_fxLamps[50];
uniform vec4 u_fxLampGrid[12];
uniform vec4 u_fxLampFaces[16];
SAMPLER3D(s_fxRoomLight, 8);
SAMPLER3D(s_fxLampGrid, 9);
SAMPLER2DSHADOW(s_fxLampDynamic, 10);
SAMPLER2DSHADOW(s_fxLampStatic, 11);
// The edge maps take the sun map's unit in the draws the sun's shadows do not reach.
#ifndef FX_SUN_MAP_SH
SAMPLER2DARRAYSHADOW(s_fxShadowMap, 15);
#endif
#define FF_LAMP_STILL(x, y, depth) shadow2D(s_fxLampStatic, vec3(x, y, depth))
#define FF_LAMP_MOVING(x, y, depth) shadow2D(s_fxLampDynamic, vec3(x, y, depth))


// 3x3 percentage-closer filter in four hardware-filtered compare taps: the optimised PCF of The Witness, after
// MJP's Shadows sample (github.com/TheRealMJP/Shadows, MIT). TAP(x, y, depth): one tap at atlas coordinates;
// texel: the point in atlas texels, corner: the texel corner nearest to it, size: the atlas in texels; least: the
// smallest tap (1 = every texel the filter reads passes).
// (The macros' own names carry a prefix: their arguments are expressions of the caller's variables.)
#define FF_LAMP_PCF3(result, least, TAP, texel, corner, size, depth) \
    { \
        vec2 pcf_st = (texel) + vec2_splat(0.5) - (corner); \
        vec2 pcf_base = ((corner) - vec2_splat(0.5)) / (size); \
        vec2 pcf_w0 = vec2_splat(3.0) - 2.0 * pcf_st; \
        vec2 pcf_w1 = vec2_splat(1.0) + 2.0 * pcf_st; \
        vec2 pcf_o0 = ((vec2_splat(2.0) - pcf_st) / pcf_w0 - vec2_splat(1.0)) / (size); \
        vec2 pcf_o1 = (pcf_st / pcf_w1 + vec2_splat(1.0)) / (size); \
        float pcf_a = TAP(pcf_base.x + pcf_o0.x, pcf_base.y + pcf_o0.y, depth); \
        float pcf_b = TAP(pcf_base.x + pcf_o1.x, pcf_base.y + pcf_o0.y, depth); \
        float pcf_c = TAP(pcf_base.x + pcf_o0.x, pcf_base.y + pcf_o1.y, depth); \
        float pcf_d = TAP(pcf_base.x + pcf_o1.x, pcf_base.y + pcf_o1.y, depth); \
        result = (pcf_w0.x * pcf_w0.y * pcf_a + pcf_w1.x * pcf_w0.y * pcf_b + pcf_w0.x * pcf_w1.y * pcf_c + \
                  pcf_w1.x * pcf_w1.y * pcf_d) / 16.0; \
        least = min(min(pcf_a, pcf_b), min(pcf_c, pcf_d)); \
    }

// The same filter over 5x5 texels in nine taps.
#define FF_LAMP_PCF5(result, least, TAP, texel, corner, size, depth) \
    { \
        vec2 pcf_st = (texel) + vec2_splat(0.5) - (corner); \
        vec2 pcf_base = ((corner) - vec2_splat(0.5)) / (size); \
        vec2 pcf_w0 = vec2_splat(4.0) - 3.0 * pcf_st; \
        vec2 pcf_w2 = vec2_splat(1.0) + 3.0 * pcf_st; \
        vec2 pcf_o0 = ((vec2_splat(3.0) - 2.0 * pcf_st) / pcf_w0 - vec2_splat(2.0)) / (size); \
        vec2 pcf_o1 = ((vec2_splat(3.0) + pcf_st) / 7.0) / (size); \
        vec2 pcf_o2 = (pcf_st / pcf_w2 + vec2_splat(2.0)) / (size); \
        float pcf_a = TAP(pcf_base.x + pcf_o0.x, pcf_base.y + pcf_o0.y, depth); \
        float pcf_b = TAP(pcf_base.x + pcf_o1.x, pcf_base.y + pcf_o0.y, depth); \
        float pcf_c = TAP(pcf_base.x + pcf_o2.x, pcf_base.y + pcf_o0.y, depth); \
        float pcf_d = TAP(pcf_base.x + pcf_o0.x, pcf_base.y + pcf_o1.y, depth); \
        float pcf_e = TAP(pcf_base.x + pcf_o1.x, pcf_base.y + pcf_o1.y, depth); \
        float pcf_f = TAP(pcf_base.x + pcf_o2.x, pcf_base.y + pcf_o1.y, depth); \
        float pcf_g = TAP(pcf_base.x + pcf_o0.x, pcf_base.y + pcf_o2.y, depth); \
        float pcf_h = TAP(pcf_base.x + pcf_o1.x, pcf_base.y + pcf_o2.y, depth); \
        float pcf_i = TAP(pcf_base.x + pcf_o2.x, pcf_base.y + pcf_o2.y, depth); \
        result = (pcf_w0.x * pcf_w0.y * pcf_a + 7.0 * pcf_w0.y * pcf_b + pcf_w2.x * pcf_w0.y * pcf_c + \
                  pcf_w0.x * 7.0 * pcf_d + 49.0 * pcf_e + pcf_w2.x * 7.0 * pcf_f + pcf_w0.x * pcf_w2.y * pcf_g + \
                  7.0 * pcf_w2.y * pcf_h + pcf_w2.x * pcf_w2.y * pcf_i) / 144.0; \
        least = min(min(min(pcf_a, pcf_b), min(pcf_c, pcf_d)), min(min(pcf_e, pcf_f), min(min(pcf_g, pcf_h), pcf_i))); \
    }

// The depth a face stores for a point `major` metres along its axis (fx::LampFaceDepth).
float ffLampFaceDepth(float major, float radius)
{
    float near = u_fxLampGrid[11].y;
    return clamp((1.0 - near / max(major, 1e-4)) * radius / (radius - near), 0.0, 1.0);
}

// 1 = the lamp of slot `slot` reaches the point at L from it (unit geometric normal Ng, facing the lamp by ngl),
// 0 = the location or a character stands between them. radius: the lamp's; filtering: 2 = the 5x5 filter on its still
// faces (the strongest lamp of the pixel with Settings::lamp_soft), 1 = 3x3 (the lamps of the pixel's filtered
// budget, Settings::lamp_filtered), 0 = one hardware-filtered tap (the pixel's other lamps); the dynamic faces take
// the 3x3 filter or the one tap alike. Its dynamic faces are read only where they hold a caster and the still faces
// let some light through. A still filter whose every tap passes is exactly 1 (its weights sum to 1 only up to
// rounding). With the slot's edge maps (README "Lamps") one tap between the pair of the point's cell tells that all
// pass (1), none (0) or some (0.5): only then the filter runs.
float ffLampVisibility(float slot, vec3 L, float radius, vec3 Ng, float ngl, float filtering)
{
    float soft = filtering > 1.5 ? 1.0 : 0.0;
    // One texel of a face at this distance, in metres: the receiver steps that far off its surface (further at grazing
    // light), and the compared point is pulled towards the lamp by most of a texel.
    float texel = 2.1 * length(L) / u_fxLamps[1].z;
    vec3 Lo = L + Ng * (texel * (1.0 + soft + 1.5 * (1.0 - clamp(ngl, 0.0, 1.0))) + 0.004);
    vec3 a = abs(Lo);
    vec3 F;
    vec3 R;
    vec3 U;
    float face;
    if (a.x >= a.y && a.x >= a.z)
    {
        if (Lo.x >= 0.0) { face = 0.0; F = vec3(1.0, 0.0, 0.0); R = vec3(0.0, 0.0, -1.0); U = vec3(0.0, 1.0, 0.0); }
        else { face = 1.0; F = vec3(-1.0, 0.0, 0.0); R = vec3(0.0, 0.0, 1.0); U = vec3(0.0, 1.0, 0.0); }
    }
    else if (a.y >= a.z)
    {
        if (Lo.y >= 0.0) { face = 2.0; F = vec3(0.0, 1.0, 0.0); R = vec3(1.0, 0.0, 0.0); U = vec3(0.0, 0.0, -1.0); }
        else { face = 3.0; F = vec3(0.0, -1.0, 0.0); R = vec3(1.0, 0.0, 0.0); U = vec3(0.0, 0.0, 1.0); }
    }
    else
    {
        if (Lo.z >= 0.0) { face = 4.0; F = vec3(0.0, 0.0, 1.0); R = vec3(1.0, 0.0, 0.0); U = vec3(0.0, 1.0, 0.0); }
        else { face = 5.0; F = vec3(0.0, 0.0, -1.0); R = vec3(-1.0, 0.0, 0.0); U = vec3(0.0, 1.0, 0.0); }
    }
    float major = max(dot(Lo, F), 1e-4);
    float s = 1.0 / 1.05; // fx::kLampFaceGuard
    vec2 uv = vec2((face + 0.5 + 0.5 * s * dot(Lo, R) / major) * u_fxLamps[1].x,
                   (slot + 0.5 - 0.5 * s * dot(Lo, U) / major) * u_fxLamps[1].y);
    if (u_fxLamps[0].y > 0.5)
        uv.y = 1.0 - uv.y;
    // Along the ray to the lamp the stored depth only grows: the pull in metres becomes a shorter axis distance.
    float len = max(length(Lo), 1e-4);
    float depth = ffLampFaceDepth(major * (len - 0.75 * texel) / len, radius);
    vec2 faces = vec2(1.0 / u_fxLamps[1].x, 1.0 / u_fxLamps[1].y);
    vec2 stillSize = faces * u_fxLamps[1].z;
    vec2 stillTexel = uv * stillSize;
    vec2 stillCorner = floor(stillTexel + vec2_splat(0.5));
    float edges = 0.5;
    if (filtering > 0.5 && u_fxLampGrid[0].w > 0.5 && u_fxLampFaces[int(slot + 0.5)].y > 0.5)
    {
        // The pair of the corner's cell, in the layer of this filter (with the 5x5, the 3x3 has the second).
        vec2 cell = floor(stillCorner * 0.5);
        float lower = face > 2.5 ? 1.0 : 0.0;
        float layer = filtering > 1.5 ? 0.0 : u_fxLampGrid[3].w;
        vec2 pair = vec2(2.0 * cell.x + 1.0 - lower * 3.0 * u_fxLamps[1].z,
                         cell.y + (floor(stillCorner.y / u_fxLamps[1].z) + lower) * 0.5 * u_fxLamps[1].z + 0.5);
        edges = shadow2DArray(s_fxShadowMap, vec4(pair / vec2(0.5 * stillSize.x, stillSize.y), layer, depth));
    }
    float lit;
    float least = 0.0;
    if (edges < 0.25)
    {
        lit = 0.0;
    }
    else if (edges > 0.75)
    {
        lit = 1.0;
    }
    else if (filtering > 1.5)
    {
        FF_LAMP_PCF5(lit, least, FF_LAMP_STILL, stillTexel, stillCorner, stillSize, depth)
    }
    else if (filtering > 0.5)
    {
        FF_LAMP_PCF3(lit, least, FF_LAMP_STILL, stillTexel, stillCorner, stillSize, depth)
    }
    else
    {
        lit = shadow2D(s_fxLampStatic, vec3(uv, depth));
    }
    if (least >= 1.0)
        lit = 1.0;
    // The result is the smaller of the two: where the still faces pass no light, the characters' cannot change it.
    if (lit > 0.0 && mod(floor(u_fxLampFaces[int(slot + 0.5)].x / exp2(face) + 0.001), 2.0) > 0.5)
    {
        // The characters' faces are coarser by the ratio of the face sizes.
        float pull = 0.75 * texel + texel * (u_fxLamps[1].z / u_fxLamps[1].w - 1.0);
        float movingDepth = ffLampFaceDepth(major * (len - pull) / len, radius);
        float moving;
        if (filtering > 0.5)
        {
            vec2 movingSize = faces * u_fxLamps[1].w;
            vec2 movingTexel = uv * movingSize;
            vec2 movingCorner = floor(movingTexel + vec2_splat(0.5));
            float movingLeast;
            FF_LAMP_PCF3(moving, movingLeast, FF_LAMP_MOVING, movingTexel, movingCorner, movingSize, movingDepth)
        }
        else
        {
            moving = shadow2D(s_fxLampDynamic, vec3(uv, movingDepth));
        }
        lit = min(lit, moving);
    }
    return lit;
}

// The light of the lamp in slot `slot` at Pw without its shadow and its surface's turn: its strongest channel times
// its falloff (which lamp of a pixel is the strongest).
float ffLampShown(float slot, vec3 Pw)
{
    int base = 2 + 3 * int(slot + 0.5);
    vec4 lamp = u_fxLamps[base];
    float reach = length(Pw - lamp.xyz);
    if (reach >= lamp.w)
        return 0.0;
    vec3 c = u_fxLamps[base + 1].rgb;
    vec4 shape = u_fxLamps[base + 2];
    return max(c.r, max(c.g, c.b)) * fxLightFalloff(reach, lamp.w, shape.x, shape.y);
}

// Adds the lamp with grid number `id` (slot + 1, 0 = none) at Pw: sum.rgb its light with its shadow, sum.a the
// luminance of its light without the shadow. Ng: geometric normal (shadow bias, no light from behind the
// surface), N: shading normal. fxLightFalloff: the lamp's shape, zero at its radius. strongest: the slot of the
// pixel's strongest lamp (the 5x5 filter with Settings::lamp_soft, else 3x3), -1 = none. filtered: how many more of
// the pixel's lamps take the 3x3 filter (Settings::lamp_filtered less the strongest; the first lamps that look their
// shadow up, in the loop's order: the lights that move, then the grid's strongest first); the others take one tap.
// specV, spec: the world-space unit vector to the eye and the pixel's highlight (fxGlossSpec / fxMaterialSpec); the
// lamp's highlight with its shadow is added to lampSpec.
// shown: the lamp's light at the pixel (gamma space): the 5x5 filter only where it is bright enough for its wider
// penumbra to show (u_fxLampGrid[11].w, Settings::lamp_soft_light), the 3x3 below.
float ffLampFilter(float slot, float strongest, float shown, inout float filtered)
{
    if (abs(slot - strongest) < 0.5)
        return u_fxLampGrid[3].w > 0.5 && shown >= u_fxLampGrid[11].w ? 2.0 : 1.0;
    if (filtered < 0.5)
        return 0.0;
    filtered -= 1.0;
    return 1.0;
}
void ffLampAdd(float id, vec3 Pw, vec3 Ng, vec3 N, vec3 specV, vec2 spec, float strongest, inout float filtered,
               inout vec4 sum, inout vec3 lampSpec)
{
    if (id < 0.5)
        return;
    float slot = id - 1.0;
    int base = 2 + 3 * int(slot);
    vec4 lamp = u_fxLamps[base];
    vec3 L = Pw - lamp.xyz;
    float reach = length(L);
    if (reach >= lamp.w)
        return;
    vec4 shape = u_fxLamps[base + 2];
    bool merged = shape.z > 0.5;
    vec3 toLamp = -L / max(reach, 1e-4);
    float ngl = dot(Ng, toLamp);
    // Its shadow is looked up once below for either kind (the program holds ffLampVisibility once): with the filter
    // `filtering` (ffLampFilter), -1 = not looked up; in the direction Lf with the normal Nf.
    float filtering = -1.0;
    vec3 Lf = L;
    vec3 Nf = Ng;
    // A merged fixture: its members' light (fxAggregateAdd) under the one shadow of its centre.
    vec4 diffuse = vec4_splat(0.0);
    vec3 highlight = vec3_splat(0.0);
    // A single lamp.
    float ndl = 0.0;
    float falloff = 0.0;
    float light = 0.0;
    vec3 rgb = vec3_splat(0.0);
    if (merged)
    {
        fxAggregateAdd(shape.z,Pw,Ng,N,specV,spec,shape.w,1.0,diffuse,highlight);
        // Exactly one visibility, including grazing-center normals. At the center the lookup direction is undefined.
        if (reach > 1e-4)
        {
            filtering = ffLampFilter(slot, strongest, fxLinear() ? sqrt(diffuse.a) : diffuse.a, filtered);
            // No member lights the pixel (all behind its surface or out of reach): nothing to shadow. The filter
            // budget is spent all the same, so the pixel's other lamps keep their filters.
            if (all(equal(diffuse.rgb, vec3_splat(0.0))) && all(equal(highlight, vec3_splat(0.0))))
                filtering = -1.0;
        }
    }
    else
    {
        ndl = dot(N, toLamp);
        if (ngl <= 0.0 || ndl <= 0.0)
            return;
        vec4 color = u_fxLamps[base + 1];
        // Every light of the place, still or moving, with the one falloff of all lights (fx/light_falloff.hpp): its
        // radius is where it ends.
        falloff = fxLightFalloff(reach, lamp.w, shape.x, shape.y);
        light = ndl * falloff;
        rgb = fxLightColor(color.rgb);
        // Light the picture cannot show (under half a step, fx::kLampNegligible) is not filtered against its shadow.
        float shown = max(rgb.r, max(rgb.g, rgb.b)) * light;
        if (fxLinear())
            shown = sqrt(shown);
        if (shown >= u_fxLampGrid[4].w)
        {
            // The faces of a ship's lantern are in the ship's space: the direction and the normal turn into it.
            if (color.w > 1.5)
            {
                int turn = 5 + 3 * int(slot - u_fxLampGrid[4].x + 0.5);
                Lf = vec3(dot(L, u_fxLampGrid[turn].xyz), dot(L, u_fxLampGrid[turn + 1].xyz), dot(L, u_fxLampGrid[turn + 2].xyz));
                Nf = vec3(dot(Ng, u_fxLampGrid[turn].xyz), dot(Ng, u_fxLampGrid[turn + 1].xyz), dot(Ng, u_fxLampGrid[turn + 2].xyz));
            }
            filtering = ffLampFilter(slot, strongest, shown, filtered);
        }
    }
    float seen = 1.0;
    if (filtering > -0.5)
        seen = ffLampVisibility(slot, Lf, lamp.w, Nf, ngl, filtering);
    if (merged)
    {
        sum.rgb += diffuse.rgb * seen;
        sum.a += diffuse.a;
        lampSpec += highlight * seen;
        return;
    }
    sum.rgb += rgb * (light * seen);
    sum.a += dot(rgb, vec3(0.299, 0.587, 0.114)) * light;
#ifdef FX_LIGHT_SPECULAR
    if (spec.y > 0.0)
    {
        lampSpec += rgb * (fxSpecLobe(N, specV, toLamp, spec) * clamp(ndl * 4.0, 0.0, 1.0) * falloff * seen);
    }
#endif
}

// How much of the lamps' part `lamp` a baked colour takes over its lamp-free part `base` (both in the space they add
// in: energy in linear light), one scale for all channels so the lamps' hue stays. The scene is 8-bit and the
// composite gives it its highlights back (fxExpandLdr): a channel past 1 is clipped, so near a lamp the relit colour
// burnt out to a flat yellow-white without its texture. Up to half of the headroom h that `base` leaves in the lamps'
// direction their light is added as it is; above, a shoulder with the same slope where it starts, x -> h - h*h/(4x),
// reaches h only at infinite light: the pool stays bright, its texture and colour stay.
float ffLampRoll(vec3 base, vec3 lamp)
{
    float peak = max(lamp.r, max(lamp.g, lamp.b));
    if (peak <= 1e-4)
        return 1.0;
    vec3 room = max(vec3_splat(1.0) - base, vec3_splat(0.0)) * peak;
    vec3 along = max(lamp, vec3_splat(1e-3 * peak));
    float h = min(room.r / along.r, min(room.g / along.g, room.b / along.b));
    if (peak <= 0.5 * h)
        return 1.0;
    return h * (1.0 - h / (4.0 * peak)) / peak;
}

// The light of the lights that move (a carried lantern, a ship's lanterns) at the pixel, with their shadows. The
// bake holds nothing of them: it is plain added light, as the game's own point lights, not a part of the relit sum:
// `lampMoved` of ffLampLight.

// The lamps that reach Pw: the lights that move, and from the lamp grid up to u_fxLampGrid[11].x of the eight of its
// cell, strongest first; `lampSpec` their highlights (ffLampAdd).
vec4 ffLampLight(vec3 Pw, vec3 Ng, vec3 N, vec3 specV, vec2 spec, out vec3 lampMoved, out vec3 lampSpec)
{
    vec4 sum = vec4_splat(0.0);
    lampMoved = vec3_splat(0.0);
    lampSpec = vec3_splat(0.0);
    if (u_fxLamps[0].x < 0.5)
        return sum;
    float cap = u_fxLampGrid[11].x;
    vec4 near = vec4_splat(0.0);
    vec4 more = vec4_splat(0.0);
    vec3 cell = (Pw - u_fxLampGrid[0].xyz) * u_fxLampGrid[1].xyz;
    if (min(min(cell.x, cell.y), cell.z) >= 0.0 && max(max(cell.x, cell.y), cell.z) <= 1.0)
    {
        float half_texel = 0.5 / u_fxLampGrid[1].w;
        float u = 0.5 * clamp(cell.x, half_texel, 1.0 - half_texel);
        near = floor(texture3D(s_fxLampGrid, vec3(u, cell.y, cell.z)) * 255.0 + vec4_splat(0.5));
        if (near.w > 0.5 && cap > 4.5)
            more = floor(texture3D(s_fxLampGrid, vec3(u + 0.5, cell.y, cell.z)) * 255.0 + vec4_splat(0.5));
    }
    // The strongest lamp of the pixel takes the best filter (ffLampFilter): the first of its cell (the grid lists the
    // strongest first) or a light that moves, whichever is stronger here.
    float strongest = -1.0;
    {
        float best = 0.0;
        if (near.x > 0.5)
        {
            strongest = near.x - 1.0;
            best = ffLampShown(strongest, Pw);
        }
        for (int i = 0; i < 2; ++i)
        {
            float slot = u_fxLampGrid[4].x + float(i);
            if (u_fxLampGrid[4].y > float(i) + 0.5)
            {
                float s = ffLampShown(slot, Pw);
                if (s > best)
                {
                    best = s;
                    strongest = slot;
                }
            }
        }
    }
    float filtered = u_fxLampGrid[11].z - 1.0;
    // One loop over the pixel's lights, so the program holds ffLampAdd (its filters, its aggregate loop) once: ten
    // inlined copies made the lamp programs four times the size of the others, and their links as slow. First the
    // lights that move (at most two: few and anywhere, every pixel asks their slots up to the last that holds one,
    // without the grid), which leave the sum as `lampMoved`, then the lamps of the grid's cell, the first always, the
    // others up to the cap.
    for (int i = 0; i < 10; ++i)
    {
        float id = 0.0;
        if (i < 2)
        {
            if (u_fxLampGrid[4].y > float(i) + 0.5)
                id = u_fxLampGrid[4].x + float(i) + 1.0;
        }
        else
        {
            if (i == 2)
            {
                lampMoved = sum.rgb;
                sum = vec4_splat(0.0);
            }
            int k = i - 2;
            if (k > 0 && cap < float(k) + 0.5)
                break;
            vec4 cellIds = k < 4 ? near : more;
            int c = k < 4 ? k : k - 4;
            id = c == 0 ? cellIds.x : c == 1 ? cellIds.y : c == 2 ? cellIds.z : cellIds.w;
        }
        ffLampAdd(id, Pw, Ng, N, specV, spec, strongest, filtered, sum, lampSpec);
    }
    return sum;
}

// The light of the place around Pw (fx::RoomLight: its baked colours), in the baked colours' own units; Nw: the
// surface's normal (the light is taken half a metre in front of it).
vec3 ffRoomLight(vec3 Pw, vec3 Nw)
{
    vec3 uvw = (Pw + Nw * 0.5 - u_fxLampGrid[2].xyz) * u_fxLampGrid[3].xyz;
    return texture3D(s_fxRoomLight, clamp(uvw, 0.0, 1.0)).rgb;
}
