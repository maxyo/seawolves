#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cstdarg>
#include <string>
#include <vector>

static std::vector<std::string> logs;

static int64_t now_ms = 100000;
static int status = 0, status_calls = 0;
static std::vector<int> headroom_asks; // forecastSeconds of each call
static std::vector<int64_t> headroom_times;
static float headroom = .22f;
static auto TestClockNow()
{
    return std::chrono::steady_clock::time_point(std::chrono::milliseconds(now_ms));
}

// The runner copies the production TU and substitutes only its clock expression.
#define dlopen TestDlopen
#define dlsym TestDlsym
#include "thermal_under_test.cpp"
#undef dlopen
#undef dlsym
#include "effects/scene_readiness.hpp"

namespace storm::shim
{
void logf(const char *format, ...)
{
    char text[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    logs.emplace_back(text);
}
} // namespace storm::shim

static void *Acquire() { return reinterpret_cast<void *>(1); }
static int Status(void *) { ++status_calls; return status; }
static float Headroom(void *, int seconds)
{
    headroom_asks.push_back(seconds);
    headroom_times.push_back(now_ms);
    return seconds ? headroom + 0.01f : headroom;
}
// The device defines no threshold below shutdown (as the emulator reports: NaN for the others).
static const storm::effects::ThermalThreshold kThresholds[] = {
    {std::nanf(""), 0}, {std::nanf(""), 1}, {std::nanf(""), 2}, {std::nanf(""), 3}, {1.4f, 6}};
static int Thresholds(void *, const storm::effects::ThermalThreshold **list, size_t *count)
{
    *list = new storm::effects::ThermalThreshold[5];
    std::memcpy(const_cast<storm::effects::ThermalThreshold *>(*list), kThresholds, sizeof(kThresholds));
    *count = 5;
    return 0;
}
extern "C" void *TestDlopen(const char *, int) noexcept { return reinterpret_cast<void *>(1); }
extern "C" void *TestDlsym(void *, const char *name) noexcept
{
    if (!std::strcmp(name, "AThermal_acquireManager")) return reinterpret_cast<void *>(&Acquire);
    if (!std::strcmp(name, "AThermal_getCurrentThermalStatus")) return reinterpret_cast<void *>(&Status);
    if (!std::strcmp(name, "AThermal_getThermalHeadroom")) return reinterpret_cast<void *>(&Headroom);
    if (!std::strcmp(name, "AThermal_getThermalHeadroomThresholds")) return reinterpret_cast<void *>(&Thresholds);
    return nullptr;
}
static void check(bool ok, const char *why)
{
    if (!ok) { std::fprintf(stderr, "FAILED: %s\n", why); for (const auto &l : logs) std::fprintf(stderr, "  log: %s\n", l.c_str()); std::exit(1); }
}
static bool logged(const char *text)
{
    for (const auto &line : logs)
        if (line.find(text) != std::string::npos) return true;
    return false;
}
static size_t count_logged(const char *text)
{
    size_t n = 0;
    for (const auto &line : logs) n += line.find(text) != std::string::npos;
    return n;
}
// One second of frames: the renderer asks every frame, the sensors are read at their own cadence.
static void second()
{
    for (int frame = 0; frame < 60; ++frame) { now_ms += 1000 / 60; storm::effects::thermal_load(); }
    now_ms += 1000 - 60 * (1000 / 60);
}
int main()
{
    using namespace storm::effects;
    background_play(true);
    check(thermal_load() == ThermalLoad::Normal, "cool at the start");
    check(logged("effects: thermal load sensing: AThermal status and headroom now and in 10 s (API 37)"), "the start line says what is read");
    check(logged("device headroom thresholds: shutdown 1.40; one thread"), "undefined (NaN) thresholds are left out");
    check(logged("waiting from 0.95 (ADPF heuristic) or after 180 s warm"), "the start line names the levels");
    check(logged("effects: thermal load normal (cool): "), "the first reading is logged");
    // Cadence: status once a second; headroom every five seconds, now and the forecast in turn.
    for (int i = 0; i < 20; ++i) second();
    // Reads at 0 s and then once in each of the next 19 seconds (the frames run 16 ms apart).
    check(status_calls == 20, "status once a second");
    check(headroom_asks.size() == 4, "headroom every five seconds");
    for (size_t i = 0; i < headroom_asks.size(); ++i)
        check(headroom_asks[i] == (i % 2 ? kHeadroomForecastSeconds : 0), "now and the forecast in turn");
    for (size_t i = 1; i < headroom_times.size(); ++i)
        check(headroom_times[i] - headroom_times[i - 1] >= kHeadroomPollMilliseconds, "headroom calls stay apart");
    check(std::strstr(thermal_text(), "none, headroom 0.22 now / 0.23 in 10 s") != nullptr, "the reading has both headrooms");
    check(std::strstr(thermal_text(), "battery not reported yet") != nullptr, "no battery broadcast yet: said so");
    check(std::strstr(thermal_text(), "load normal for 19 s") != nullptr, "the spell counts from the first reading");
    battery_temperature(385);
    second();
    check(std::strstr(thermal_text(), "battery 38.5 C") != nullptr, "the reading has the battery temperature");
    // A phone whose status stays NONE: the headroom rising past 0.75; the work slows, then stops when warmth goes on.
    headroom = .76f;
    for (int i = 0; i < 10; ++i) second();
    check(thermal_load() == ThermalLoad::Elevated, "status none, headroom 0.76: one thread");
    check(background_wait_reason() == nullptr, "one thread, no wait");
    check(logged("effects: thermal load elevated (headroom 0.7"), "the change is logged with its reason");
    for (int i = 0; i < 175; ++i) second();
    check(thermal_load() == ThermalLoad::Hold, "warm for 180 s without cooling: waits");
    check(logged("effects: thermal load hold (warm for 18"), "the stop is logged with its reason");
    check(background_wait_reason() == kThermalWaitReason, "waits while the game is played");
    check(SceneCooling(true, false, background_wait_reason()), "a reveal does not wait for work that waits to cool");
    background_play(false);
    check(background_wait_reason() == nullptr, "a place's loading is not held");
    background_play(true);
    hold_background_work("renderer measurement");
    check(std::strcmp(background_wait_reason(), "renderer measurement") == 0, "an explicit hold comes first");
    hold_background_work(nullptr);
    // Cooling resumes it.
    headroom = .65f;
    for (int i = 0; i < 30; ++i) second();
    check(thermal_load() == ThermalLoad::Hold, "above the cool level: still waits");
    headroom = .5f;
    for (int i = 0; i < 30; ++i) second();
    check(thermal_load() == ThermalLoad::Normal && background_wait_reason() == nullptr, "cooled: resumes");
    check(logged("effects: thermal load normal (cool: status none, headroom 0.5"), "the resume is logged");
    // The platform's status still counts at once.
    status = 2; second();
    check(thermal_load() == ThermalLoad::Hold, "moderate: waits");
    check(logged("effects: thermal load hold (status moderate: about to throttle)"), "the status is named");
    status = 0; second();
    check(thermal_load() == ThermalLoad::Normal, "status back to none, headroom cool: normal");
    // An unavailable headroom is an error once, and then expires: the status alone decides.
    headroom = std::nanf(""); status = 0;
    for (int i = 0; i < 40; ++i) second();
    check(count_logged("ERROR: effects: thermal load sensing: AThermal_getThermalHeadroom returned nan") == 1, "NaN said once");
    check(std::strstr(thermal_text(), "headroom nan now / nan in 10 s") != nullptr, "expired readings are unknown");
    check(std::strstr(thermal_text(), "unavailable)") != nullptr && std::strstr(thermal_text(), " 0 unavailable") == nullptr,
          "the reading counts the unavailable reads");
    battery_temperature(kNoBatteryTemperature);
    check(logged("ERROR: effects: thermal load sensing: the battery broadcast has no temperature"), "a missing battery temperature is an error");
    std::puts("thermal production polling: status every second, headroom now/forecast every 5 s, warm -> one thread, "
              "sustained warmth -> waits in play only, cooling resumes, missing signals logged");
}
