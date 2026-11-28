// The sea's surface in its vertex port (vs_port_sea2), the coast foam's (vs_port_coastfoam) and the sea's self-check
// (vs_sea_check.sc): the wave field's displacement of a point of the still water and the displaced surface's normal,
// from the samples the CPU made (src/libs/sea/src/ocean_field.cpp, Field::Point: the same bilinear sample and
// derivatives) and the registers of src/libs/sea/include/sea_surface_gpu.hpp.
// c8.w = 0: the CPU displaced the sea's vertices itself (until the self-check passed); they are used as given.
#ifndef SEA_SURFACE_SH
#define SEA_SURFACE_SH

// The field: the cascades' samples stacked in one RGBA16F texture (dx, h, dz per texel), read texel by texel at the
// vertex texture unit, as the skinned draws read their bone palettes (ff_skin.sh). D3DVERTEXTEXTURESAMPLER0.
SAMPLER2D(s_vtex, 13);

// The cascade of register `cascade` (sea_gpu::cascade_register) at the still point `local`: adds its displacement and
// that displacement's derivatives per metre along x and z.
void seaCascade(vec2 local, vec4 cascade, inout vec3 offset, inout vec3 alongX, inout vec3 alongZ)
{
    if (cascade.w < 0.0)
        return;
    float size = float(textureSize(s_vtex, 0).x);
    vec2 t = local * cascade.x + cascade.yz;
    vec2 i = floor(t);
    vec2 f = t - i;
    vec2 w0 = mod(i, size);
    vec2 w1 = mod(i + 1.0, size);
    ivec2 i0 = ivec2(w0.x, w0.y + cascade.w);
    ivec2 i1 = ivec2(w1.x, w1.y + cascade.w);
    vec3 a1 = texelFetch(s_vtex, i0, 0).xyz;
    vec3 a2 = texelFetch(s_vtex, ivec2(i1.x, i0.y), 0).xyz;
    vec3 a3 = texelFetch(s_vtex, ivec2(i0.x, i1.y), 0).xyz;
    vec3 a4 = texelFetch(s_vtex, i1, 0).xyz;
    offset += mix(mix(a1, a2, f.x), mix(a3, a4, f.x), f.y);
    alongX += mix(a2 - a1, a4 - a3, f.y) * cascade.x;
    alongZ += mix(a3 - a1, a4 - a2, f.x) * cascade.x;
}

// The displacement of the still point `local` by the three cascades at registers first .. first + 2, and the
// displaced surface's unit normal (storm::sea::ocean::SurfacePoint::Normal).
vec3 seaField(vec2 local, int first, out vec3 normal)
{
    vec3 offset = vec3_splat(0.0);
    vec3 alongX = vec3_splat(0.0);
    vec3 alongZ = vec3_splat(0.0);
    seaCascade(local, u_vsc[first], offset, alongX, alongZ);
    seaCascade(local, u_vsc[first + 1], offset, alongX, alongZ);
    seaCascade(local, u_vsc[first + 2], offset, alongX, alongZ);
    normal = normalize(cross(vec3(alongZ.x, alongZ.y, 1.0 + alongZ.z), vec3(1.0 + alongX.x, alongX.y, alongX.z)));
    return offset;
}

// The sea's surface at the still point `local` (metres from the mesh's origin, x and z): `pos` as the sea's vertex
// position (world minus the sea's vertex offset) and its `normal`.
void seaSurface(vec2 local, out vec4 pos, out vec3 normal)
{
    vec2 toCamera = local - u_vsc[8].xy;
    vec3 offset = vec3_splat(0.0);
    normal = vec3(0.0, 1.0, 0.0);
    if (dot(toCamera, toCamera) <= u_vsc[8].z)
        offset = seaField(local, 4, normal);
    vec2 xz = local + offset.xz + u_vsc[7].xy;
    pos = vec4(xz.x, u_vsc[7].z + offset.y, xz.y, 1.0);
}

// The port's vertex registers v0 (position) and v1 (normal) from its stage inputs (which only main() can name,
// storm_common.sh): displaced here from the flat grid, or as the CPU wrote them (c8.w = 0).
void seaVertex(vec4 position, vec3 inNormal, out vec4 v0, out vec4 v1)
{
    if (u_vsc[8].w != 0.0)
    {
        vec3 normal;
        seaSurface(position.xz, v0, normal);
        v1 = vec4(normal, 1.0);
    }
    else
    {
        v0 = position;
        v1 = vec4(inNormal, 1.0);
    }
}

#endif
