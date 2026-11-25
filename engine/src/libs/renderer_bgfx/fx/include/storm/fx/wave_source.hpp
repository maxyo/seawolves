// A hull on the sea as the wakes take it (fx::SeaWakes), independent of the GPU.
#pragma once
#include <array>
#include <cstdint>

namespace storm::fx
{
struct WaveSource
{
    uint64_t identity = 0;                    // stable entity id; zero is an anonymous source
    std::array<double, 2> position{};         // absolute world x, z (metres)
    std::array<float, 2> forward{0.0f, 1.0f}; // unit heading in x, z
    float half_length = 0.5f;                 // metres along `forward`: half the waterline's length
    float half_width = 0.5f;                  // the largest half beam at the waterline
    // The waterline (render::EffectsWaveSource): kHullStations stations evenly spaced from `stern` to `stem`
    // (metres along `forward` from `position`), each with the waterline's centre offset to the side (towards
    // (-forward z, forward x)) and its half beam there.
    static constexpr int kHullStations = 8;
    float stern = -0.5f, stem = 0.5f;
    std::array<float, kHullStations> station_side{};
    std::array<float, kHullStations> station_half{};
    float speed = 0.0f; // m/s along `forward`
};

} // namespace storm::fx
