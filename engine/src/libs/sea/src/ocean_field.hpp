// The sea's wave field: the spectrum's cascades transformed to heights and horizontal displacements. README.md.
#pragma once

#include "ocean_fft.hpp"
#include "ocean_spectrum.hpp"
#include "storm_glm.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace storm::sea::ocean
{
// One sample of a cascade: the height (y) and horizontal displacement (x, z) in metres of the grid point
// (x, y) * patch / size of the still water.
struct Displacement
{
    float dx = 0.0f, h = 0.0f, dz = 0.0f, unused = 0.0f;
};

// A cascade's slopes at a sample, per metre, by central differences of its displacement: the height's along x and z
// and the horizontal displacement's own (its Jacobian's diagonal less one). What the pixel stage shades with.
struct Slope
{
    float hx = 0.0f, hz = 0.0f, dxx = 0.0f, dzz = 0.0f;
};

// The pixel stage's slope texture of a cascade (LEAN mapping, Olano and Baker 2010): per texel the displaced surface's
// slopes along x and z and their squares, as four half floats, followed by every level of 2 x 2 averages down to one
// texel. A level so holds the mean slope and the mean square slope of the samples it covers; their difference is the
// slope variance it hides, which the shading turns into roughness (Bruneton et al. 2010).
int SlopeLevels(int size);
size_t SlopeLevelOffset(int size, int level); // in texels, into PackSlopeMoments' output
void PackSlopeMoments(const std::vector<Slope> &slopes, int size, std::vector<uint64_t> &out);

// The surface at a point of the still water: its displacement and that displacement's derivatives per metre along
// x and z, of the bilinear interpolation between samples (what the vertex shader computes from the same samples).
struct SurfacePoint
{
    glm::vec3 offset{0.0f};
    glm::vec3 along_x{0.0f}, along_z{0.0f};

    // The displaced surface's unit normal.
    glm::vec3 Normal() const;
};

class Field
{
  public:
    explicit Field(std::vector<Cascade> layout);

    const std::vector<Cascade> &Layout() const
    {
        return layout_;
    }

    // The spectra (BuildSpectrum on the layout), the displacement factor and the still water's level.
    void SetSpectra(std::vector<CascadeSpectrum> spectra, float choppiness, float mean_level);
    void SetState(const SeaState &state);
    // A weather change: from the spectra in use towards `target`, `Ease(weight)` between them (0 the old, 1 the new;
    // the draws are the same, only the spectrum's square root changes, so no wave jumps). At 1 the target stays.
    void EaseTo(std::vector<CascadeSpectrum> target, float choppiness, float mean_level);
    void Ease(float weight);
    bool Easing() const
    {
        return !target_.empty();
    }

    // Cascade `cascade` at `time` seconds of the sea's clock. Cascades are independent: one thread each, with its
    // own transform of the cascade's size.
    void UpdateCascade(size_t cascade, double time, InverseFft2d &transform);
    void Update(double time);

    const std::vector<Displacement> &Samples(size_t cascade) const
    {
        return samples_[cascade];
    }
    // The samples as the vertex shader's texture holds them: four half floats a sample (dx, h, dz, 0), exactly the
    // values Samples() holds (UpdateCascade rounds them so).
    const std::vector<uint64_t> &SampleHalves(size_t cascade) const
    {
        return sample_halves_[cascade];
    }
    const std::vector<Slope> &Slopes(size_t cascade) const
    {
        return slopes_[cascade];
    }
    float MeanLevel() const
    {
        return Mixed(mean_level_, target_mean_level_);
    }
    // Bounds of the surface's height (metres, the still level included) after the last update: the sums of each
    // cascade's lowest and highest sample.
    float Lowest() const;
    float Highest() const;

    // The first `cascades` cascades' displacement of the still water's point (x, z), bilinear between samples.
    glm::vec3 Offset(float x, float z, size_t cascades) const;
    SurfacePoint Point(float x, float z, size_t cascades) const;
    // The height of the surface over (x, z) and its unit normal: the displaced point that lands there.
    float Height(float x, float z, size_t cascades, glm::vec3 *normal = nullptr) const;

  private:
    void Sample(size_t cascade, float x, float z, SurfacePoint &sum) const;
    glm::vec2 StillPoint(float x, float z, size_t cascades) const;
    float Mixed(float from, float to) const
    {
        return target_.empty() ? from : from + (to - from) * weight_;
    }

    std::vector<Cascade> layout_;
    std::vector<CascadeSpectrum> spectra_, target_;
    float choppiness_ = 0.0f, mean_level_ = 0.0f;
    float target_choppiness_ = 0.0f, target_mean_level_ = 0.0f, weight_ = 0.0f;
    std::vector<float> cascade_low_, cascade_high_; // per cascade, of the last update
    std::vector<std::vector<Displacement>> samples_;
    std::vector<std::vector<uint64_t>> sample_halves_;
    std::vector<std::vector<Slope>> slopes_;
    // Per cascade: h + i dx and dz in frequency, transformed in place.
    std::vector<std::array<FftBuffer, 2>> work_;
    std::vector<std::unique_ptr<InverseFft2d>> transforms_; // Update's, one per size
};
} // namespace storm::sea::ocean
