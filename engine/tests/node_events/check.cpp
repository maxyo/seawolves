#include "node_events.hpp"

#include <string>
#include <vector>

#include <catch2/catch.hpp>

using storm::xi::NodeEvents;

TEST_CASE("node_events.a_handler_may_release_the_queue")
{
    // the handler of the first event opens another interface: the queue is cleared while that event is raised
    NodeEvents events;
    events.Add({"later", "B", 2});
    events.Add({"opens", "A", 1});
    std::vector<std::string> raised;
    events.Run([&](const NodeEvents::Event &event) {
        if (event.node == "A")
        {
            events.Clear();
            events.Add({"queued by the new interface", "C", 3});
        }
        raised.push_back(event.name + " " + event.node); // the event is still whole (ASan)
    });
    CHECK(raised == std::vector<std::string>{"opens A", "queued by the new interface C"});
}

TEST_CASE("node_events.order_and_count")
{
    NodeEvents events;
    const uint64_t before = events.Added();
    events.Add({"first", "A", 0});
    events.Add({"second", "B", 0});
    CHECK(events.Added() == before + 2);
    std::vector<std::string> raised;
    events.Run([&](const NodeEvents::Event &event) { raised.push_back(event.name); });
    CHECK(raised == std::vector<std::string>{"second", "first"}); // the last added first, as the engine always did
}
