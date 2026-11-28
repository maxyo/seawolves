#pragma once

#include "cannon_trace.h"
#include "storm/render/effects.hpp"

// The wave field SEA_BASE::WaveXZ reads this frame (its heights; no wakes), for the GPU's other readers (the coast
// foam's strands): its texture and what sea_surface.sh needs to read it (sea_surface_gpu.hpp).
struct SeaHeightField
{
    int32_t texture = -1; // renderer texture id: the cascades' samples, RGBA32F, cascade c in rows c * size..
    int32_t size = 0;     // samples a side of each cascade
    int32_t cascades = 0;
    float patch[3]{};                    // metres each cascade repeats over
    float mean_level = 0.0f;             // the still water's height
    float lowest = 0.0f, highest = 0.0f; // bounds of the surface's height this frame
    glm::vec3 camera{};                  // beyond `reach` metres of it (in x, z) the sea is flat at mean_level
    float reach = 0.0f;
};

class SEA_BASE : public CANNON_TRACE_BASE
{
  public:
    virtual float WaveXZ(float x, float z, glm::vec3 *vNormal = nullptr) = 0;
    // What WaveXZ computes its heights from this frame (the coast foam samples it on the GPU: CoastFoam); false
    // when there is none. An implementation whose WaveXZ is more than this field must change both.
    virtual bool HeightField(SeaHeightField &field) const = 0;
    // A hull on the water this frame: the sea hands this frame's hulls to the renderer's wakes when it draws
    // (storm::render::SetEffectsSeaFrame). Execute stage.
    virtual void AddWaveSource(const storm::render::EffectsWaveSource &source) = 0;
};
