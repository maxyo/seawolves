// The AThermal NDK API's availability and what the device's heat asks of the background work, free of Android so a
// host check can run them: the symbol lookup is injected (thermal.cpp passes dlsym on libandroid.so) and the clock is
// a parameter. A symbol missing on an API that has it is an error to log loudly, never a quiet "no thermal status" (a
// wrong symbol name once did that on every phone).
//
// The signals (developer.android.com/ndk/reference/group/thermal, developer.android.com/games/optimize/adpf/thermal):
// - AThermal_getCurrentThermalStatus: the platform's throttling level. Some devices report NONE whatever the
//   temperature (the ADPF guide's "Device limitations of the Thermal API"; a Xiaomi with Android 17 reported NONE up
//   to the moment its vendor thermal manager killed the game), so it never decides alone.
// - AThermal_getThermalHeadroom: the share of the thermal envelope in use, from the skin temperature sensor; 1.0 is
//   the SEVERE throttling threshold. Read now (forecast 0) and kHeadroomForecastSeconds ahead; the higher decides.
// - AThermal_getThermalHeadroomThresholds (API 35): the headroom at which the device enters each status, when its
//   manufacturer defines them. Without them the ADPF guide's heuristic levels apply (0.85 light, 0.95 moderate).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace storm::effects
{

// AThermalHeadroomThreshold of android/thermal.h (API 35): the same layout.
struct ThermalThreshold
{
    float headroom;
    int status;
};

struct ThermalApi
{
    void *(*acquire)() = nullptr;                                                // AThermal_acquireManager, API 30
    int (*status)(void *) = nullptr;                                             // AThermal_getCurrentThermalStatus, API 30
    float (*headroom)(void *, int) = nullptr;                                    // AThermal_getThermalHeadroom, API 31
    int (*thresholds)(void *, const ThermalThreshold **, size_t *) = nullptr;    // AThermal_getThermalHeadroomThresholds, API 35
};

struct ThermalResolution
{
    ThermalApi api;
    bool available = false; // the status can be read
    std::string error;      // not empty: the API level has the functions but a symbol did not resolve
};

constexpr int kThermalStatusApi = 30;
constexpr int kThermalHeadroomApi = 31;
constexpr int kThermalThresholdsApi = 35;
// From API 36 the thresholds array belongs to the caller (array delete); before, to the manager.
constexpr int kThermalThresholdsOwnedApi = 36;

// lookup(name) returns the symbol's address or null.
template <class Lookup> ThermalResolution resolve_thermal_api(int api_level, Lookup lookup)
{
    ThermalResolution result;
    if (api_level < kThermalStatusApi)
        return result;
    result.api.acquire = reinterpret_cast<void *(*)()>(lookup("AThermal_acquireManager"));
    result.api.status = reinterpret_cast<int (*)(void *)>(lookup("AThermal_getCurrentThermalStatus"));
    if (!result.api.acquire)
        result.error += " AThermal_acquireManager";
    if (!result.api.status)
        result.error += " AThermal_getCurrentThermalStatus";
    if (api_level >= kThermalHeadroomApi)
    {
        result.api.headroom = reinterpret_cast<float (*)(void *, int)>(lookup("AThermal_getThermalHeadroom"));
        if (!result.api.headroom)
            result.error += " AThermal_getThermalHeadroom";
    }
    if (api_level >= kThermalThresholdsApi)
    {
        result.api.thresholds = reinterpret_cast<int (*)(void *, const ThermalThreshold **, size_t *)>(
            lookup("AThermal_getThermalHeadroomThresholds"));
        if (!result.api.thresholds)
            result.error += " AThermal_getThermalHeadroomThresholds";
    }
    if (!result.error.empty())
        result.error = "Android API " + std::to_string(api_level) + " has AThermal but these symbols did not resolve:" +
                       result.error;
    result.available = result.api.acquire && result.api.status;
    return result;
}

// ATHERMAL_STATUS_LIGHT: "light throttling where UX is not impacted".
constexpr int kThermalLight = 1;
// ATHERMAL_STATUS_MODERATE: "moderate throttling where UX is not largely impacted".
constexpr int kThermalModerate = 2;
// ATHERMAL_STATUS_SEVERE: "severe throttling where UX is largely impacted"; headroom 1.0.
constexpr int kThermalSevere = 3;

inline const char *thermal_status_name(int status)
{
    static const char *const names[] = {"none", "light", "moderate", "severe", "critical", "emergency", "shutdown"};
    return status >= 0 && status < 7 ? names[status] : status == -1 ? "error" : "unknown";
}

// The forecast horizon. The platform keeps sampling the skin sensor once a second only while headroom was asked in the
// last ten seconds (ThermalManagerService's TemperatureWatcher; with fewer than three samples it returns the current
// value as the "forecast"), so it is asked every kHeadroomPollMilliseconds, alternating now and the forecast: each is
// fresh every ten seconds, and the calls stay well apart of the once-a-second limit the NDK reference names.
constexpr int kHeadroomForecastSeconds = 10;
constexpr int kHeadroomPollMilliseconds = 5000;
// A reading older than this is unknown: an unavailable sensor never keeps a hot classification indefinitely.
constexpr int kHeadroomValidMilliseconds = 25000;

// Headroom levels. The background work yields early, before the platform throttles the game: it takes one thread from
// kHeadroomWarm (or the device's LIGHT threshold when that is lower), back to its normal share below kHeadroomCool, so
// the classification does not flap. It stops at the level where the device is about to throttle: the device's
// MODERATE threshold, or without one the ADPF guide's heuristic 0.95 ("could be moderately throttled, reduce workload
// immediately"). 1.0 is SEVERE by the API's definition.
constexpr float kHeadroomWarm = 0.75f;
constexpr float kHeadroomHysteresis = 0.15f;
constexpr float kHeadroomCool = kHeadroomWarm - kHeadroomHysteresis;
constexpr float kHeadroomStop = 0.95f;
constexpr float kHeadroomSevere = 1.0f;
// If the heating goes on, the work stops: a device that stays warm this long with the background work on
// one thread (not cooled below the cool level, nor the status back to NONE) has it stop. A phone's skin temperature
// moves over minutes; the one Android 17 Xiaomi that never left status NONE rose from headroom 0.22 to 0.75 in 14
// minutes and was killed by its vendor thermal manager 9.5 minutes after that.
constexpr int64_t kSustainedWarmMilliseconds = 180000;

struct HeadroomLevels
{
    float warm = kHeadroomWarm;
    float cool = kHeadroomCool;
    float stop = kHeadroomStop;
    bool device_light = false, device_moderate = false; // the level comes from the device's thresholds
};

// The levels for the device's thresholds (AThermal_getThermalHeadroomThresholds; none: the defaults).
inline HeadroomLevels headroom_levels(const ThermalThreshold *thresholds, size_t count)
{
    HeadroomLevels levels;
    for (size_t i = 0; thresholds && i < count; ++i)
    {
        const float value = thresholds[i].headroom;
        if (!std::isfinite(value) || value <= 0.0f)
            continue;
        if (thresholds[i].status == kThermalLight && value < levels.warm)
        {
            levels.warm = value;
            levels.device_light = true;
        }
        if (thresholds[i].status == kThermalModerate)
        {
            levels.stop = value;
            levels.device_moderate = true;
        }
    }
    levels.warm = std::min(levels.warm, levels.stop);
    levels.cool = levels.warm - kHeadroomHysteresis;
    return levels;
}

// What the device's heat asks of the background work (relief and x2 textures, ray bakes) while the game is played.
// The game's own frame rate and resolution never change for heat. A place's loading is never slowed or paused by it:
// nothing is played then, and a load that waited for a phone to cool never ended.
enum class ThermalLoad : unsigned char
{
    Normal,   // the background work's normal share of the cores (fx::CpuAdmission::normal_gameplay_threads)
    Elevated, // the device warms: one thread
    Hold,     // about to throttle, or warm for kSustainedWarmMilliseconds: it waits until the device cools
    Severe,   // SEVERE status or headroom 1.0 and above: it waits, and the platform throttles
};

// Whether the background work waits while the game is played.
inline bool background_waits(ThermalLoad load)
{
    return load == ThermalLoad::Hold || load == ThermalLoad::Severe;
}

// The CPU threads of the background work while the game is played; `normal` is its share on a cool device.
inline unsigned background_threads(ThermalLoad load, unsigned normal)
{
    return background_waits(load) ? 0u : load == ThermalLoad::Elevated ? 1u : normal;
}

inline const char *thermal_load_name(ThermalLoad load)
{
    return load == ThermalLoad::Severe     ? "severe"
           : load == ThermalLoad::Hold     ? "hold"
           : load == ThermalLoad::Elevated ? "elevated"
                                           : "normal";
}

inline const char *background_policy(ThermalLoad load)
{
    return background_waits(load)          ? "while the game is played the background work waits until the device cools (a place's loading goes on)"
           : load == ThermalLoad::Elevated ? "while the game is played the background work takes one thread"
                                           : "the background work takes its normal share of the cores";
}

// One reading of the sensors. NaN: that headroom is unknown (not supported, not yet read, or expired).
struct ThermalInput
{
    int status = 0;
    float headroom_now = std::nanf("");
    float headroom_forecast = std::nanf("");
    int64_t now_ms = 0;
};

// The classification over time: warm -> one thread; about to throttle, or warm without cooling for
// kSustainedWarmMilliseconds -> waits; it waits until the device has cooled (status NONE and headroom below the cool
// level), then takes its normal share again.
class ThermalState
{
  public:
    explicit ThermalState(HeadroomLevels levels = {}) : levels_(levels) {}

    // True when the load changed (the reason says why).
    bool update(const ThermalInput &in)
    {
        if (!sampled_)
            since_ms_ = in.now_ms; // the first reading starts the first spell
        sampled_ = true;
        const float now = in.headroom_now, ahead = in.headroom_forecast;
        const float h = std::isnan(now) ? ahead : std::isnan(ahead) ? now : std::max(now, ahead);
        const bool known = !std::isnan(h);
        const bool severe = in.status >= kThermalSevere || (known && h >= kHeadroomSevere);
        const bool stop = in.status >= kThermalModerate || (known && h >= levels_.stop);
        const bool warm = in.status >= kThermalLight || (known && h >= levels_.warm);
        const bool still_warm = in.status >= kThermalLight || (known && h >= levels_.cool);
        const bool hot = load_ == ThermalLoad::Normal ? warm : still_warm;
        ThermalLoad next = ThermalLoad::Normal;
        char why[160] = "";
        if (!hot && known)
            std::snprintf(why, sizeof(why), "cool: status %s, headroom %.2f below %.2f", thermal_status_name(in.status),
                          static_cast<double>(h), static_cast<double>(load_ == ThermalLoad::Normal ? levels_.warm : levels_.cool));
        else if (!hot)
            std::snprintf(why, sizeof(why), "cool: status %s, headroom unknown", thermal_status_name(in.status));
        else if (severe)
        {
            next = ThermalLoad::Severe;
            signal(why, sizeof(why), in.status, kThermalSevere, h, kHeadroomSevere, "severe throttling");
        }
        else if (stop)
        {
            next = ThermalLoad::Hold;
            signal(why, sizeof(why), in.status, kThermalModerate, h, levels_.stop, "about to throttle");
        }
        else if (background_waits(load_))
        {
            next = ThermalLoad::Hold;
            std::snprintf(why, sizeof(why), "%s; waiting to cool below headroom %.2f and status none", reason_.c_str(),
                          static_cast<double>(levels_.cool));
        }
        else if (load_ == ThermalLoad::Elevated && in.now_ms - since_ms_ >= kSustainedWarmMilliseconds)
        {
            next = ThermalLoad::Hold;
            std::snprintf(why, sizeof(why), "warm for %lld s without cooling below headroom %.2f",
                          static_cast<long long>((in.now_ms - since_ms_) / 1000), static_cast<double>(levels_.cool));
        }
        else
        {
            next = ThermalLoad::Elevated;
            if (load_ != ThermalLoad::Elevated)
                signal(why, sizeof(why), in.status, kThermalLight, h, levels_.warm, "warming");
        }
        if (next == load_)
            return false;
        reason_ = why;
        since_ms_ = in.now_ms;
        load_ = next;
        return true;
    }

    ThermalLoad load() const { return load_; }
    const std::string &reason() const { return reason_; }
    int64_t since_ms() const { return since_ms_; }
    const HeadroomLevels &levels() const { return levels_; }

  private:
    // Which signal crossed: the status, or the headroom against its level.
    static void signal(char *out, size_t size, int status, int status_level, float h, float level, const char *what)
    {
        if (status >= status_level)
            std::snprintf(out, size, "status %s: %s", thermal_status_name(status), what);
        else
            std::snprintf(out, size, "headroom %.2f >= %.2f: %s", static_cast<double>(h), static_cast<double>(level),
                          what);
    }

    HeadroomLevels levels_;
    ThermalLoad load_ = ThermalLoad::Normal;
    int64_t since_ms_ = 0;
    bool sampled_ = false;
    std::string reason_ = "cool";
};

} // namespace storm::effects
