#pragma once

namespace storm::relief
{
// Which device a relief texture runs on (relief.hpp Session::SetGame): the game's state decides it, not a measurement.
// While the player sees the scene the GPU is the game's: a GPU texture beside the game's frames took 13-39 s on a
// Pixel 10 Pro (PowerVR D-Series) and halved the frame rate. A GPU texture stops at its next submission when the player
// sees the scene (Outcome::Yielded: back to the queue, for the CPU).
enum class DeviceReason
{
    None,          // no texture has chosen yet
    NoGpu,         // no usable GPU (none, no graphs, a crash recorded, its networks failed to load)
    SceneShown,    // the player sees the game's scene
    GameBusy,      // the game draws its scene under the loading cover
    GpuFree,       // the game leaves the GPU free
    LoadingWindow, // the place's loading window
};
struct DeviceChoice
{
    bool gpu = false;
    DeviceReason why = DeviceReason::None;
};
// `loading_window()` is asked only when the window decides (it consumes the window's one texture).
template <class LoadingWindow>
DeviceChoice ChooseTextureDevice(bool gpu_possible, bool scene_shown, bool gpu_free, LoadingWindow &&loading_window)
{
    if (!gpu_possible)
        return {false, DeviceReason::NoGpu};
    if (scene_shown)
        return {false, DeviceReason::SceneShown};
    if (gpu_free)
        return {true, DeviceReason::GpuFree};
    if (loading_window())
        return {true, DeviceReason::LoadingWindow};
    return {false, DeviceReason::GameBusy};
}
inline const char *DeviceReasonText(DeviceReason why)
{
    switch (why)
    {
    case DeviceReason::None:
        return "no texture yet";
    case DeviceReason::NoGpu:
        return "no usable GPU";
    case DeviceReason::SceneShown:
        return "the player sees the game's scene: the GPU is the game's";
    case DeviceReason::GameBusy:
        return "the game draws its scene under the loading cover";
    case DeviceReason::GpuFree:
        return "the game leaves the GPU free: a loading or an interface screen, or the game in the background";
    case DeviceReason::LoadingWindow:
        return "the place's loading window";
    }
    return "?";
}
} // namespace storm::relief
