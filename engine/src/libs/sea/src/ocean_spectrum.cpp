#include "ocean_spectrum.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <string>

namespace storm::sea::ocean
{
namespace
{
// The game's 64 height frames (RESOURCE/Sea, 128 x 128 texels, one tile per 128 / Scale metres), measured over all
// frames of TEHO's set: standard deviation 0.090 and mean 0.320 of the amplitude, energy peaking at 4 to 8 waves
// per tile.
constexpr float kFrameTexels = 128.0f;
constexpr float kHsPerAmp = 4.0f * 0.090f;
constexpr float kMeanPerAmp = 0.320f;
constexpr float kWavesPerTile = 5.0f;

// JONSWAP's mean peak enhancement (Hasselmann et al. 1973). Spreading: a swell narrow, a wind sea broad
// (Horvath 2015's range of cos-2s exponents).
constexpr float kGamma = 3.3f;
constexpr float kSwellSpreading = 16.0f;
constexpr float kWindSpreading = 4.0f;
// The largest PosShift any weather sets (2.5) leans the waves by 0.8: steeper, the short wind waves fold over
// widely and the surface over a point is no longer one.
constexpr float kChoppinessPerPosShift = 0.32f;
constexpr float kMaxChoppiness = 0.8f;

// Patch sizes in metres, largest first: not multiples of each other, so the cascades' repeats never line up.
constexpr float kPatches[] = {997.0f, 173.0f, 31.0f};

// JONSWAP in angular frequency without its constant factor (the systems are normalised on the samples).
double Jonswap(double omega, double peak)
{
    const double sigma = omega <= peak ? 0.07 : 0.09;
    const double r = std::exp(-(omega - peak) * (omega - peak) / (2.0 * sigma * sigma * peak * peak));
    return std::pow(omega, -5.0) * std::exp(-1.25 * std::pow(peak / omega, 4.0)) * std::pow(kGamma, r);
}

// The system's spectral density per wavenumber area at k (|k| = k, angle theta), up to a constant.
double Density(const WaveSystem &system, double k, double theta)
{
    if (system.hs <= 0.0f)
        return 0.0;
    const double omega = std::sqrt(kGravity * k);
    const double peak = std::sqrt(kGravity * 2.0 * std::numbers::pi / system.peak_wavelength);
    const double spread = std::pow(std::abs(std::cos(0.5 * (theta - system.direction))), 2.0 * system.spreading);
    // S(omega) d omega / d k / k: the frequency spectrum over the polar wavenumber plane.
    return Jonswap(omega, peak) * (kGravity / (2.0 * omega)) / k * spread;
}

uint64_t SplitMix(uint64_t x)
{
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

// A complex Gaussian draw of unit mean square for sample (x, y) of cascade c: the same on every run and platform.
std::complex<double> Draw(size_t cascade, int x, int y)
{
    const uint64_t key = SplitMix((cascade << 40) ^ (static_cast<uint64_t>(x) << 20) ^ static_cast<uint64_t>(y));
    const double u1 = (static_cast<double>(key >> 11) + 1.0) / 9007199254740992.0; // (0, 1]
    const double u2 = static_cast<double>(SplitMix(key) >> 11) / 9007199254740992.0;
    const double radius = std::sqrt(-std::log(u1)); // Box-Muller with each part's variance 1/2
    return std::polar(radius, 2.0 * std::numbers::pi * u2);
}

int Signed(int index, int size)
{
    return index < size / 2 ? index : index - size;
}
} // namespace

SeaState MapSea2(const Sea2Keys &keys)
{
    const float total = keys.amp1 + keys.amp2;
    const float cap = total > keys.max_sea_height && total > 0.0f ? keys.max_sea_height / total : 1.0f;
    const auto system = [&](float amp, float scale, float spreading) {
        WaveSystem s;
        s.hs = kHsPerAmp * amp * cap;
        s.peak_wavelength = kFrameTexels / (kWavesPerTile * scale);
        s.direction = keys.wind_direction;
        s.spreading = spreading;
        return s;
    };
    SeaState state;
    state.swell = system(keys.amp1, keys.scale1, kSwellSpreading);
    state.wind = system(keys.amp2, keys.scale2, kWindSpreading);
    state.choppiness = std::clamp(kChoppinessPerPosShift * keys.pos_shift, 0.0f, kMaxChoppiness);
    state.mean_level = kMeanPerAmp * total * cap;
    return state;
}

std::vector<Cascade> CascadeLayout(int count, int size)
{
    if (count < 1 || count > static_cast<int>(std::size(kPatches)) || size < 8 || (size & (size - 1)) != 0)
        throw std::invalid_argument("sea: no cascade layout of " + std::to_string(count) + " x " +
                                    std::to_string(size));
    std::vector<Cascade> layout(count);
    for (int i = 0; i < count; i++)
    {
        const float nyquist = std::numbers::pi_v<float> * static_cast<float>(size) / kPatches[i];
        layout[i] = {size, kPatches[i], i == 0 ? 0.0f : layout[i - 1].k_high,
                     // A cascade keeps waves of four samples or more; the last one goes to its limit.
                     i + 1 < count ? 0.5f * nyquist : nyquist};
    }
    return layout;
}

std::vector<CascadeSpectrum> BuildSpectrum(const SeaState &state, const std::vector<Cascade> &layout)
{
    struct Sample
    {
        std::complex<double> draw;
        double swell = 0.0, wind = 0.0;
    };
    std::vector<std::vector<Sample>> samples(layout.size());
    std::vector<CascadeSpectrum> spectra(layout.size());
    double swell_energy = 0.0, wind_energy = 0.0;
    for (size_t c = 0; c < layout.size(); c++)
    {
        const Cascade &cascade = layout[c];
        const int n = cascade.size;
        const double dk = 2.0 * std::numbers::pi / cascade.patch;
        samples[c].resize(static_cast<size_t>(n) * n);
        spectra[c].amplitude.assign(samples[c].size(), {});
        spectra[c].omega.assign(samples[c].size(), 0.0f);
        for (int y = 0; y < n; y++)
            for (int x = 0; x < n; x++)
            {
                const int kx = Signed(x, n), ky = Signed(y, n);
                const double k = dk * std::hypot(kx, ky);
                // The Nyquist row and column have no opposite sample to pair with: they stay empty.
                if (k < cascade.k_low || k >= cascade.k_high || k == 0.0 || kx == -n / 2 || ky == -n / 2)
                    continue;
                const size_t i = static_cast<size_t>(y) * n + x;
                const double theta = std::atan2(ky, kx);
                Sample &s = samples[c][i];
                s.draw = Draw(c, kx, ky);
                s.swell = Density(state.swell, k, theta) * dk * dk;
                s.wind = Density(state.wind, k, theta) * dk * dk;
                swell_energy += std::norm(s.draw) * s.swell;
                wind_energy += std::norm(s.draw) * s.wind;
                spectra[c].omega[i] = static_cast<float>(std::sqrt(kGravity * k));
            }
    }
    // The time-mean variance is 2 sum |a|^2 (a wave and its opposite share each sample's height).
    const auto weight = [](const WaveSystem &system, double energy, const char *name) {
        if (system.hs <= 0.0f)
            return 0.0;
        if (energy <= 0.0)
            throw std::runtime_error(std::string("sea: the cascades hold none of the ") + name + " (peak " +
                                     std::to_string(system.peak_wavelength) + " m)");
        return static_cast<double>(system.hs) * system.hs / 16.0 / (2.0 * energy);
    };
    const double swell = weight(state.swell, swell_energy, "swell");
    const double wind = weight(state.wind, wind_energy, "wind sea");
    for (size_t c = 0; c < layout.size(); c++)
        for (size_t i = 0; i < samples[c].size(); i++)
        {
            const Sample &s = samples[c][i];
            spectra[c].amplitude[i] = std::complex<float>(s.draw * std::sqrt(swell * s.swell + wind * s.wind));
        }
    return spectra;
}

double HeightVariance(const std::vector<CascadeSpectrum> &spectra)
{
    double sum = 0.0;
    for (const CascadeSpectrum &spectrum : spectra)
        for (const std::complex<float> a : spectrum.amplitude)
            sum += std::norm(std::complex<double>(a));
    return 2.0 * sum;
}
} // namespace storm::sea::ocean
