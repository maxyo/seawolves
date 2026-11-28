#include "ocean_field.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace storm::sea::ocean
{
namespace
{
// Newton steps on p + D(p) = x: the bilinear field is linear within a cell, so a few steps land on the point.
constexpr int kInversionSteps = 4;
// Below this Jacobian determinant the displaced grid folds (whitecaps): no unique point lands there, and a Newton
// step would jump; the plain step p = x - D(p) is taken instead.
constexpr float kFoldDeterminant = 0.1f;
} // namespace

Field::Field(std::vector<Cascade> layout) : layout_(std::move(layout))
{
    samples_.resize(layout_.size());
    work_.resize(layout_.size());
    for (size_t c = 0; c < layout_.size(); c++)
    {
        const size_t count = static_cast<size_t>(layout_[c].size) * layout_[c].size;
        samples_[c].assign(count, {});
        work_[c][0].assign(count, {});
        work_[c][1].assign(count, {});
    }
}

void Field::SetSpectra(std::vector<CascadeSpectrum> spectra, float choppiness, float mean_level)
{
    if (spectra.size() != layout_.size())
        throw std::invalid_argument("sea: spectra for another layout");
    spectra_ = std::move(spectra);
    choppiness_ = choppiness;
    mean_level_ = mean_level;
}

void Field::SetState(const SeaState &state)
{
    SetSpectra(BuildSpectrum(state, layout_), state.choppiness, state.mean_level);
}

void Field::UpdateCascade(size_t cascade, double time, InverseFft2d &transform)
{
    const Cascade &layout = layout_[cascade];
    const int n = layout.size;
    if (transform.Size() != n)
        throw std::invalid_argument("sea: a transform of another size");
    const CascadeSpectrum &spectrum = spectra_[cascade];
    auto &[hx, z] = work_[cascade];
    const std::complex<float> i1(0.0f, 1.0f);
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++)
        {
            const size_t i = static_cast<size_t>(y) * n + x;
            const size_t opposite = static_cast<size_t>((n - y) % n) * n + (n - x) % n;
            const int kx = x < n / 2 ? x : x - n, ky = y < n / 2 ? y : y - n;
            if (kx == 0 && ky == 0)
            {
                hx[i] = z[i] = {};
                continue;
            }
            // A wave along k as e^(i (k.x - w t)); its opposite sample holds the conjugate so the field is real.
            const double phase = std::fmod(static_cast<double>(spectrum.omega[i]) * time, 2.0 * std::numbers::pi);
            const std::complex<float> turn(static_cast<float>(std::cos(phase)), static_cast<float>(-std::sin(phase)));
            const std::complex<float> h = spectrum.amplitude[i] * turn + std::conj(spectrum.amplitude[opposite] * turn);
            // Horizontal displacement towards the crests (Tessendorf's choppy waves): i k / |k| h.
            const float length = std::hypot(static_cast<float>(kx), static_cast<float>(ky));
            const std::complex<float> d = i1 * choppiness_ * h / length;
            hx[i] = h + i1 * (d * static_cast<float>(kx));
            z[i] = d * static_cast<float>(ky);
        }
    transform.Run(hx.data());
    transform.Run(z.data());
    std::vector<Displacement> &out = samples_[cascade];
    for (size_t i = 0; i < out.size(); i++)
        out[i] = {hx[i].imag(), hx[i].real(), z[i].real(), 0.0f};
}

void Field::Update(double time)
{
    for (size_t c = 0; c < layout_.size(); c++)
    {
        InverseFft2d *transform = nullptr;
        for (const auto &t : transforms_)
            if (t->Size() == layout_[c].size)
                transform = t.get();
        if (!transform)
            transform = transforms_.emplace_back(std::make_unique<InverseFft2d>(layout_[c].size)).get();
        UpdateCascade(c, time, *transform);
    }
}

glm::vec3 Field::Sample(size_t cascade, float x, float z) const
{
    const Cascade &layout = layout_[cascade];
    const int n = layout.size;
    // Doubles: a world position of kilometres in samples of a short cascade keeps its fraction.
    const double u = static_cast<double>(x) / layout.patch * n, v = static_cast<double>(z) / layout.patch * n;
    const double fu = std::floor(u), fv = std::floor(v);
    const auto ix = static_cast<int64_t>(fu), iz = static_cast<int64_t>(fv);
    const float tx = static_cast<float>(u - fu), tz = static_cast<float>(v - fv);
    const auto at = [&](int64_t sx, int64_t sz) {
        const Displacement &d = samples_[cascade][static_cast<size_t>((sz & (n - 1)) * n + (sx & (n - 1)))];
        return glm::vec3(d.dx, d.h, d.dz);
    };
    const glm::vec3 a = glm::mix(at(ix, iz), at(ix + 1, iz), tx);
    const glm::vec3 b = glm::mix(at(ix, iz + 1), at(ix + 1, iz + 1), tx);
    return glm::mix(a, b, tz);
}

glm::vec3 Field::Offset(float x, float z, size_t cascades) const
{
    glm::vec3 sum(0.0f);
    for (size_t c = 0; c < cascades; c++)
        sum += Sample(c, x, z);
    return sum;
}

glm::vec2 Field::StillPoint(float x, float z, size_t cascades) const
{
    const float step = SmallestSpacing(cascades) * 0.25f;
    glm::vec2 p(x, z);
    for (int i = 0; i < kInversionSteps; i++)
    {
        const glm::vec3 o = Offset(p.x, p.y, cascades);
        const glm::vec2 miss = p + glm::vec2(o.x, o.z) - glm::vec2(x, z);
        const glm::vec3 ox = (Offset(p.x + step, p.y, cascades) - o) / step;
        const glm::vec3 oz = (Offset(p.x, p.y + step, cascades) - o) / step;
        // d(p + D)/dp, columns along x and z.
        const glm::mat2 jacobian(1.0f + ox.x, ox.z, oz.x, 1.0f + oz.z);
        const float determinant = glm::determinant(jacobian);
        p -= determinant > kFoldDeterminant ? glm::inverse(jacobian) * miss : miss;
    }
    return p;
}

float Field::SmallestSpacing(size_t cascades) const
{
    float spacing = layout_[0].patch / layout_[0].size;
    for (size_t c = 1; c < cascades; c++)
        spacing = std::min(spacing, layout_[c].patch / layout_[c].size);
    return spacing;
}

float Field::Height(float x, float z, size_t cascades, glm::vec3 *normal) const
{
    const glm::vec2 p = StillPoint(x, z, cascades);
    if (normal)
    {
        const float step = SmallestSpacing(cascades);
        const auto point = [&](float px, float pz) {
            const glm::vec3 offset = Offset(px, pz, cascades);
            return glm::vec3(px + offset.x, offset.y, pz + offset.z);
        };
        const glm::vec3 along_x = point(p.x + step, p.y) - point(p.x - step, p.y);
        const glm::vec3 along_z = point(p.x, p.y + step) - point(p.x, p.y - step);
        *normal = glm::normalize(glm::cross(along_z, along_x));
    }
    return Offset(p.x, p.y, cascades).y + mean_level_;
}
} // namespace storm::sea::ocean
