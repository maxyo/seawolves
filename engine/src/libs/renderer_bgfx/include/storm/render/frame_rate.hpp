#pragma once

#include <cstdint>
#include <vector>

namespace storm::render
{
// The frame rate the game window is paced at, frames per second (Android: Swappy and the ADPF target, the
// settings' «Частота кадров»; src/shim/frame_pacing.hpp). Applies at once and is logged; before the device
// exists it is the rate the device starts with. No effect where the renderer does not pace (desktop).
void SetFrameRate(uint32_t fps);
// Presents in step with the display (desktop; on Android the pacing always does), from the next frame; before the
// device exists, how it starts (Renderer: engine.ini vsync). Logged when it changes.
void SetVSync(bool on);
bool VSync();
// While on, the intervals between the window's presents (ms) are kept until TakePresentIntervals takes them (the
// automatic graphics preset measures with them); off drops them. Nothing is kept where the renderer does not pace.
void SamplePresentIntervals(bool on);
std::vector<float> TakePresentIntervals();

// The GPU time of one frame from bgfx's frame timer query, with the bgfx frame number it measured.
struct GpuFrameTime
{
    uint32_t frame = 0;
    float ms = 0.0f;
};
struct BenchFrames
{
    uint32_t presented = 0;        // bgfx frame number of the last presented frame
    std::vector<GpuFrameTime> gpu; // the GPU times that arrived since the last call, each frame once
    double refresh_hz = 0.0;       // the display's refresh rate (0: unknown)
    bool swappy = false;           // presents go through Swappy
};
// The renderer bench (renderer bench.hpp; debuggable Android builds only): while on, frames are presented without
// Swappy, at the display's fastest mode, so they queue back to back and the GPU stays busy; every frame's GPU time is
// kept for TakeBenchFrames. Off restores the frame rate setting's pacing. Logged both ways. Android only.
void SetBenchPresentation(bool on);
BenchFrames TakeBenchFrames();
// Whether a GPU frame time has arrived since the device started (the bench measures nothing without timer queries).
bool GpuFrameTimesArrive();
} // namespace storm::render
