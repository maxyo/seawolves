#pragma once
#include "android_gameplay.hpp"
#include "storm_ui/hud_command.h"
#include <array>
#include <functional>
#include <unordered_map>

namespace storm::android
{
// A frame-local snapshot of the few controls used by combat. Copy availability
// bits instead of hundreds of names; later script queries may replace the
// controls table, so string_views into its descriptors must not be retained.
class CombatControlAvailability
{
    static constexpr std::array names = {
        std::string_view("ChrFightMode"),    std::string_view("BOAL_UsePotion"),  std::string_view("ChrAction"),
        std::string_view("ChrCamSpecMode"),  std::string_view("ChrAttackForce2"), std::string_view("ChrAttackForce"),
        std::string_view("ChrAttackFast2"),  std::string_view("ChrAttackFast"),   std::string_view("ChrAttackBreak"),
        std::string_view("ChrAttackBreak2"), std::string_view("ChrParry"),        std::string_view("ChrParry2"),
        std::string_view("ChrAttackFient"),  std::string_view("ChrAttackFient2"), std::string_view("ChrAttackRound"),
        std::string_view("ChrAttackRound2"), std::string_view("ChrFire"),         std::string_view("ChrBlock"),
        std::string_view("ChrBlock2")};
    std::array<bool, names.size()> available_{};

  public:
    void allow(std::string_view name)
    {
        const auto found = std::find(names.begin(), names.end(), name);
        if (found != names.end())
            available_[found - names.begin()] = true;
    }
    bool contains(std::string_view name) const
    {
        const auto found = std::find(names.begin(), names.end(), name);
        return found != names.end() && available_[found - names.begin()];
    }
};
// Native FindDialogCharacter returns a script index, or -1 when unavailable.
inline bool validDialogTarget(int64_t target)
{
    return target >= 0 && target <= 0x7fffffff;
}
inline std::atomic<uint64_t> combatCancelEpoch{0};
// JNI producer safe: no container, engine pointer or script access here.
inline void cancelAndroidCombatInput()
{
    combatCancelEpoch.fetch_add(1, std::memory_order_release);
}
struct CombatBinding
{
    std::string action, control;
    bool held = false;
    // The native target the action acts on (interact: the dialog character's index; -1 none).
    // Another target is another availability: a tap aimed at the old one must not reach it.
    int64_t target = -1;
    bool operator==(const CombatBinding &) const = default;
};
// Pure semantic state; numeric phases intentionally independent of CONTROLS.
// 0 inactive, 1 active, 2 released, 3 activated match the engine contract.
//
// Two fences guard a UI command, at different granularity:
// - the activity generation names the input session: Activity, scene, native
//   player and stance. Any change invalidates every command of the old session.
// - each action's generation names one continuous availability of that action
//   with one native binding. When an action appears, disappears or is remapped,
//   only its own queued and in-flight commands die; the other buttons keep
//   working. ("interact" comes and goes as passers-by enter the dialog cone.)
// The gameplay revision is published for the HUD to pair this state with the
// gameplay state it came from; it is not a fence, because it also moves for
// state the combat actions do not depend on (movement generation, language).
class CombatInput
{
    struct Bound
    {
        CombatBinding binding;
        uint64_t generation = 0;
        bool operator==(const Bound &) const = default;
    };
    struct Request
    {
        uint64_t generation, action, cancel;
        std::string name, phase;
    };
    struct Virtual
    {
        bool held = false, previous = false, pulse = false;
        int state = 0;
    };
    inline static std::atomic<uint64_t> nextGeneration_{0};
    uint64_t revision_ = 0, generation_ = 0, cancel_ = 0;
    std::string scene_, player_, mode_ = "unknown";
    std::vector<Bound> bindings_;
    std::deque<Request> pending_;
    std::unordered_map<std::string, Virtual> virtual_;
    bool fresh_ = false;
    static uint64_t nextGeneration()
    {
        return nextGeneration_.fetch_add(1, std::memory_order_relaxed) + 1;
    }
    const Bound *binding(std::string_view action) const
    {
        for (const auto &b : bindings_)
            if (b.binding.action == action)
                return &b;
        return nullptr;
    }
    static void release(Virtual &value)
    {
        value.state = value.held || value.previous || value.pulse ? 2 : 0;
        value.held = value.previous = value.pulse = false;
    }
    bool synchronizeCancel()
    {
        const auto epoch = combatCancelEpoch.load(std::memory_order_acquire);
        if (epoch == cancel_)
            return false;
        cancel_ = epoch;
        clear();
        return true;
    }

  public:
    // Installed by the engine adapter. Called only on the game thread.
    std::function<bool()> guard;
    void clear()
    {
        pending_.clear();
        for (auto &[name, value] : virtual_)
            release(value);
        fresh_ = false;
    }
    void resetActivity()
    {
        generation_ = nextGeneration();
        clear();
    }
    void update(uint64_t revision, std::string scene, std::string player, std::string mode,
                std::vector<CombatBinding> bindings)
    {
        synchronizeCancel();
        revision_ = revision;
        if (scene != scene_ || player != player_ || mode != mode_)
        {
            resetActivity();
            bindings_.clear();
            scene_ = std::move(scene);
            player_ = std::move(player);
            mode_ = std::move(mode);
        }
        std::vector<Bound> next;
        for (auto &b : bindings)
        {
            const auto *old = binding(b.action);
            const auto generation = old && old->binding == b ? old->generation : nextGeneration();
            next.push_back({std::move(b), generation});
        }
        for (const auto &old : bindings_)
        {
            const auto kept = std::find_if(next.begin(), next.end(), [&](const Bound &b) {
                return b.binding.action == old.binding.action && b.generation == old.generation;
            });
            if (kept != next.end())
                continue;
            // This availability ended: its queued commands must not fire on a
            // later one, and a hold or pulse on its native control lets go.
            std::erase_if(pending_, [&](const Request &r) { return r.name == old.binding.action; });
            const bool stillBound = std::any_of(
                next.begin(), next.end(), [&](const Bound &b) { return b.binding.control == old.binding.control; });
            if (const auto v = virtual_.find(old.binding.control); v != virtual_.end() && !stillBound)
                release(v->second);
        }
        bindings_ = std::move(next);
    }
    uint64_t generation() const
    {
        return generation_;
    }
    // 0 when the action is not offered now.
    uint64_t actionGeneration(std::string_view action) const
    {
        const auto *b = binding(action);
        return b ? b->generation : 0;
    }
    uint64_t revision() const
    {
        return revision_;
    }
    const std::string &player() const
    {
        return player_;
    }
    bool offer(const storm::ui::hud::CombatAction &tap,
               uint64_t enqueueEpoch = combatCancelEpoch.load(std::memory_order_acquire))
    {
        synchronizeCancel();
        // Fence the JNI drain as well as our own queue: cancellation may occur
        // after Java returned a command but before it reaches this mailbox.
        if (enqueueEpoch != cancel_ || tap.activity != generation_ || !generation_)
            return false;
        if (tap.action == "all")
        {
            if (tap.phase != "cancel" || tap.generation)
                return false;
            clear();
            return true;
        }
        const auto *b = binding(tap.action);
        if (!b || tap.generation != b->generation || mode_ == "unknown" || player_.empty())
            return false;
        const auto &bound = b->binding;
        if ((bound.held && tap.phase != "down" && tap.phase != "up") || (!bound.held && tap.phase != "tap"))
            return false;
        if (tap.phase == "up")
        {
            std::erase_if(pending_, [&](const auto &r) { return r.name == bound.action; });
            auto &v = virtual_[bound.control];
            v.held = false;
            v.state = v.previous ? 2 : 0;
            return true;
        }
        // One waiting tap per action: taps faster than the frames pulse them do not pile up into swings after the
        // player stopped.
        if (std::any_of(pending_.begin(), pending_.end(), [&](const Request &r) { return r.name == tap.action; }))
            return false;
        pending_.push_back({tap.activity, tap.generation, cancel_, tap.action, tap.phase});
        return true;
    }
    // Called at Controls::Update, after the old frame's Player has consumed its
    // input and before Core::ProcessControls emits the next frame's events.
    void advance()
    {
        synchronizeCancel();
        if (!guard || !guard())
        {
            clear();
            return;
        }
        for (auto &[name, v] : virtual_)
        {
            v.previous = v.held || v.pulse;
            v.pulse = false;
        }
        bool tapped = false;
        while (!pending_.empty())
        {
            const auto &r = pending_.front();
            const auto *b = binding(r.name);
            if (r.generation != generation_ || r.cancel != cancel_ || !b || b->generation != r.action)
            {
                pending_.pop_front();
                continue;
            }
            // Player executes one attack by priority per frame. Preserve tap
            // order and one inactive frame between repeated pulses.
            auto &v = virtual_[b->binding.control];
            if (r.phase == "tap" && (tapped || v.previous))
                break;
            if (r.phase == "tap")
            {
                v.pulse = true;
                tapped = true;
            }
            else
                v.held = true;
            pending_.pop_front();
        }
        for (auto &[name, v] : virtual_)
        {
            const bool active = v.held || v.pulse;
            v.state = active ? (v.previous ? 1 : 3) : (v.previous ? 2 : 0);
        }
        fresh_ = true;
    }
    // Merge only named output states. No PC key maps, arbitrary script events,
    // or direct Character calls. Physical activation/hold keeps priority.
    int merge(std::string_view control, int physical)
    {
        synchronizeCancel();
        auto it = virtual_.find(std::string(control));
        if (it == virtual_.end())
            return physical;
        if ((fresh_ || it->second.held) && (!guard || !guard()))
            clear();
        const int state = it->second.state;
        if (physical == 1 || physical == 3)
            return physical;
        return state == 0 ? physical : state;
    }
    // The "combat" channel without what the engine adds to it (the talk target, the pistol).
    storm::ui::hud::Combat hud() const
    {
        storm::ui::hud::Combat out;
        out.known = true;
        out.gameplay_revision = revision_;
        out.activity = generation_;
        out.player = player_;
        out.mode = mode_;
        for (const auto &bound : bindings_)
            out.actions[bound.binding.action] = bound.generation;
        return out;
    }
    // What hud() reads.
    auto publicationKey() const
    {
        return std::tie(revision_, generation_, player_, mode_, bindings_);
    }
};
inline CombatInput combatInput;
} // namespace storm::android
