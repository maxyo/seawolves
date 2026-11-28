// The sea's wave field: the spectrum's cascades transformed to heights and horizontal displacements. README.md.
#pragma once

#include "ocean_fft.hpp"
#include "ocean_spectrum.hpp"
#include "storm_glm.h"

#include <array>
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

    // Cascade `cascade` at `time` seconds of the sea's clock. Cascades are independent: one thread each, with its
    // own transform of the cascade's size.
    void UpdateCascade(size_t cascade, double time, InverseFft2d &transform);
    void Update(double time);

    const std::vector<Displacement> &Samples(size_t cascade) const
    {
        return samples_[cascade];
    }

    // The first `cascades` cascades' displacement of the still water's point (x, z), bilinear between samples.
    glm::vec3 Offset(float x, float z, size_t cascades) const;
    // The height of the surface over (x, z) and its unit normal: the displaced point that lands there.
    float Height(float x, float z, size_t cascades, glm::vec3 *normal = nullptr) const;

  private:
    glm::vec3 Sample(size_t cascade, float x, float z) const;
    glm::vec2 StillPoint(float x, float z, size_t cascades) const;
    float SmallestSpacing(size_t cascades) const;

    std::vector<Cascade> layout_;
    std::vector<CascadeSpectrum> spectra_;
    float choppiness_ = 0.0f, mean_level_ = 0.0f;
    std::vector<std::vector<Displacement>> samples_;
    // Per cascade: h + i dx and dz in frequency, transformed in place.
    std::vector<std::array<std::vector<std::complex<float>>, 2>> work_;
    std::vector<std::unique_ptr<InverseFft2d>> transforms_; // Update's, one per size
};
} // namespace storm::sea::ocean
