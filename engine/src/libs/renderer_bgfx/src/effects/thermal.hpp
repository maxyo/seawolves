// When the background work must yield: the relief generation and the ray bakes (the place's probes, the town light:
// fx::SetBakeGate). Two things hold it: an explicit renderer hold (the automatic graphics preset measures the GPU:
// renderer auto_preset.hpp), and the device's heat while the game is played (thermal_api.hpp ThermalState): a warming
// device gives it one thread (effects.cpp sets fx::CpuAdmission's gameplay limit every frame), a device about to
// throttle or warm for minutes without cooling has it wait until it cools. A place's loading is never paused or slowed
// by heat, and the game's own frame rate and resolution never change for it.
#pragma once

#include "effects/thermal_api.hpp"

namespace storm::effects
{
// Why the background work waits for the device to cool (background_wait_reason; scene_readiness.hpp knows it by
// its address: a reveal does not wait for work that waits to cool).
inline constexpr const char *kThermalWaitReason =
    "thermal protection (the device is about to throttle or stays warm while the game is played; waiting to cool, a "
    "place's loading goes on)";

// The device's heat (thermal_api.hpp; AThermal, Android 11+ / API 30, headroom API 31, thresholds API 35, through
// dlsym: the app's minimum API is 24). The status is read at most once a second, the headroom every five seconds
// (now and the forecast in turn). No thermal signal (older Android, a missing symbol) is an error in the log; the host
// has none and stays Normal. Every change is logged with its reason and what it asks of the background work.
ThermalLoad thermal_load();
// Any load above Normal.
bool device_hot();
// The last reading for the log lines: status, headroom now and ahead, battery temperature, the load and since when.
const char *thermal_text();
// Any thread (GamePerformance's battery receiver over JNI): the battery's temperature in tenths of a degree Celsius
// (BatteryManager.EXTRA_TEMPERATURE); kNoBatteryTemperature when the broadcast had none. Reported in the thermal
// lines; it does not decide the load (Android documents no throttling level for it).
inline constexpr int kNoBatteryTemperature = -100000;
void battery_temperature(int tenths_celsius);

// Main thread: the renderer holds the background work (`why` says why; null releases it). Logged on change.
void hold_background_work(const char *why);
// Main thread, every frame: whether the game is played (no place is loading). Heat holds the work only then.
void background_play(bool playing);
// Why the background work must wait now (the explicit hold, or heat while the game is played); null: it may run.
// Any thread.
const char *background_wait_reason();

} // namespace storm::effects
