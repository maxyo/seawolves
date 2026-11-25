#include "effects/thermal.hpp"

#include "storm/render/log.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

#ifdef __ANDROID__
#include <android/api-level.h>
#include <dlfcn.h>
#endif

namespace storm::effects
{

namespace
{
std::atomic<const char *> g_hold{nullptr};
std::atomic<bool> g_playing{false};
std::atomic<int> g_battery{kNoBatteryTemperature - 1}; // below kNoBatteryTemperature: nothing received yet

void battery_text(char *out, size_t size)
{
    const int tenths = g_battery.load();
    if (tenths < kNoBatteryTemperature)
        snprintf(out, size, "battery not reported yet");
    else if (tenths == kNoBatteryTemperature)
        snprintf(out, size, "battery temperature unavailable");
    else
        snprintf(out, size, "battery %.1f C", tenths / 10.0);
}

#ifdef __ANDROID__
ThermalSensor &device_sensor()
{
    static void *const library = [] {
        void *handle = dlopen("libandroid.so", RTLD_NOW);
        if (!handle)
            render::Log().error("effects: thermal load sensing: libandroid.so did not load");
        return handle;
    }();
    static ThermalSensor sensor(android_get_device_api_level(),
                                [](const char *name) { return library ? dlsym(library, name) : nullptr; });
    return sensor;
}
#endif
} // namespace

ThermalSensor::ThermalSensor(int level, const std::function<void *(const char *)> &lookup)
{
    {
        const auto resolution = resolve_thermal_api(level, lookup);
        if (!resolution.error.empty())
            render::Log().error("effects: thermal load sensing: {}", resolution.error);
        if (!resolution.available)
        {
            render::Log().error("effects: thermal load sensing: no thermal signal on Android API {} (AThermal needs "
                                "API 30): the background work never yields to heat on this device",
                                level);
            return;
        }
        api_ = resolution.api;
        manager_ = api_.acquire();
        if (!manager_)
        {
            render::Log().error("effects: thermal load sensing: AThermal_acquireManager returned null on Android API "
                                "{}: no thermal signal, the background work never yields to heat",
                                level);
            return;
        }
        std::string thresholds = "none (API 35 has them)";
        if (api_.thresholds)
        {
            const ThermalThreshold *list = nullptr;
            size_t count = 0;
            if (const int failure = api_.thresholds(manager_, &list, &count))
                thresholds = "the query failed (error " + std::to_string(failure) + ")";
            else
            {
                // A status the device has no threshold for comes as NaN (or is left out).
                thresholds.clear();
                for (size_t i = 0; list && i < count; ++i)
                    if (std::isfinite(list[i].headroom))
                    {
                        char item[48];
                        snprintf(item, sizeof(item), "%s%s %.2f", thresholds.empty() ? "" : ", ",
                                 thermal_status_name(list[i].status), static_cast<double>(list[i].headroom));
                        thresholds += item;
                    }
                if (thresholds.empty())
                    thresholds = "the device defines none (its status may stay NONE while it heats)";
                levels_ = headroom_levels(list, count);
            }
            if (level >= kThermalThresholdsOwnedApi)
                delete[] list; // from API 36 the caller owns the array (android/thermal.h)
        }
        if (!api_.headroom)
            render::Log().error("effects: thermal load sensing: no headroom on Android API {} (API 31 has it): the "
                                "status alone decides, and some devices keep it NONE while they heat",
                                level);
        render::Log().info(
            "effects: thermal load sensing: AThermal status{} (API {}); device headroom thresholds: {}; one thread "
            "from headroom {:.2f} ({}), waiting from {:.2f} ({}) or after {} s warm, normal again below {:.2f}",
            api_.headroom ? " and headroom now and in 10 s" : "", level, thresholds, levels_.warm,
            levels_.device_light ? "the device's light" : "default", levels_.stop,
            levels_.device_moderate ? "the device's moderate" : "ADPF heuristic", kSustainedWarmMilliseconds / 1000,
            levels_.cool);
    }
    state_ = ThermalState(levels_);
}

ThermalLoad ThermalSensor::sample(int64_t now)
{
    // Do not overlap platform calls from the renderer and background workers.
    // A busy caller uses the last decision; it never stalls drawing on a sensor.
    std::unique_lock lock(sample_mutex_, std::try_to_lock);
    if (lock && manager_ && now - asked_ >= 1000)
    {
        asked_ = now;
        const int status = api_.status(manager_);
        if (api_.headroom && now - headroom_asked_ >= kHeadroomPollMilliseconds)
        {
            headroom_asked_ = now;
            Headroom &slot = ask_forecast_ ? forecast_headroom_ : now_headroom_;
            const float sample = api_.headroom(manager_, ask_forecast_ ? kHeadroomForecastSeconds : 0);
            ask_forecast_ = !ask_forecast_;
            ++reads_;
            if (std::isfinite(sample) && sample >= 0.0f)
            {
                slot.value = sample;
                slot.at = now;
            }
            else
            {
                ++unavailable_;
                if (render::Once(said_unavailable_))
                    render::Log().error(
                        "effects: thermal load sensing: AThermal_getThermalHeadroom returned {:.2f} (read {}): {}",
                        sample, reads_,
                        had_headroom_ ? "a reading is kept for 25 s, then the status alone decides"
                                      : "the device may not support it; the status alone decides");
            }
            had_headroom_ = had_headroom_ || std::isfinite(sample);
        }
        const ThermalInput input{status, now_headroom_.valid(now), forecast_headroom_.valid(now), now};
        const bool changed = state_.update(input);
        char battery[48];
        battery_text(battery, sizeof(battery));
        char reading[192];
        snprintf(reading, sizeof(reading), "%s, headroom %.2f now / %.2f in %d s (%u reads, %u unavailable), %s",
                 thermal_status_name(status), static_cast<double>(input.headroom_now),
                 static_cast<double>(input.headroom_forecast), kHeadroomForecastSeconds, reads_, unavailable_, battery);
        if (changed || status != last_status_)
            render::Log().info("effects: thermal load {} ({}): {}; status {}", thermal_load_name(state_.load()),
                               state_.reason(), background_policy(state_.load()), reading);
        last_status_ = status;
        load_ = state_.load();
        std::lock_guard text(text_mutex_);
        snprintf(text_, sizeof(text_), "%s; load %s for %lld s (%s)", reading, thermal_load_name(state_.load()),
                 static_cast<long long>((now - state_.since_ms()) / 1000), state_.reason().c_str());
    }
    return load_.load();
}

void ThermalSensor::text(char (&out)[kTextSize]) const
{
    std::lock_guard lock(text_mutex_);
    std::memcpy(out, text_, sizeof(out));
}

ThermalLoad thermal_load()
{
#ifdef __ANDROID__
    const int64_t now =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
    return device_sensor().sample(now);
#else
    return ThermalLoad::Normal;
#endif
}

bool device_hot()
{
    return thermal_load() != ThermalLoad::Normal;
}

const char *thermal_text()
{
#ifdef __ANDROID__
    static thread_local char text[ThermalSensor::kTextSize];
    device_sensor().text(text);
    return text;
#else
    return "no thermal signal on this platform";
#endif
}

void battery_temperature(int tenths_celsius)
{
    // The first broadcast comes at the game's start, before the engine's log may be open: the value shows in the
    // thermal lines (thermal_text), a missing one as an error.
    const int was = g_battery.exchange(tenths_celsius);
    if (tenths_celsius == kNoBatteryTemperature && was != kNoBatteryTemperature)
        render::Log().error("effects: thermal load sensing: the battery broadcast has no temperature");
}

void hold_background_work(const char *why)
{
    const char *was = g_hold.exchange(why);
    if (why && !was)
        render::Log().info("effects: background work (relief generation, ray bakes) held: {}", why);
    else if (!why && was)
        render::Log().info("effects: background work (relief generation, ray bakes) released (was held: {})", was);
}

void background_play(bool playing)
{
    g_playing = playing;
}

const char *background_wait_reason()
{
    if (const char *held = g_hold.load())
        return held;
    // The sensor is read whether or not the game is played: its changes are logged.
    return background_waits(thermal_load()) && g_playing.load() ? kThermalWaitReason : nullptr;
}

} // namespace storm::effects
