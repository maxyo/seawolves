#include "effects/thermal.hpp"

#include "effects/thermal_api.hpp"
#include "shim/shim.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

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
#ifdef __ANDROID__
std::mutex g_text_mutex;
char g_text[320] = "no reading yet"; // under g_text_mutex
#endif

#ifdef __ANDROID__
struct Thermal
{
    ThermalApi api;
    void *manager = nullptr;
    HeadroomLevels levels;
    Thermal()
    {
        void *library = dlopen("libandroid.so", RTLD_NOW);
        const int level = android_get_device_api_level();
        const auto resolution = resolve_thermal_api(level, [library](const char *name) {
            return library ? dlsym(library, name) : nullptr;
        });
        if (!resolution.error.empty() || (level >= kThermalStatusApi && !library))
            shim::logf("ERROR: effects: thermal load sensing: %s", resolution.error.empty() ? "libandroid.so did not load"
                                                                                           : resolution.error.c_str());
        if (!resolution.available)
        {
            shim::logf("ERROR: effects: thermal load sensing: no thermal signal on Android API %d (AThermal needs API 30): "
                       "the background work never yields to heat on this device", level);
            return;
        }
        api = resolution.api;
        manager = api.acquire();
        if (!manager)
        {
            shim::logf("ERROR: effects: thermal load sensing: AThermal_acquireManager returned null on Android API %d: no "
                       "thermal signal, the background work never yields to heat", level);
            return;
        }
        std::string thresholds = "none (API 35 has them)";
        if (api.thresholds)
        {
            const ThermalThreshold *list = nullptr;
            size_t count = 0;
            if (const int failure = api.thresholds(manager, &list, &count))
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
                levels = headroom_levels(list, count);
            }
            if (level >= kThermalThresholdsOwnedApi)
                delete[] list; // from API 36 the caller owns the array (android/thermal.h)
        }
        if (!api.headroom)
            shim::logf("ERROR: effects: thermal load sensing: no headroom on Android API %d (API 31 has it): the status "
                       "alone decides, and some devices keep it NONE while they heat", level);
        shim::logf("effects: thermal load sensing: AThermal status%s (API %d); device headroom thresholds: %s; one thread "
                   "from headroom %.2f (%s), waiting from %.2f (%s) or after %lld s warm, normal again below %.2f",
                   api.headroom ? " and headroom now and in 10 s" : "", level, thresholds.c_str(),
                   static_cast<double>(levels.warm), levels.device_light ? "the device's light" : "default",
                   static_cast<double>(levels.stop), levels.device_moderate ? "the device's moderate" : "ADPF heuristic",
                   static_cast<long long>(kSustainedWarmMilliseconds / 1000), static_cast<double>(levels.cool));
    }
};

// A headroom reading kept for kHeadroomValidMilliseconds.
struct Headroom
{
    float value = std::nanf("");
    int64_t at = 0;
    float valid(int64_t now) const { return now - at < kHeadroomValidMilliseconds ? value : std::nanf(""); }
};

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
#endif
} // namespace

ThermalLoad thermal_load()
{
#ifdef __ANDROID__
    static Thermal thermal; // thread-safe initialisation
    static std::mutex sample_mutex;
    static ThermalState state(thermal.levels); // under sample_mutex
    static int64_t asked = -1000000, headroom_asked = -1000000;
    static bool ask_forecast = false, had_headroom = false, said_unavailable = false;
    static unsigned reads = 0, unavailable = 0;
    static Headroom now_headroom, forecast_headroom;
    static int last_status = -2;
    static std::atomic<ThermalLoad> load{ThermalLoad::Normal};
    const int64_t now =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    // Do not overlap platform calls from the renderer and background workers.
    // A busy caller uses the last decision; it never stalls drawing on a sensor.
    std::unique_lock lock(sample_mutex, std::try_to_lock);
    if (lock && thermal.manager && now - asked >= 1000)
    {
        asked = now;
        const int status = thermal.api.status(thermal.manager);
        if (thermal.api.headroom && now - headroom_asked >= kHeadroomPollMilliseconds)
        {
            headroom_asked = now;
            Headroom &slot = ask_forecast ? forecast_headroom : now_headroom;
            const float sample = thermal.api.headroom(thermal.manager, ask_forecast ? kHeadroomForecastSeconds : 0);
            ask_forecast = !ask_forecast;
            ++reads;
            if (std::isfinite(sample) && sample >= 0.0f)
            {
                slot.value = sample;
                slot.at = now;
            }
            else
            {
                ++unavailable;
                if (!said_unavailable)
                    shim::logf("ERROR: effects: thermal load sensing: AThermal_getThermalHeadroom returned %.2f (read %u): "
                               "%s", static_cast<double>(sample), reads,
                               had_headroom ? "a reading is kept for 25 s, then the status alone decides"
                                            : "the device may not support it; the status alone decides");
                said_unavailable = true;
            }
            had_headroom = had_headroom || std::isfinite(sample);
        }
        const ThermalInput input{status, now_headroom.valid(now), forecast_headroom.valid(now), now};
        const bool changed = state.update(input);
        char battery[48];
        battery_text(battery, sizeof(battery));
        char reading[192];
        snprintf(reading, sizeof(reading), "%s, headroom %.2f now / %.2f in %d s (%u reads, %u unavailable), %s",
                 thermal_status_name(status), static_cast<double>(input.headroom_now),
                 static_cast<double>(input.headroom_forecast), kHeadroomForecastSeconds, reads, unavailable, battery);
        if (changed || status != last_status)
            shim::logf("effects: thermal load %s (%s): %s; status %s", thermal_load_name(state.load()),
                       state.reason().c_str(), background_policy(state.load()), reading);
        last_status = status;
        load = state.load();
        std::lock_guard text(g_text_mutex);
        snprintf(g_text, sizeof(g_text), "%s; load %s for %lld s (%s)", reading, thermal_load_name(state.load()),
                 static_cast<long long>((now - state.since_ms()) / 1000), state.reason().c_str());
    }
    return load.load();
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
    static thread_local char text[sizeof(g_text)];
    std::lock_guard lock(g_text_mutex);
    std::memcpy(text, g_text, sizeof(text));
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
        shim::logf("ERROR: effects: thermal load sensing: the battery broadcast has no temperature");
}

void hold_background_work(const char *why)
{
    const char *was = g_hold.exchange(why);
    if (why && !was)
        shim::logf("effects: background work (relief generation, ray bakes) held: %s", why);
    else if (!why && was)
        shim::logf("effects: background work (relief generation, ray bakes) released (was held: %s)", was);
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
