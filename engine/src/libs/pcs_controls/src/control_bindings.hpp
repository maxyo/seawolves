#pragma once

// Key bindings per control group, controls that act on a hold of their key, tap/hold pairs and combat schemes, as
// Caribbean Legend's scripts set them (README.md). Free of the engine: tests/control_bindings runs it as it is.

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace storm::controls
{
inline constexpr int32_t kAllGroups = -2; // a binding of every group (MapControl with two arguments)
inline constexpr int32_t kNoKey = -1;
// The virtual keys of the keyboard and the mouse buttons: what can be held (the mouse's axes and wheel cannot).
inline constexpr int32_t kHoldableKeys = 256;

// What a script sees of a control the model decides; Ordinary: the key's own state.
enum class Pulse : uint8_t
{
    Ordinary,
    Inactive,
    Activated,   // one frame, value 1
    Inactivated, // the frame after
};

class ControlBindings
{
  public:
    using KeyDown = std::function<bool(int32_t key)>;

    void Resize(size_t controls)
    {
        controls_.resize(std::max(controls_.size(), controls));
        pulses_.resize(controls_.size(), 0);
    }

    // `key` kNoKey removes the group's binding.
    void Map(int32_t control, int32_t key, int32_t group)
    {
        auto &bindings = controls_.at(control).keys;
        if (key == kNoKey)
            bindings.erase(group);
        else
            bindings[group] = key;
    }

    // The key the control answers to now: none when its scheme is not the current one; its binding in the current
    // group, else its binding in every group.
    int32_t Key(int32_t control) const
    {
        const Control &c = controls_.at(control);
        if (c.scheme != 0 && c.scheme != scheme_)
            return kNoKey;
        for (const int32_t group : {group_, kAllGroups})
            if (const auto found = c.keys.find(group); found != c.keys.end())
                return found->second;
        return kNoKey;
    }

    // Each returns whether anything changed (the caller then rebuilds).
    bool SetGroup(int32_t group)
    {
        return std::exchange(group_, group) != group;
    }

    bool SetHold(int32_t control, int32_t group, bool hold)
    {
        auto &groups = controls_.at(control).hold_groups;
        return hold ? groups.insert(group).second : groups.erase(group) > 0;
    }

    // (-1, -1) forgets every pair; otherwise two different controls, `primary` firing on a tap of the key `hold`
    // fires on when held. A second pair of the same `hold` replaces the first.
    bool Pair(int32_t primary, int32_t hold)
    {
        if (primary == -1 && hold == -1)
        {
            pairs_.clear();
            return true;
        }
        const auto valid = [&](int32_t c) { return c >= 0 && static_cast<size_t>(c) < controls_.size(); };
        if (!valid(primary) || !valid(hold) || primary == hold)
            return false;
        pairs_[hold] = primary;
        return true;
    }

    void SetThreshold(int32_t milliseconds)
    {
        threshold_ms_ = milliseconds;
    }

    bool SetScheme(int32_t control, int32_t scheme)
    {
        return std::exchange(controls_.at(control).scheme, scheme) != scheme;
    }

    // The current combat scheme; a key held at the switch moves none of the schemes' controls until it is released.
    bool SetCurrentScheme(int32_t scheme, const KeyDown &down)
    {
        if (std::exchange(scheme_, scheme) == scheme)
            return false;
        held_at_switch_.clear();
        for (const Control &c : controls_)
            for (const auto &[group, key] : c.keys)
                if (key >= 0 && down(key))
                    held_at_switch_.insert(key);
        return true;
    }

    // The tap/hold resolution of the current group. A key already down waits for its release.
    void Rebuild(const KeyDown &down)
    {
        entries_.clear();
        std::fill(pulses_.begin(), pulses_.end(), 0);
        for (int32_t hold = 0; hold < static_cast<int32_t>(controls_.size()); ++hold)
        {
            const int32_t key = Key(hold);
            if (key < 0 || key >= kHoldableKeys || !HoldHere(hold))
                continue;
            int32_t tap = -1;
            if (const auto pair = pairs_.find(hold); pair != pairs_.end())
            {
                if (Key(pair->second) == key && !HoldHere(pair->second))
                    tap = pair->second;
            }
            else
            {
                // An unpaired hold control takes the one other control on its key as its tap; a key shared more
                // widely is ambiguous and stays an ordinary key.
                int taps = 0, holds = 0;
                for (int32_t other = 0; other < static_cast<int32_t>(controls_.size()); ++other)
                    if (other != hold && Key(other) == key)
                    {
                        (HoldHere(other) ? holds : taps)++;
                        tap = HoldHere(other) ? tap : other;
                    }
                if (taps > 1 || holds > 0)
                    continue;
            }
            const bool pressed = down(key);
            entries_.push_back({hold, tap, key, pressed, pressed, false, 0});
        }
    }

    // Once per frame: a key released before the threshold fires the tap control, one held to the threshold fires the
    // hold control while it is still held; each fires once per press.
    void Update(uint64_t now_ms, const KeyDown &down)
    {
        for (int &pulse : pulses_)
            pulse = pulse == kActivated ? kInactivated : 0;
        std::erase_if(held_at_switch_, [&](int32_t key) { return !down(key); });
        for (Entry &e : entries_)
        {
            const bool pressed = down(e.key);
            if (e.wait_release)
            {
                e.wait_release = pressed;
                e.down = pressed;
                continue;
            }
            if (pressed && !e.down)
            {
                e.decided = false;
                e.pressed_ms = now_ms;
            }
            if (!e.decided && pressed && now_ms - e.pressed_ms >= static_cast<uint64_t>(threshold_ms_))
            {
                e.decided = true;
                pulses_[e.hold] = kActivated;
            }
            else if (!e.decided && e.down && !pressed)
            {
                e.decided = true;
                if (e.tap >= 0)
                    pulses_[e.tap] = kActivated;
            }
            e.down = pressed;
        }
    }

    Pulse State(int32_t control) const
    {
        const Control &c = controls_.at(control);
        if (c.scheme != 0 && (c.scheme != scheme_ || held_at_switch_.contains(Key(control))))
            return Pulse::Inactive;
        const bool resolved = std::any_of(entries_.begin(), entries_.end(),
                                          [&](const Entry &e) { return e.hold == control || e.tap == control; });
        if (!resolved)
            return Pulse::Ordinary;
        return pulses_[control] == kActivated     ? Pulse::Activated
               : pulses_[control] == kInactivated ? Pulse::Inactivated
                                                  : Pulse::Inactive;
    }

  private:
    static constexpr int kActivated = 2, kInactivated = 1;

    struct Control
    {
        std::map<int32_t, int32_t> keys; // group -> key
        std::set<int32_t> hold_groups;
        int32_t scheme = 0; // 0: in every scheme
    };
    struct Entry
    {
        int32_t hold, tap, key;
        bool down, wait_release, decided;
        uint64_t pressed_ms;
    };

    bool HoldHere(int32_t control) const
    {
        const auto &groups = controls_[control].hold_groups;
        return groups.contains(group_) || groups.contains(kAllGroups);
    }

    std::vector<Control> controls_;
    std::vector<int> pulses_;
    std::map<int32_t, int32_t> pairs_; // hold -> primary
    std::vector<Entry> entries_;
    std::set<int32_t> held_at_switch_;
    int32_t group_ = kAllGroups;
    int32_t scheme_ = 1;
    int32_t threshold_ms_ = 300;
};
} // namespace storm::controls
