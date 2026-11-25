// A HUD channel's state is made only when what it is made from changed: the publisher keeps a copy of that, the
// tuple of references the state class hands out (publicationKey()).
#pragma once
#include "storm_ui/hud_state.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace storm::android
{
// The values of a publication key.
template <class... T> std::tuple<std::remove_cvref_t<T>...> changeKey(const std::tuple<T...> &key)
{
    return key;
}
// True, and kept, when `key` differs from the one kept last.
template <class Owned, class Key> bool changed(std::optional<Owned> &last, const Key &key)
{
    if (last && *last == key)
        return false;
    last.emplace(key);
    return true;
}
// The newest state of each HUD channel as the UI host was given it: a state equal to its channel's last one is no
// change; a UI host that starts later gets the newest of each, in the order of HudState's alternatives.
class HudStates
{
    std::array<std::optional<storm::ui::HudState>, std::variant_size_v<storm::ui::HudState>> last_;

  public:
    // True, and kept, when `state` differs from its channel's last one.
    bool keep(const storm::ui::HudState &state)
    {
        auto &last = last_[state.index()];
        if (last && *last == state)
            return false;
        last = state;
        return true;
    }
    template <class Give> void replay(Give &&give) const
    {
        for (const auto &state : last_)
            if (state)
                give(*state);
    }
};
// The UI's commands for the next game frame, oldest first. A command of a latest-value slot (a stick position, a
// geometry) takes the place of the slot's command still waiting; a command without a slot always queues (a held
// control's release, HoldSet: so a press and its release in one frame both reach the engine).
template <class Command> class CommandQueue
{
    std::vector<Command> commands_;
    std::map<std::string, size_t> slots_; // slot -> its command's place in commands_

  public:
    void push(Command command, const std::string &slot)
    {
        if (!slot.empty())
        {
            if (const auto found = slots_.find(slot); found != slots_.end())
            {
                commands_[found->second] = std::move(command);
                return;
            }
            slots_[slot] = commands_.size();
        }
        commands_.push_back(std::move(command));
    }
    std::vector<Command> take()
    {
        std::vector<Command> out;
        out.swap(commands_);
        slots_.clear();
        return out;
    }
};
// The player's input session, as the activity says it: an even epoch while it is resumed and has the window's focus,
// odd while not. The activity's thread moves it in order with the touches it sends; the game loop reads it at the top
// of an iteration, and what that iteration takes in acts only while the epoch is still that live one (inputCurrent).
// A tap of the instant the app goes is thus dropped, however late the game thread hears of the pause.
class InputEpoch
{
    std::atomic<uint64_t> epoch_{0};

  public:
    static bool live(uint64_t epoch)
    {
        return epoch % 2 == 0;
    }
    // The activity's thread: the input session is live (on) or not.
    void set(bool on)
    {
        const uint64_t epoch = epoch_.load(std::memory_order_relaxed);
        if (live(epoch) != on)
            epoch_.store(epoch + 1, std::memory_order_release);
    }
    uint64_t now() const
    {
        return epoch_.load(std::memory_order_acquire);
    }
};
inline InputEpoch inputEpoch;
// The input taken in at `start` (InputEpoch::now at the top of a loop iteration) may act now.
inline bool inputCurrent(uint64_t start, uint64_t now)
{
    return InputEpoch::live(start) && start == now;
}
// What the activity's thread sends the UI host (its calls and pointer samples, oldest first, each with the input epoch
// it was sent in), applied at the top of the next game loop iteration. A Step says whether it is a pointer sample
// (touch()) and has its `epoch`.
template <class Step> class UiInbox
{
    std::vector<Step> steps_;

  public:
    void push(Step step)
    {
        steps_.push_back(std::move(step));
    }
    void pushFront(Step step)
    {
        steps_.insert(steps_.begin(), std::move(step));
    }
    struct Taken
    {
        std::vector<Step> steps;
        size_t dropped = 0; // pointer samples of an earlier or a not live epoch
    };
    // What applies in an iteration of `epoch`: the calls, and the pointer samples sent in that epoch while it is live.
    // Steps sent in a later epoch wait, with everything after them, for an iteration of theirs.
    Taken take(uint64_t epoch)
    {
        Taken taken;
        auto step = steps_.begin();
        for (; step != steps_.end() && step->epoch <= epoch; ++step)
            if (!step->touch() || (step->epoch == epoch && InputEpoch::live(epoch)))
                taken.steps.push_back(std::move(*step));
            else
                ++taken.dropped;
        steps_.erase(steps_.begin(), step);
        return taken;
    }
};
} // namespace storm::android
