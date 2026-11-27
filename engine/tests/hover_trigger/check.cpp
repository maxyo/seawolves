#include "hover_trigger.hpp"

#include <vector>

#include <catch2/catch.hpp>

using storm::xi::HoverTrigger;

namespace
{
struct Script
{
    float delay = 0.5f;
    std::vector<std::string> events;

    HoverTrigger Trigger()
    {
        return HoverTrigger({[this] { return delay; }, [this](const std::string &n) { events.push_back("start " + n); },
                             [this](const std::string &n) { events.push_back("end " + n); },
                             [this] { events.push_back("cancel"); }});
    }
};
} // namespace

TEST_CASE("hover_trigger.fires_once_after_the_delay")
{
    Script script;
    auto trigger = script.Trigger();
    trigger.Hover("BUTTON");
    CHECK(script.events == std::vector<std::string>{"start BUTTON"});
    trigger.Advance(0.3f);
    CHECK(trigger.Progress() == Approx(0.6f));
    CHECK(script.events.size() == 1);
    trigger.Advance(0.3f);
    trigger.Advance(5.0f);
    trigger.Hover("BUTTON");
    CHECK(script.events == std::vector<std::string>{"start BUTTON", "end BUTTON"});
    CHECK(trigger.Progress() == 0.0f);
}

TEST_CASE("hover_trigger.leaving_and_switching_cancel")
{
    Script script;
    auto trigger = script.Trigger();
    trigger.Hover("A");
    trigger.Advance(0.1f);
    trigger.Hover("B");
    trigger.Advance(0.1f);
    trigger.Hover("");
    trigger.Advance(1.0f);
    trigger.Hover("");
    CHECK(script.events == std::vector<std::string>{"start A", "cancel", "start B", "cancel"});
}

TEST_CASE("hover_trigger.script_delays")
{
    Script script;
    script.delay = 0.0f; // at once on the next frame
    auto trigger = script.Trigger();
    trigger.Hover("A");
    trigger.Advance(0.0f);
    CHECK(script.events == std::vector<std::string>{"start A", "end A"});

    script.delay = -1.0f; // hints off: announced, never fires
    trigger.Hover("B");
    trigger.Advance(100.0f);
    CHECK(script.events.back() == "start B");
    CHECK(trigger.Progress() == 0.0f);
}
