#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "jni_text.hpp"
#include "storm_ui/hud_command.h"
#include "storm_ui/hud_state.h"

namespace storm::android
{
// A reading of the game to the millionth: what the HUD redraws for (a change below it is no change).
inline double hudReading(double value)
{
    return std::round(value * 1e6) / 1e6;
}
// Text of the game for the touch HUD: UTF-8 (what its renderer takes) without C0/C1 controls or DEL.
inline bool hudTextValid(std::string_view value)
{
    for (size_t i = 0; i < value.size();)
    {
        const auto [c, length] = jni_text::DecodeUtf8(value, i);
        if (!length || c < 0x20 || (c >= 0x7f && c < 0xa0))
            return false;
        i += length;
    }
    return true;
}
// A step in a HUD menu (land commands, sea orders) as the menu takes it: the HUD's command (MenuStep) with the
// menu's scene it was taken in.
struct HudMenuRequest
{
    uint64_t scene = 0, revision = 0;
    std::string verb, id;
};
// Text of the game's scripts for a HUD channel: the UI renders UTF-8 only. Text that is not is left out ("") and
// `defect` says so (the caller logs it).
inline std::string hudScriptText(std::string_view text, const char *&defect)
{
    if (jni_text::IsUtf8(text))
        return std::string(text);
    defect = "a text that is not UTF-8 is left out";
    return {};
}
// A caption of the game for the HUD (a date, a place, a ship's model, a command's note): without the spaces around
// it, UTF-8 text without controls; anything else (or nothing left) is left out and `defect` says so.
inline std::string hudCaption(std::string_view text, const char *&defect)
{
    while (!text.empty() && text.front() == ' ')
        text.remove_prefix(1);
    while (!text.empty() && text.back() == ' ')
        text.remove_suffix(1);
    if (hudTextValid(text))
        return std::string(text);
    defect = "a caption that is not UTF-8 text is left out";
    return {};
}
// A FaceId the HUD builds a portrait's texture path from: letters, digits and '_' only; anything else is no face.
inline std::string hudFaceId(std::string_view id, const char *&defect)
{
    if (std::all_of(id.begin(), id.end(), [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        }))
        return std::string(id);
    defect = "a FaceId that is not a portrait id is left out";
    return {};
}
// A text of a HUD menu frame from the scripts: invalid or (when `required`) blank text is shown as "…" and the menu
// is then not complete; `defect` says so for the publisher's log.
inline void checkMenuText(std::string &text, bool required, bool &complete, const char *&defect)
{
    if (hudTextValid(text) && (!required || text.find_first_not_of(' ') != std::string::npos))
        return;
    text = "\xE2\x80\xA6";
    complete = false;
    defect = "invalid or empty text shown as \xE2\x80\xA6";
}
// An item of a HUD menu frame as the channel shows it.
template <class Item> storm::ui::hud::MenuItem hudMenuItem(const Item &item, std::string picture)
{
    return {item.id,
            item.kind,
            item.label,
            item.source,
            std::move(picture),
            item.enabled,
            hudReading(std::isfinite(item.cooldown) ? std::clamp(item.cooldown, 0.0, 1.0) : 0.0)};
}
// Whether two menu frames differ only in the cooldown animation of their items: such a change must
// not invalidate the taps in flight (an enabled change still does; the native confirm samples the
// cooldown afresh).
template <class Frame> bool sameMenuButCooldown(Frame a, Frame b)
{
    for (auto *frame : {&a, &b})
        for (auto &item : frame->items)
            item.cooldown = item.enabled ? 1 : 0;
    return a == b;
}
// Game-thread owned; no script/entity pointers cross threads.
// Script control groups can lag entity Execute by a frame. For the two ordinary
// land groups, the verified Player owns stance; actual group bindings still own
// capabilities. Modal, scene and player guards are resolved before this policy.
struct GameplayContext
{
    std::string_view group;
    // blocked: a command menu (ComState) or a video takes the controls; video says which.
    bool blocked = false, video = false, sea = false, land = false, naval = false, worldmap = false;
    bool location = false; // a land location is loaded (land: its interface is started too)
    std::optional<bool> nativeFight;
};
inline std::string_view gameplayMode(const GameplayContext &context)
{
    if (context.blocked)
        return "unknown";
    if (context.worldmap)
        return context.group == "WorldMapControls" ? "worldmap" : "unknown";
    if (context.sea)
        return context.group == "Sailing1Pers" || context.group == "Sailing3Pers"
                   ? (context.naval ? "naval" : "sailing")
                   : "unknown";
    if (!context.land || !context.nativeFight.has_value())
        return "unknown";
    if (context.group == "PrimaryLand" || context.group == "FightModeControls")
        return *context.nativeFight ? "melee" : "explore";
    return "unknown";
}
// Why gameplayMode is "unknown" (GameplaySnapshot::blocked): the touch UI decides from it
// whether the screen shows anything the player can use (storm::ui Host::WantsTouch).
inline std::string_view gameplayBlocked(const GameplayContext &context)
{
    if (context.blocked)
        return context.video ? "video" : "commands";
    if (context.worldmap)
        return context.group == "WorldMapControls" ? "" : "group";
    if (context.sea)
        return context.group == "Sailing1Pers" || context.group == "Sailing3Pers" ? "" : "group";
    if (!context.land)
        return context.location ? "no-land-interface" : "no-scene";
    if (!context.nativeFight.has_value())
        return "no-player";
    if (context.group == "PrimaryLand" || context.group == "FightModeControls")
        return "";
    return "group";
}
struct GameplaySnapshot
{
    std::string mode = "unknown", language, group, scene;
    std::vector<std::string> capabilities;
    // Stable only for one native land Player in one loaded scene. Android may
    // retain locomotion across stance revisions only when this nonempty token matches.
    std::string inputOwner;
    bool analogMovement = false;
    uint64_t movementGeneration = 0;
    // Mode "unknown" only: why. starting (the engine or its scripts not up yet), paused (the
    // app not in front), loading (the loading screen), transition (a fader is up: a scene change,
    // or the fade in after a load), interface (one open or launched),
    // dialogue, cutscene (the hero is an actor), video, commands (a command menu), no-scene
    // (no land, sea or world map yet), no-land-interface (a location whose land interface is not
    // started), no-player, group (a control group no mode knows),
    // error (the scripts could not be read).
    std::string blocked;
    bool operator==(const GameplaySnapshot &) const = default;
};
// An active ability as a touch HUD shows it, at sea (android_ship_abilities.hpp) and on land
// (battle_interface land_status.hpp): all of it read from the scripts' own state.
//   id     the perk (ChrPerksList) and BattleInterface / land interface command name
//   label  the game's own name of it (the command's note: AbilityDescribe.txt)
//   state  ready (the scripts enable its command now), active (its effect runs), recovery (it
//          was used and its delay runs), blocked (owned, not usable now)
//   fill   active: what is left of the effect; recovery: how far it has recovered (0…1)
//   left   active / recovery: whole seconds the scripts still count (perks.c procChrPerkDelay)
// The ship's battle abilities a touch HUD offers: the perk commands of the scripts' battle interface
// (BattleInterface.Commands.<X>, launched as "BI_<X>").
inline constexpr std::array<std::string_view, 4> ShipAbilityNames{"ImmediateReload", "LightRepair", "InstantRepair",
                                                                  "Turn180"};
inline bool shipAbilityControl(std::string_view control)
{
    return control.starts_with("BI_") &&
           std::find(ShipAbilityNames.begin(), ShipAbilityNames.end(), control.substr(3)) != ShipAbilityNames.end();
}
struct BattleAbility
{
    enum State
    {
        Ready,
        Active,
        Recovery,
        Blocked
    };
    std::string id, label;
    State state = Blocked;
    double fill = 0.0;
    int left = 0;
    bool operator==(const BattleAbility &) const = default;
};
// The state of a perk ability from the scripts' counters (perks.c ActivateCharacterPerk sets
// perks.list.<X>.active and .delay, procChrPerkDelay counts both down once a second and deletes them at 0): the
// effect runs while .active exists, the perk recovers while only .delay does. The fill is the share of ChrPerksList
// TimeDuration / TimeDelay, the values the game counts from; none without them. Absent counters are std::nullopt.
// `enabled` is the scripts' own verdict on the command (its .enable), asked only when neither counter runs.
inline void battleAbilityTiming(BattleAbility &ability, std::optional<double> active, std::optional<double> delay,
                                std::optional<double> duration, std::optional<double> cooldown, bool enabled)
{
    const auto seconds = [](double value) { return static_cast<int>(std::ceil(std::clamp(value, 0.0, 86400.0))); };
    ability.fill = 0.0;
    ability.left = 0;
    if (active && *active > 0)
    {
        ability.state = BattleAbility::Active;
        if (duration && *duration > 0)
            ability.fill = std::clamp(*active / *duration, 0.0, 1.0); // what is left of the effect
        ability.left = seconds(*active);
    }
    else if (delay && *delay > 0)
    {
        ability.state = BattleAbility::Recovery;
        if (cooldown && *cooldown > 0)
            ability.fill = std::clamp(1.0 - *delay / *cooldown, 0.0, 1.0); // how far it has recovered
        ability.left = seconds(*delay);
    }
    else
        ability.state = enabled ? BattleAbility::Ready : BattleAbility::Blocked;
    ability.fill = std::round(ability.fill * 100.0) / 100.0;
}
inline std::vector<storm::ui::hud::Ability> hudAbilities(const std::vector<BattleAbility> &abilities)
{
    static constexpr const char *states[] = {"ready", "active", "recovery", "blocked"};
    std::vector<storm::ui::hud::Ability> out;
    for (const auto &ability : abilities)
        out.push_back({ability.id, ability.label, states[ability.state], ability.fill, ability.left});
    return out;
}
class GameplayState
{
    struct Action
    {
        uint64_t revision;
        std::string control;
    };
    // Survives SDL_main restart in the same process/Activity.
    inline static std::atomic<uint64_t> nextRevision_{0};
    GameplaySnapshot snapshot_;
    uint64_t revision_ = 0;
    std::deque<Action> pending_;

  public:
    bool update(GameplaySnapshot snapshot)
    {
        if (revision_ && snapshot_ == snapshot)
            return false;
        snapshot_ = std::move(snapshot);
        revision_ = nextRevision_.fetch_add(1, std::memory_order_relaxed) + 1;
        pending_.clear();
        return true;
    }
    uint64_t revision() const
    {
        return revision_;
    }
    const GameplaySnapshot &snapshot() const
    {
        return snapshot_;
    }
    bool allows(std::string_view control) const
    {
        return std::find(snapshot_.capabilities.begin(), snapshot_.capabilities.end(), control) !=
               snapshot_.capabilities.end();
    }
    storm::ui::hud::Gameplay hud() const
    {
        storm::ui::hud::Gameplay out;
        out.revision = revision_;
        out.mode = snapshot_.mode;
        out.blocked = snapshot_.blocked;
        out.language = snapshot_.language;
        out.capabilities.insert(snapshot_.capabilities.begin(), snapshot_.capabilities.end());
        if (out.mode == "explore" || out.mode == "melee")
        {
            out.input_owner = snapshot_.inputOwner;
            out.analog_movement = snapshot_.analogMovement && !out.input_owner.empty() && snapshot_.movementGeneration;
            out.movement_generation = out.analog_movement ? snapshot_.movementGeneration : 0;
        }
        return out;
    }
    // A control the HUD tapped for this gameplay revision: queued for the game frame. Only controls the scripts'
    // Control Activation handler takes are activated this way; held and polled ones go through HeldControls.
    bool offer(const storm::ui::hud::GameplayControl &tap)
    {
        static constexpr std::string_view kActivated[] =
            {"Ship_Fire",   "Sea_CameraSwitch", "hk_charge1",  "hk_charge2",
             "hk_charge3",  "hk_charge4",       "TimeScaleX1", "TimeScaleX2",
             "TimeScaleX4", "TimeScaleX8",      "TelescopeIn", "TelescopeOut"}; // the spyglass: raise, lower (main.cpp)
        if (tap.revision != revision_ ||
            (std::find(std::begin(kActivated), std::end(kActivated), tap.control) == std::end(kActivated) &&
             !shipAbilityControl(tap.control)) ||
            !allows(tap.control))
            return false;
        pending_.push_back({tap.revision, tap.control});
        return true;
    }
    // The next tap of this revision; a new revision drops the queue (update).
    std::optional<std::string> take()
    {
        if (pending_.empty())
            return std::nullopt;
        auto action = std::move(pending_.front());
        pending_.pop_front();
        return action.control;
    }
};
} // namespace storm::android
