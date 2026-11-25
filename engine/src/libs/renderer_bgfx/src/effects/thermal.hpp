// When the background work must yield: the relief generation and the ray bakes (the place's probes, the town light:
// fx::SetBakeGate). Two things hold it: an explicit renderer hold (the automatic graphics preset measures the GPU:
// renderer auto_preset.hpp), and the device's heat while the game is played (thermal_api.hpp ThermalState): a warming
// device gives it one thread (effects.cpp sets fx::CpuAdmission's gameplay limit every frame), a device about to
// throttle or warm for minutes without cooling has it wait until it cools. A place's loading is never paused or slowed
// by heat, and the game's own frame rate and resolution never change for it.
#pragma once

#include "effects/thermal_api.hpp"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <mutex>

namespace storm::effects
{
// Why the background work waits for the device to cool (background_wait_reason).
inline constexpr const char *kThermalWaitReason =
    "thermal protection (the device is about to throttle or stays warm while the game is played; waiting to cool, a "
    "place's loading goes on)";

// The device's heat (thermal_api.hpp; AThermal, Android 11+ / API 30, headroom API 31, thresholds API 35, through
// dlsym: the app's minimum API is 26). The status is read at most once a second, the headroom every five seconds
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

// The AThermal readings of the process (thermal_api.hpp): the symbols found through `lookup` on Android API `level`.
// sample() is called by the renderer every frame and by the background workers; a caller that finds another one
// reading gets the last decision, so drawing never waits on a sensor.
class ThermalSensor
{
  public:
    static constexpr size_t kTextSize = 320;
    ThermalSensor(int level, const std::function<void *(const char *)> &lookup);
    // The load at `now` (steady clock, milliseconds): the status read at most once a second, the headroom every
    // kHeadroomPollMilliseconds.
    ThermalLoad sample(int64_t now);
    // The last reading (thermal_text).
    void text(char (&out)[kTextSize]) const;

  private:
    // A headroom reading kept for kHeadroomValidMilliseconds.
    struct Headroom
    {
        float value = std::nanf("");
        int64_t at = 0;
        float valid(int64_t now) const
        {
            return now - at < kHeadroomValidMilliseconds ? value : std::nanf("");
        }
    };

    ThermalApi api_;
    void *manager_ = nullptr;
    HeadroomLevels levels_;
    std::mutex sample_mutex_;
    ThermalState state_; // under sample_mutex_, as the members below
    int64_t asked_ = -1000000, headroom_asked_ = -1000000;
    bool ask_forecast_ = false, had_headroom_ = false, said_unavailable_ = false;
    unsigned reads_ = 0, unavailable_ = 0;
    Headroom now_headroom_, forecast_headroom_;
    int last_status_ = -2;
    std::atomic<ThermalLoad> load_{ThermalLoad::Normal};
    mutable std::mutex text_mutex_;
    char text_[kTextSize] = "no reading yet"; // under text_mutex_
};

} // namespace storm::effects
