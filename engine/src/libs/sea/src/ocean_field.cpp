#include "ocean_field.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

#include "half_float.hpp"

namespace storm::sea::ocean
{
namespace
{
// Newton steps on p + D(p) = x: the bilinear field is linear within a cell, so a few steps land on the point.
constexpr int kInversionSteps = 4;
// Below this Jacobian determinant the displaced grid folds (whitecaps): no unique point lands there, and a Newton
// step would jump; the plain step p = x - D(p) is taken instead.
constexpr float kFoldDeterminant = 0.1f;
// A crest the choppy displacement squeezes (1 + dxx towards 0) steepens by 1 / (1 + dxx); past a fold it would turn
// over. The shading keeps at most this steepening and leaves the fold itself to the whitecaps.
constexpr float kMinStretch = 0.25f;

} // namespace

int SlopeLevels(int size)
{
    int levels = 1;
    while ((size >> (levels - 1)) > 1)
        levels++;
    return levels;
}

size_t SlopeLevelOffset(int size, int level)
{
    size_t offset = 0;
    for (int l = 0; l < level; l++)
        offset += static_cast<size_t>(size >> l) * static_cast<size_t>(size >> l);
    return offset;
}

void PackSlopeMoments(const std::vector<Slope> &slopes, int size, std::vector<uint64_t> &out)
{
    const int levels = SlopeLevels(size);
    std::vector<glm::vec4> level(slopes.size()), next;
    for (size_t i = 0; i < slopes.size(); i++)
    {
        const Slope &s = slopes[i];
        const float x = s.hx / std::max(1.0f + s.dxx, kMinStretch);
        const float z = s.hz / std::max(1.0f + s.dzz, kMinStretch);
        level[i] = {x, z, x * x, z * z};
    }
    out.resize(SlopeLevelOffset(size, levels));
    for (int l = 0; l < levels; l++)
    {
        const int n = size >> l;
        uint64_t *packed = out.data() + SlopeLevelOffset(size, l);
        for (size_t i = 0; i < level.size(); i++)
            packed[i] = storm::PackHalves(level[i].x, level[i].y, level[i].z, level[i].w);
        if (n == 1)
            break;
        const int half = n / 2;
        next.assign(static_cast<size_t>(half) * half, glm::vec4(0.0f));
        for (int y = 0; y < half; y++)
            for (int x = 0; x < half; x++)
            {
                const size_t i = static_cast<size_t>(2 * y) * n + 2 * x;
                next[static_cast<size_t>(y) * half + x] =
                    0.25f * (level[i] + level[i + 1] + level[i + n] + level[i + n + 1]);
            }
        level.swap(next);
    }
}

Field::Field(std::vector<Cascade> layout) : layout_(std::move(layout))
{
    samples_.resize(layout_.size());
    sample_halves_.resize(layout_.size());
    slopes_.resize(layout_.size());
    work_.resize(layout_.size());
    cascade_low_.assign(layout_.size(), 0.0f);
    cascade_high_.assign(layout_.size(), 0.0f);
    for (size_t c = 0; c < layout_.size(); c++)
    {
        const size_t count = static_cast<size_t>(layout_[c].size) * layout_[c].size;
        samples_[c].assign(count, {});
        sample_halves_[c].assign(count, 0);
        slopes_[c].assign(count, {});
        work_[c][0].assign(count, {});
        work_[c][1].assign(count, {});
    }
}

void Field::SetSpectra(std::vector<CascadeSpectrum> spectra, float choppiness, float mean_level)
{
    if (spectra.size() != layout_.size())
        throw std::invalid_argument("sea: spectra for another layout");
    spectra_ = std::move(spectra);
    target_.clear();
    choppiness_ = choppiness;
    mean_level_ = mean_level;
}

void Field::SetState(const SeaState &state)
{
    SetSpectra(BuildSpectrum(state, layout_), state.choppiness, state.mean_level);
}

void Field::EaseTo(std::vector<CascadeSpectrum> target, float choppiness, float mean_level)
{
    if (target.size() != layout_.size())
        throw std::invalid_argument("sea: spectra for another layout");
    if (!target_.empty())
    {
        // A change during a change: the surface as it stands now is where the new one starts.
        for (size_t c = 0; c < spectra_.size(); c++)
            for (size_t i = 0; i < spectra_[c].amplitude.size(); i++)
                spectra_[c].amplitude[i] += (target_[c].amplitude[i] - spectra_[c].amplitude[i]) * weight_;
        choppiness_ = Mixed(choppiness_, target_choppiness_);
        mean_level_ = Mixed(mean_level_, target_mean_level_);
    }
    target_ = std::move(target);
    target_choppiness_ = choppiness;
    target_mean_level_ = mean_level;
    weight_ = 0.0f;
}

void Field::Ease(float weight)
{
    if (target_.empty())
        return;
    if (weight < 1.0f)
    {
        weight_ = std::max(weight, 0.0f);
        return;
    }
    spectra_ = std::move(target_);
    target_.clear();
    choppiness_ = target_choppiness_;
    mean_level_ = target_mean_level_;
}

void Field::UpdateCascade(size_t cascade, double time, InverseFft2d &transform)
{
    const Cascade &layout = layout_[cascade];
    const int n = layout.size;
    if (transform.Size() != n)
        throw std::invalid_argument("sea: a transform of another size");
    const CascadeSpectrum &spectrum = spectra_[cascade];
    const std::vector<std::complex<float>> *target = target_.empty() ? nullptr : &target_[cascade].amplitude;
    const float choppiness = Mixed(choppiness_, target_choppiness_);
    const auto amplitude = [&](size_t i) {
        return target ? spectrum.amplitude[i] + ((*target)[i] - spectrum.amplitude[i]) * weight_
                      : spectrum.amplitude[i];
    };
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
            const std::complex<float> h = amplitude(i) * turn + std::conj(amplitude(opposite) * turn);
            // Horizontal displacement towards the crests (Tessendorf's choppy waves): i k / |k| h.
            const float length = std::hypot(static_cast<float>(kx), static_cast<float>(ky));
            const std::complex<float> d = i1 * choppiness * h / length;
            hx[i] = h + i1 * (d * static_cast<float>(kx));
            z[i] = d * static_cast<float>(ky);
        }
    transform.Run(hx.data());
    transform.Run(z.data());
    std::vector<Displacement> &out = samples_[cascade];
    std::vector<uint64_t> &halves = sample_halves_[cascade];
    float low = 0.0f, high = 0.0f;
    for (size_t i = 0; i < out.size(); i++)
    {
        // Rounded to the half floats of the vertex shader's texture: the CPU reads what the GPU draws.
        const uint16_t dx = storm::HalfFromFloat(hx[i].imag()), h = storm::HalfFromFloat(hx[i].real()),
                       dz = storm::HalfFromFloat(z[i].real());
        halves[i] = dx | static_cast<uint64_t>(h) << 16 | static_cast<uint64_t>(dz) << 32;
        out[i] = {storm::FloatFromHalf(dx), storm::FloatFromHalf(h), storm::FloatFromHalf(dz), 0.0f};
        low = std::min(low, out[i].h);
        high = std::max(high, out[i].h);
    }
    cascade_low_[cascade] = low;
    cascade_high_[cascade] = high;
    const float half_step = 0.5f * static_cast<float>(n) / layout.patch; // central differences, per metre
    std::vector<Slope> &slopes = slopes_[cascade];
    for (int y = 0; y < n; y++)
    {
        const Displacement *row = &out[static_cast<size_t>(y) * n];
        const Displacement *up = &out[static_cast<size_t>((y + 1) & (n - 1)) * n];
        const Displacement *down = &out[static_cast<size_t>((y - 1) & (n - 1)) * n];
        for (int x = 0; x < n; x++)
        {
            const Displacement &right = row[(x + 1) & (n - 1)], &left = row[(x - 1) & (n - 1)];
            slopes[static_cast<size_t>(y) * n + x] = {(right.h - left.h) * half_step, (up[x].h - down[x].h) * half_step,
                                                      (right.dx - left.dx) * half_step,
                                                      (up[x].dz - down[x].dz) * half_step};
        }
    }
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

float Field::Lowest() const
{
    float sum = MeanLevel();
    for (const float low : cascade_low_)
        sum += low;
    return sum;
}

float Field::Highest() const
{
    float sum = MeanLevel();
    for (const float high : cascade_high_)
        sum += high;
    return sum;
}

glm::vec3 SurfacePoint::Normal() const
{
    const glm::vec3 x(1.0f + along_x.x, along_x.y, along_x.z), z(along_z.x, along_z.y, 1.0f + along_z.z);
    return glm::normalize(glm::cross(z, x));
}

// The bilinear sample and its derivatives exactly as sea_surface.sh computes them on the GPU.
void Field::Sample(size_t cascade, float x, float z, SurfacePoint &sum) const
{
    const Cascade &layout = layout_[cascade];
    const int n = layout.size;
    const float per_metre = static_cast<float>(n) / layout.patch;
    // Doubles: a world position of kilometres in samples of a short cascade keeps its fraction.
    const double u = static_cast<double>(x) / layout.patch * n, v = static_cast<double>(z) / layout.patch * n;
    const double fu = std::floor(u), fv = std::floor(v);
    const auto ix = static_cast<int64_t>(fu), iz = static_cast<int64_t>(fv);
    const float tx = static_cast<float>(u - fu), tz = static_cast<float>(v - fv);
    const auto at = [&](int64_t sx, int64_t sz) {
        const Displacement &d = samples_[cascade][static_cast<size_t>((sz & (n - 1)) * n + (sx & (n - 1)))];
        return glm::vec3(d.dx, d.h, d.dz);
    };
    const glm::vec3 a1 = at(ix, iz), a2 = at(ix + 1, iz), a3 = at(ix, iz + 1), a4 = at(ix + 1, iz + 1);
    sum.offset += glm::mix(glm::mix(a1, a2, tx), glm::mix(a3, a4, tx), tz);
    sum.along_x += glm::mix(a2 - a1, a4 - a3, tz) * per_metre;
    sum.along_z += glm::mix(a3 - a1, a4 - a2, tx) * per_metre;
}

SurfacePoint Field::Point(float x, float z, size_t cascades) const
{
    SurfacePoint point;
    for (size_t c = 0; c < cascades; c++)
        Sample(c, x, z, point);
    return point;
}

glm::vec3 Field::Offset(float x, float z, size_t cascades) const
{
    return Point(x, z, cascades).offset;
}

glm::vec2 Field::StillPoint(float x, float z, size_t cascades) const
{
    glm::vec2 p(x, z);
    for (int i = 0; i < kInversionSteps; i++)
    {
        const SurfacePoint s = Point(p.x, p.y, cascades);
        const glm::vec2 miss = p + glm::vec2(s.offset.x, s.offset.z) - glm::vec2(x, z);
        // d(p + D)/dp, columns along x and z.
        const glm::mat2 jacobian(1.0f + s.along_x.x, s.along_x.z, s.along_z.x, 1.0f + s.along_z.z);
        const float determinant = glm::determinant(jacobian);
        p -= determinant > kFoldDeterminant ? glm::inverse(jacobian) * miss : miss;
    }
    return p;
}

float Field::Height(float x, float z, size_t cascades, glm::vec3 *normal) const
{
    const glm::vec2 p = StillPoint(x, z, cascades);
    const SurfacePoint s = Point(p.x, p.y, cascades);
    if (normal)
        *normal = s.Normal();
    return s.offset.y + MeanLevel();
}
} // namespace storm::sea::ocean
