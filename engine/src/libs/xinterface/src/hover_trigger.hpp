#pragma once

// Hover triggers: nodes with `UseTrigger = 1` under the mouse, timed by the TRIGGER node.
// Entering such a node announces it (Event_NodeStartTrigger: the scripts draw its frame) and starts the timer, whose
// length the scripts give (Event_GetTriggerDelay, seconds; negative: never fires; the TRIGGER node's `time` when they
// do not answer). When it runs out the node fires once (Event_NodeEndTrigger: the scripts open its info window).
// Leaving the node, or entering another, cancels it (Event_NullTrigger). Once it has fired, moving to another line or
// column of a node that has them (a TABLE's UseLineTrigger, UseColTrigger) announces the new part (the scripts move
// the frame and the info window there: Event_ChangeTableTriggerFrame).

#include <cstdint>
#include <functional>
#include <string>

namespace storm::xi
{
// a line and a column of the hovered node (-1: none) and their rectangle
struct HoverPart
{
    int32_t line = -1, column = -1;
    int32_t left = 0, top = 0, right = 0, bottom = 0;
};

class HoverTrigger
{
  public:
    using Part = HoverPart;

    struct Events
    {
        std::function<float()> delay;
        std::function<void(const std::string &)> start;
        std::function<void(const std::string &)> end;
        std::function<void()> cancel;
        std::function<void(const std::string &, const Part &)> part;
    };

    explicit HoverTrigger(Events events) : events_(std::move(events))
    {
    }

    // `hovered`: the trigger node under the mouse, "" for none
    void Hover(const std::string &hovered, const Part &part = Part{})
    {
        if (hovered == node_)
        {
            if (!running_ && !node_.empty())
                MoveTo(part);
            return;
        }
        part_ = {};
        if (!node_.empty())
            events_.cancel();
        node_ = hovered;
        running_ = !node_.empty();
        progress_ = 0.0f;
        if (!running_)
            return;
        delay_ = events_.delay();
        events_.start(node_);
    }

    void Advance(float seconds)
    {
        if (!running_ || delay_ < 0.0f)
            return;
        progress_ = delay_ > 0.0f ? progress_ + seconds / delay_ : 1.0f;
        if (progress_ < 1.0f)
            return;
        running_ = false;
        progress_ = 0.0f;
        events_.end(node_);
    }

    // 0 while nothing runs, else how much of the delay has passed (the TRIGGER node draws it)
    [[nodiscard]] float Progress() const
    {
        return running_ ? progress_ : 0.0f;
    }

    [[nodiscard]] const std::string &Node() const
    {
        return node_;
    }

  private:
    void MoveTo(const Part &part)
    {
        const bool moved =
            (part.line >= 0 && part.line != part_.line) || (part.column >= 0 && part.column != part_.column);
        part_ = part;
        if (moved)
            events_.part(node_, part);
    }

    Events events_;
    Part part_;
    std::string node_;
    bool running_ = false;
    float delay_ = 0.0f;
    float progress_ = 0.0f;
};
} // namespace storm::xi
