// The wind's bend of plants and tents (renderer_bgfx README, "Wind"). Included by ff_vertex.sh in the lit programs.
// Caribbean Legend's technique with its numbers (GPU Gems 3, ch. 16): a plant's vertex colour R or G marks a palm's
// or a tree's moving part and B a shrub's, the second texture coordinates' v how far up its leaf the vertex is; a
// tent's R marks the cloth that sags and waves, B its hanging edge. A game without marks bends a plant by its height
// above the object's lowest point. The wind's direction, strength and gusts are the weather's (environment::State).
// u_fxSway[0]: xyz the wind's direction in view space, w the kind: 0 none, 1 plant, 2 tent, 3 by height
// u_fxSway[1]: x seconds (wrapped at effects::kSwaySeconds), y the wind's strength 0..1, z the gusts' gain (1 none)
// u_fxSway[2]: x by height: the object's lowest y, y 1 / its extent, z how far its top sways at full wind (metres)
uniform vec4 u_fxSway[3];

// A palm's or a tree's sway along the wind, in units of its amplitude: a slow swing, faster and leaning in a strong
// wind, larger in a gust. phase: where the plant stands (neighbours swing apart).
float ffSwing(float t, float phase, float strength, float gust)
{
    float strong = smoothstep(0.55, 1.0, strength);
    float wave = 0.5 + 0.5 * sin(t * 0.18 + phase * 2.37);
    float gusting = pow(wave, 8.0) * mix(0.65, 1.0, 0.5 + 0.5 * sin(t * 0.07 + phase * 0.61));
    float swing = mix(sin(t + phase), sin(t * 1.5 + phase), strong);
    return strong * 3.04 + swing * mix(1.0, 0.45, strong) * (1.0 + 0.65 * gusting * gust) * mix(1.0, 3.0, strong);
}

// The vertex's shift: model space along its normal (returned in `normalShift`), view space along the wind (the
// result). P, N: the vertex's model position and normal; color: its colour; uv1: its second texture coordinates;
// scale: the model's scale.
vec3 ffSway(vec3 P, vec3 N, vec4 color, vec2 uv1, float scale, out float normalShift)
{
    normalShift = 0.0;
    float kind = u_fxSway[0].w;
    if (kind < 0.5)
        return vec3_splat(0.0);
    float t = u_fxSway[1].x;
    float strength = u_fxSway[1].y;
    float gust = u_fxSway[1].z;
    float phase = dot(P.xz, vec2(0.11, 0.07));
    float along = 0.0;
    if (kind < 1.5)
    {
        float leaf = clamp(abs(uv1.y), 0.0, 1.0);
        float bend = leaf * leaf;
        float palm = clamp(max(color.r, color.g), 0.0, 1.0);
        float shrub = clamp(color.b, 0.0, 1.0);
        float amplitude = 0.25 * mix(0.5, 1.0, strength) * gust * scale;
        along = ffSwing(t, phase, strength, gust) * amplitude * bend * palm;
        // The leaves flutter along their normals, more in a strong wind.
        normalShift = sin(t * 1.5 + leaf * 12.566 + uv1.x * 3.1416 + phase * 1.37) * 0.05 * amplitude *
                      mix(1.0, 2.0, smoothstep(0.55, 1.0, strength)) * smoothstep(0.0, 1.0, leaf) * bend * palm;
        // A shrub shivers.
        along += (sin(t * 1.12 + phase) * 0.045 * gust + (gust - 1.0) * 0.16) * mix(0.5, 1.5, strength) * bend * shrub;
    }
    else if (kind < 2.5)
    {
        float cloth = smoothstep(0.25, 0.75, color.r);
        float edge = clamp(color.b, 0.0, 1.0);
        float wind = strength * gust;
        normalShift = (sin(t * 2.0 + uv1.x * 6.0) * 0.03 * mix(1.0, 2.5, edge) + sin(t * 6.0 + uv1.x * 20.0) * 0.01 * edge) *
                      wind * cloth;
    }
    else
    {
        float w = clamp((P.y - u_fxSway[2].x) * u_fxSway[2].y, 0.0, 1.0);
        along = (ffSwing(t, phase, strength, gust) * mix(0.5, 1.0, strength) * gust + 0.1 * sin(t * 3.0 + phase * 5.0)) *
                u_fxSway[2].z * w * w;
    }
    return u_fxSway[0].xyz * along;
}
