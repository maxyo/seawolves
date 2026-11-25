// The thermal sensor (src/effects/thermal.cpp ThermalSensor) on a fake AThermal of API 37 and a clock of its own: its
// reading cadence, the loads it decides and its reading text; and the background work's hold.
#include "effects/thermal.hpp"
#include "storm/render/log.hpp"

#include <catch2/catch.hpp>
#include <spdlog/sinks/callback_sink.h>

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace storm::effects;

namespace
{
int status = 0, status_calls = 0;
float headroom = .22f;
std::vector<int> headroom_asks; // forecastSeconds of each call
std::vector<int64_t> headroom_times;
int64_t now_ms = 100000;
int errors = 0;

void *Acquire()
{
    return reinterpret_cast<void *>(1);
}
int Status(void *)
{
    ++status_calls;
    return status;
}
float Headroom(void *, int seconds)
{
    headroom_asks.push_back(seconds);
    headroom_times.push_back(now_ms);
    return seconds ? headroom + 0.01f : headroom;
}
// The device defines no threshold below shutdown (as the emulator reports: NaN for the others).
const ThermalThreshold kThresholds[] = {
    {std::nanf(""), 0}, {std::nanf(""), 1}, {std::nanf(""), 2}, {std::nanf(""), 3}, {1.4f, 6}};
int Thresholds(void *, const ThermalThreshold **list, size_t *count)
{
    auto *copy = new ThermalThreshold[5]; // from API 36 the caller owns the array
    std::memcpy(copy, kThresholds, sizeof(kThresholds));
    *list = copy;
    *count = 5;
    return 0;
}
void *Lookup(const char *name)
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

// One second of frames: the renderer asks every frame, the sensors are read at their own cadence.
ThermalLoad second(ThermalSensor &sensor)
{
    ThermalLoad load = ThermalLoad::Normal;
    for (int frame = 0; frame < 60; ++frame)
    {
        now_ms += 1000 / 60;
        load = sensor.sample(now_ms);
    }
    now_ms += 1000 - 60 * (1000 / 60);
    return load;
}

std::string text(const ThermalSensor &sensor)
{
    char out[ThermalSensor::kTextSize];
    sensor.text(out);
    return out;
}
} // namespace

TEST_CASE("the sensor's cadence and loads")
{
    storm::render::SetLogSinks(
        "thermal", {std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg &msg) {
            errors += msg.level >= spdlog::level::err;
        })},
        spdlog::level::debug, spdlog::level::off);
    ThermalSensor sensor(37, Lookup);
    CHECK(errors == 0);
    CHECK(sensor.sample(now_ms) == ThermalLoad::Normal);
    // Status once a second; the headroom every five seconds, now and the forecast in turn.
    for (int i = 0; i < 20; ++i)
        second(sensor);
    // At the start, then once in each of the next 19 seconds (the frames run 16 ms apart).
    CHECK(status_calls == 20);
    REQUIRE(headroom_asks.size() == 4);
    for (size_t i = 0; i < headroom_asks.size(); ++i)
        CHECK(headroom_asks[i] == (i % 2 ? kHeadroomForecastSeconds : 0));
    for (size_t i = 1; i < headroom_times.size(); ++i)
        CHECK(headroom_times[i] - headroom_times[i - 1] >= kHeadroomPollMilliseconds);
    CHECK(text(sensor).find("headroom 0.22 now / 0.23 in 10 s") != std::string::npos);
    CHECK(text(sensor).find("load normal for 19 s") != std::string::npos);
    battery_temperature(385);
    second(sensor);
    CHECK(text(sensor).find("battery 38.5 C") != std::string::npos);

    // Status NONE with the headroom past the warm level: one thread; warm for long: it waits; cooling resumes.
    headroom = .76f;
    for (int i = 0; i < 10; ++i)
        CHECK(second(sensor) != ThermalLoad::Hold);
    CHECK(sensor.sample(now_ms) == ThermalLoad::Elevated);
    for (int i = 0; i < 175; ++i)
        second(sensor);
    CHECK(sensor.sample(now_ms) == ThermalLoad::Hold);
    headroom = .65f;
    for (int i = 0; i < 30; ++i)
        second(sensor);
    CHECK(sensor.sample(now_ms) == ThermalLoad::Hold); // above the cool level
    headroom = .5f;
    for (int i = 0; i < 30; ++i)
        second(sensor);
    CHECK(sensor.sample(now_ms) == ThermalLoad::Normal);
    // The platform's status counts at once.
    status = 2;
    CHECK(second(sensor) == ThermalLoad::Hold);
    status = 0;
    CHECK(second(sensor) == ThermalLoad::Normal);

    // An unavailable headroom is an error once; the readings expire and the status alone decides.
    headroom = std::nanf("");
    const int before = errors;
    for (int i = 0; i < 40; ++i)
        second(sensor);
    CHECK(errors == before + 1);
    CHECK(text(sensor).find("headroom nan now / nan in 10 s") != std::string::npos);
    CHECK(text(sensor).find(" 0 unavailable") == std::string::npos);
    battery_temperature(kNoBatteryTemperature);
    CHECK(errors == before + 2);
}

TEST_CASE("an explicit hold of the background work")
{
    background_play(true);
    CHECK(background_wait_reason() == nullptr); // the host has no heat
    hold_background_work("renderer measurement");
    CHECK(std::strcmp(background_wait_reason(), "renderer measurement") == 0);
    hold_background_work(nullptr);
    CHECK(background_wait_reason() == nullptr);
}
