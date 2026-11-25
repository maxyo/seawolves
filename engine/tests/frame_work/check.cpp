// platform/frame_work.hpp: the game thread's frames as the windows of the "frame work:" line see them.
#include "platform/frame_work.hpp"

#include <catch2/catch.hpp>

#include <chrono>
#include <thread>

namespace fw = storm::platform::frame_work;

namespace
{
constexpr auto kStay = std::chrono::milliseconds(60);

double ms(int64_t ns)
{
    return static_cast<double>(ns) * 1e-6;
}

struct Game
{
};
} // namespace

TEST_CASE("a restart drops the frame's time so far, of its phases and of an entity stage open across it")
{
    fw::EndFrame();
    fw::Take(fw::Reader::Summary);
    {
        const fw::Scope phase(fw::Phase::Execute);
        const fw::EntityScope entity(typeid(Game), false);
        // The stay: a phase and a script event ended inside it (charged already), the rest still running.
        {
            const fw::Scope script(fw::Phase::Script);
            fw::BeginEvent("Before", fw::Phase::Execute, fw::Clock::now());
            std::this_thread::sleep_for(kStay / 2);
            fw::EndEvent(fw::Clock::now());
        }
        fw::BeginEvent("Across", fw::Phase::Execute, fw::Clock::now());
        std::this_thread::sleep_for(kStay / 2);
        fw::Restart();
        fw::EndEvent(fw::Clock::now());
    }
    fw::EndFrame();
    const fw::Window window = fw::Take(fw::Reader::Summary);
    INFO(fw::Describe(window));
    REQUIRE(window.frames == 1);
    const double stay = std::chrono::duration<double, std::milli>(kStay).count();
    CHECK(ms(window.worst_work) < stay / 4);
    CHECK(ms(window.worst_entity_ns) < stay / 4);
    CHECK(window.worst_event == "Across");
    CHECK(ms(window.worst_event_ns) < stay / 4);
}

namespace
{
void event(const char *name, fw::Phase origin, std::chrono::milliseconds length)
{
    fw::BeginEvent(name, origin, fw::Clock::now());
    std::this_thread::sleep_for(length);
    fw::EndEvent(fw::Clock::now());
}
} // namespace

TEST_CASE("the slowest frame names its slowest script event and where it was raised")
{
    fw::EndFrame();
    fw::Take(fw::Reader::Summary);
    event("Quick", fw::Phase::Execute, std::chrono::milliseconds(2));
    fw::EndFrame();
    event("Small", fw::Phase::Execute, std::chrono::milliseconds(2));
    event("Slow", fw::Phase::Realize, std::chrono::milliseconds(30));
    event("Later", fw::Phase::Script, std::chrono::milliseconds(2));
    fw::EndFrame();
    fw::EndFrame(); // a frame without events after it
    const fw::Window window = fw::Take(fw::Reader::Summary);
    INFO(fw::Describe(window) << ' ' << fw::DescribeWorstEvent(window));
    CHECK(window.worst_event == "Slow");
    CHECK(window.worst_event_origin == fw::Phase::Realize);
    CHECK(ms(window.worst_event_ns) >= 30);
    CHECK(ms(window.worst_event_ns) < 60);
    CHECK_FALSE(window.worst_loading);
}

TEST_CASE("a slow frame without a script event names none, not an earlier frame's")
{
    fw::EndFrame();
    fw::Take(fw::Reader::Summary);
    event("Early", fw::Phase::Execute, std::chrono::milliseconds(10));
    fw::EndFrame();
    {
        const fw::Scope phase(fw::Phase::Execute);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    fw::EndFrame();
    const fw::Window window = fw::Take(fw::Reader::Summary);
    INFO(fw::Describe(window) << ' ' << fw::DescribeWorstEvent(window));
    CHECK(window.worst_event.empty());
}

TEST_CASE("an event across presents counts in each frame for its part; a frame inside a load says so")
{
    fw::EndFrame();
    fw::Take(fw::Reader::Summary);
    storm::loading::screen_up = true;
    fw::BeginEvent("Load", fw::Phase::Script, fw::Clock::now());
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    fw::EndFrame(); // the loading screen's present
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    fw::EndFrame();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    fw::EndEvent(fw::Clock::now());
    storm::loading::screen_up = false;
    fw::EndFrame();
    const fw::Window window = fw::Take(fw::Reader::Summary);
    INFO(fw::Describe(window) << ' ' << fw::DescribeWorstEvent(window));
    REQUIRE(window.frames == 3);
    CHECK(window.worst_event == "Load");
    CHECK(ms(window.worst_event_ns) >= 40);
    CHECK(ms(window.worst_event_ns) < 55); // its part in the second frame, not since it began
    CHECK(window.worst_loading);
}
