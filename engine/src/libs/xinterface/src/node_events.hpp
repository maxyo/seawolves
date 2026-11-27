#pragma once

// The node events an interface raises for the scripts (MSG_INTERFACE_SET_EVENT queues them, the frame raises them).

#include <cstdint>
#include <deque>
#include <functional>
#include <string>

namespace storm::xi
{
class NodeEvents
{
  public:
    struct Event
    {
        std::string name;
        std::string node;
        int32_t command = -1;
    };

    // the last added is raised first
    void Add(Event event)
    {
        events_.push_front(std::move(event));
        ++added_;
    }

    void Clear()
    {
        events_.clear();
    }

    // how many were ever added: a change says one was queued
    [[nodiscard]] uint64_t Added() const
    {
        return added_;
    }

    // Each event leaves the queue before it is raised: its handler may open another interface, which clears the queue
    // (or queue more events).
    void Run(const std::function<void(const Event &)> &raise)
    {
        while (!events_.empty())
        {
            const Event event = std::move(events_.front());
            events_.pop_front();
            raise(event);
        }
    }

  private:
    std::deque<Event> events_;
    uint64_t added_ = 0;
};
} // namespace storm::xi
