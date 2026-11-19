#include "thermal_api.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>

using namespace storm::effects;

static void *Acquire() { return nullptr; }
static int Status(void *) { return 0; }
static float Headroom(void *, int) { return 0.f; }
static int Thresholds(void *, const ThermalThreshold **, size_t *) { return 0; }

static void check(bool ok, const char *what)
{
    if (!ok)
    {
        std::fprintf(stderr, "FAILED: %s\n", what);
        std::exit(1);
    }
}

// A lookup that knows exactly the NDK's real names, as libandroid.so exports them (the names a wrong one cannot match).
static void *Real(const char *name)
{
    if (!std::strcmp(name, "AThermal_acquireManager"))
        return reinterpret_cast<void *>(&Acquire);
    if (!std::strcmp(name, "AThermal_getCurrentThermalStatus"))
        return reinterpret_cast<void *>(&Status);
    if (!std::strcmp(name, "AThermal_getThermalHeadroom"))
        return reinterpret_cast<void *>(&Headroom);
    if (!std::strcmp(name, "AThermal_getThermalHeadroomThresholds"))
        return reinterpret_cast<void *>(&Thresholds);
    return nullptr;
}

int main()
{
    check(!resolve_thermal_api(29, Real).available && resolve_thermal_api(29, Real).error.empty(), "API 29: none, no error");
    for (int level : {30, 31, 35, 37})
    {
        const auto r = resolve_thermal_api(level, Real);
        check(r.available && r.error.empty(), "real symbols resolve on API >= 30");
        check((r.api.headroom != nullptr) == (level >= 31), "headroom resolves from API 31");
        check((r.api.thresholds != nullptr) == (level >= 35), "thresholds resolve from API 35");
    }
    // The bug: a wrong status name left the API unresolved on Android 16 and only an info line said so.
    const auto wrong = resolve_thermal_api(36, [](const char *name) {
        return std::strcmp(name, "AThermal_getCurrentThermalStatus") ? Real(name) : nullptr;
    });
    check(!wrong.available, "a lookup without AThermal_getCurrentThermalStatus fails");
    check(wrong.error.find("AThermal_getCurrentThermalStatus") != std::string::npos, "the error names the missing symbol");
    std::set<std::string> asked;
    resolve_thermal_api(36, [&](const char *name) { asked.insert(name); return Real(name); });
    check(asked == std::set<std::string>{"AThermal_acquireManager", "AThermal_getCurrentThermalStatus",
                                         "AThermal_getThermalHeadroom", "AThermal_getThermalHeadroomThresholds"},
          "the exact NDK symbol names are asked");
    const auto no_headroom = resolve_thermal_api(31, [](const char *n) {
        return std::strcmp(n, "AThermal_getThermalHeadroom") ? Real(n) : nullptr;
    });
    check(no_headroom.available && !no_headroom.error.empty(), "a missing headroom is an error but the status works");

    const float nan = std::nanf("");
    // Levels: the defaults (ADPF heuristic), and the device's own thresholds when it defines them.
    {
        const HeadroomLevels none = headroom_levels(nullptr, 0);
        check(none.warm == kHeadroomWarm && none.stop == kHeadroomStop && none.cool == kHeadroomCool, "no thresholds: defaults");
        const ThermalThreshold device[] = {{0.6f, kThermalLight}, {0.85f, kThermalModerate}, {1.0f, kThermalSevere}};
        const HeadroomLevels own = headroom_levels(device, 3);
        check(own.warm == 0.6f && own.device_light && own.stop == 0.85f && own.device_moderate, "the device's light and moderate");
        check(std::fabs(own.cool - 0.45f) < 1e-6f, "cool sits the hysteresis below warm");
        const ThermalThreshold late[] = {{0.9f, kThermalLight}, {0.7f, kThermalModerate}};
        const HeadroomLevels low = headroom_levels(late, 2);
        check(low.warm == 0.7f && low.stop == 0.7f && !low.device_light, "warm never above the stop level");
        const ThermalThreshold broken[] = {{nan, kThermalModerate}, {-1.0f, kThermalLight}};
        check(headroom_levels(broken, 2).stop == kHeadroomStop, "invalid thresholds are ignored");
    }
    auto at = [](int status, float now, float ahead, int64_t ms) { return ThermalInput{status, now, ahead, ms}; };
    // The status alone (no headroom).
    {
        ThermalState s;
        check(!s.update(at(0, nan, nan, 0)) && s.load() == ThermalLoad::Normal, "cool, unknown headroom: normal");
        ThermalState late;
        late.update(at(0, nan, nan, 136000));
        check(late.since_ms() == 136000, "the first reading starts the first spell");
        check(s.update(at(1, nan, nan, 1000)) && s.load() == ThermalLoad::Elevated, "light: one thread");
        check(s.update(at(2, nan, nan, 2000)) && s.load() == ThermalLoad::Hold, "moderate: waits");
        check(s.update(at(3, nan, nan, 3000)) && s.load() == ThermalLoad::Severe, "severe");
        check(s.update(at(1, nan, nan, 4000)) && s.load() == ThermalLoad::Hold, "back to light: still waits");
        check(s.reason().find("waiting to cool") != std::string::npos, "the reason says it waits to cool");
        check(s.update(at(0, nan, nan, 5000)) && s.load() == ThermalLoad::Normal, "status none: normal again");
    }
    // The headroom without any status: the higher of now and the forecast decides.
    {
        ThermalState s;
        check(!s.update(at(0, 0.5f, 0.7f, 0)), "below warm: normal");
        check(s.update(at(0, 0.5f, 0.76f, 1000)) && s.load() == ThermalLoad::Elevated, "a warm forecast: one thread");
        check(s.reason().find("0.76 >= 0.75") != std::string::npos, "the reason names the headroom and its level");
        check(!s.update(at(0, 0.62f, 0.61f, 2000)) && s.load() == ThermalLoad::Elevated, "hysteresis keeps it warm");
        check(s.update(at(0, 0.97f, 0.9f, 3000)) && s.load() == ThermalLoad::Hold, "headroom now at 0.95+: waits");
        check(s.update(at(0, 1.02f, nan, 4000)) && s.load() == ThermalLoad::Severe, "headroom 1.0: severe");
        check(s.update(at(0, 0.8f, 0.8f, 5000)) && s.load() == ThermalLoad::Hold, "below severe, not cooled: waits");
        check(!s.update(at(0, 0.61f, 0.6f, 6000)) && s.load() == ThermalLoad::Hold, "above cool: still waits");
        check(s.update(at(0, 0.55f, 0.58f, 7000)) && s.load() == ThermalLoad::Normal, "cooled: normal share again");
        check(s.reason().rfind("cool", 0) == 0, "the reason says it cooled");
        check(!s.update(at(0, 0.7f, 0.7f, 8000)), "after cooling, warm again only from the warm level");
    }
    // A phone whose status stays NONE (Xiaomi, Android 17): status NONE all session, the headroom rising from 0.22; 0.75 reached
    // 14 minutes in, the vendor's thermal manager killed the game 9.5 minutes later. The work slows at 0.75 and stops
    // after kSustainedWarmMilliseconds without cooling, before the kill, even if the headroom never reaches 0.95.
    {
        ThermalState s;
        int64_t ms = 0;
        bool stopped = false;
        int64_t elevated_at = -1, stopped_at = -1;
        for (float h = 0.22f; ms <= int64_t(24 * 60) * 1000; ms += 10000)
        {
            h = std::min(0.9f, 0.22f + 0.53f * float(ms) / float(14 * 60 * 1000));
            s.update(at(0, h, h + 0.01f, ms));
            if (s.load() == ThermalLoad::Elevated && elevated_at < 0)
                elevated_at = ms;
            if (background_waits(s.load()) && !stopped)
            {
                stopped = true;
                stopped_at = ms;
            }
        }
        check(elevated_at > 13 * 60 * 1000 && elevated_at <= 14 * 60 * 1000, "the trace: one thread from about 0.75");
        check(stopped && stopped_at - elevated_at >= kSustainedWarmMilliseconds &&
                  stopped_at - elevated_at < kSustainedWarmMilliseconds + 10000,
              "the trace: stopped after the sustained warm time, the status still none");
        check(stopped_at < elevated_at + int64_t(9.5 * 60 * 1000), "the trace: stopped before the vendor's kill");
        check(s.reason().find("warm for") != std::string::npos || s.reason().find("waiting to cool") != std::string::npos,
              "the trace: the reason says it stays warm");
        // Cooling: the game in a menu, the headroom falling.
        for (float h = 0.9f; h > 0.4f; h -= 0.05f, ms += 10000)
            s.update(at(0, h, h - 0.02f, ms));
        check(s.load() == ThermalLoad::Normal, "the trace: cooled, the work resumes with its normal share");
    }
    // Sustained warmth with a brief cooling resets the timer only when it really cooled.
    {
        ThermalState s;
        s.update(at(0, 0.8f, 0.8f, 0));
        check(s.load() == ThermalLoad::Elevated, "warm");
        s.update(at(0, 0.65f, 0.65f, kSustainedWarmMilliseconds - 1000));
        check(s.load() == ThermalLoad::Elevated, "a little cooler but above cool: still the same warm spell");
        s.update(at(0, 0.65f, 0.65f, kSustainedWarmMilliseconds));
        check(s.load() == ThermalLoad::Hold, "warm for the sustained time: waits");
        s.update(at(0, 0.5f, 0.5f, kSustainedWarmMilliseconds + 1000));
        check(s.load() == ThermalLoad::Normal, "cooled: normal");
        s.update(at(0, 0.8f, 0.8f, kSustainedWarmMilliseconds + 2000));
        s.update(at(0, 0.8f, 0.8f, 2 * kSustainedWarmMilliseconds));
        check(s.load() == ThermalLoad::Elevated, "a new warm spell starts its own timer");
    }
    // An unknown headroom (expired, NaN) with status none is not hot: an unavailable sensor never holds forever.
    {
        ThermalState s;
        s.update(at(0, 0.97f, 0.97f, 0));
        check(s.load() == ThermalLoad::Hold, "about to throttle: waits");
        s.update(at(0, nan, nan, 30000));
        check(s.load() == ThermalLoad::Normal && s.reason().find("unknown") != std::string::npos,
              "headroom unknown and status none: normal, said so");
    }
    check(background_threads(ThermalLoad::Normal, 2) == 2, "cool: the normal share");
    check(background_threads(ThermalLoad::Elevated, 2) == 1, "warm: one thread");
    check(background_threads(ThermalLoad::Hold, 2) == 0 && background_threads(ThermalLoad::Severe, 2) == 0, "waiting: none");
    std::puts("thermal_api: ok");
}
