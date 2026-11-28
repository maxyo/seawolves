// The sea's directional wave spectrum and its cascades. README.md.
#pragma once

#include <complex>
#include <cstdint>
#include <vector>

namespace storm::sea::ocean
{
inline constexpr float kGravity = 9.81f;

// A JONSWAP wave system: significant wave height (metres), peak wavelength (metres), the direction its waves travel
// (radians: (cos, sin) in world x, z) and the cos-2s spreading exponent s (larger: narrower).
struct WaveSystem
{
    float hs = 0.0f;
    float peak_wavelength = 0.0f;
    float direction = 0.0f;
    float spreading = 0.0f;
};

struct SeaState
{
    WaveSystem swell, wind;
    float choppiness = 0.0f; // horizontal displacement factor, 0..0.8
    float mean_level = 0.0f; // metres the still water stands above y = 0
};

// What a game's weather sets on the sea (Sea.Sea2.*, Sea.MaxSeaHeight) and the wind's travel direction in the
// convention of WaveSystem::direction.
struct Sea2Keys
{
    float amp1 = 0.0f, scale1 = 0.0f, amp2 = 0.0f, scale2 = 0.0f;
    float pos_shift = 0.0f;
    float max_sea_height = 0.0f;
    float wind_direction = 0.0f;
};

// The two Sea2 waves as a swell and a wind sea of the heights and lengths the game's frames gave them.
SeaState MapSea2(const Sea2Keys &keys);

// A square patch of size x size samples over `patch` metres, owning the wavenumbers k_low <= |k| < k_high.
struct Cascade
{
    int size = 0;
    float patch = 0.0f;
    float k_low = 0.0f, k_high = 0.0f;
};

// `count` cascades of `size` samples, largest patch first, their bands adjoining.
std::vector<Cascade> CascadeLayout(int count, int size);

// A cascade's spectrum at time 0: per sample (y * size + x, frequency index as InverseFft2d), the complex amplitude
// a(k) of the wave travelling along k and its angular frequency.
struct CascadeSpectrum
{
    std::vector<std::complex<float>> amplitude;
    std::vector<float> omega;
};

// Fixed Gaussian draws times the square root of the spectrum, normalised per system on the cascades' own samples so
// that the field's time-mean height variance is exactly (hs_swell^2 + hs_wind^2) / 16.
std::vector<CascadeSpectrum> BuildSpectrum(const SeaState &state, const std::vector<Cascade> &layout);

// The time-mean height variance (square metres) of the field these spectra make.
double HeightVariance(const std::vector<CascadeSpectrum> &spectra);
} // namespace storm::sea::ocean
