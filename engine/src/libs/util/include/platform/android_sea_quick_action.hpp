#pragma once
#include "android_gameplay.hpp"
#include "storm_ui/hud_command.h"
#include "storm_ui/hud_state.h"
#include <tuple>

namespace storm::android
{
// The quick action ILogAndActions offers at sea (requirement U1). The complete target
// identity stays native; the touch HUD receives a revision that fences that tuple.
// The script offers one action at a time, by priority (BI_SetSeaState: Board, LandTroops,
// Reload, Moor, Map). `map`: the script would also let the squadron go to the world map now
// (its bMapEnter), so the HUD offers Map beside the action it shows (moor and
// map together whenever both are possible); `mapLabel` is the script's caption of Map.
struct SeaQuickActionFrame
{
    uint64_t host = 0;
    bool sea = false, available = false, complete = false;
    std::string owner, action, label, target;
    bool map = false;
    std::string mapLabel;
    bool operator==(const SeaQuickActionFrame &) const = default;
};
inline bool knownSeaQuickAction(std::string_view action)
{
    return action == "Map" || action == "Moor" || action == "Board" || action == "LandTroops" || action == "Reload";
}
// Game-thread owned.
//
class SeaQuickActionState
{
    SeaQuickActionFrame frame_;
    uint64_t revision_ = 1;
    bool pending_ = false;
    std::string pendingAction_;

  public:
    const SeaQuickActionFrame &frame() const
    {
        return frame_;
    }
    uint64_t revision() const
    {
        return revision_;
    }
    bool valid() const
    {
        return frame_.sea && frame_.host && frame_.available && frame_.complete && knownSeaQuickAction(frame_.action);
    }
    void update(SeaQuickActionFrame frame)
    {
        if (frame == frame_)
            return;
        const bool offered = valid();
        frame_ = std::move(frame);
        // Nothing offered before or now: no tap to fence, no new state (behind the main menu the sea's frame
        // and the blocked one alternate every frame).
        if (!offered && !valid())
            return;
        // Another action, target or owner: what was drawn and tapped belongs to the old one.
        ++revision_;
        pending_ = false;
    }
    // A tap on the action the HUD showed for this revision (or on Map beside it): accepted once.
    bool offer(const storm::ui::hud::SeaQuickTap &tap)
    {
        if (tap.revision != revision_ || !valid() ||
            !(tap.action == frame_.action || (tap.action == "Map" && frame_.map)) || pending_)
            return false;
        pending_ = true;
        pendingAction_ = tap.action;
        return true;
    }
    std::optional<std::string> consume()
    {
        if (!pending_)
            return {};
        pending_ = false;
        if (!valid())
            return {};
        // The same action offered again after this is a new one to tap: an echo of this tap is stale.
        ++revision_;
        return std::move(pendingAction_);
    }
    // What hud() reads: an offered frame changes only with a new revision.
    auto publicationKey() const
    {
        return std::tie(revision_);
    }
    storm::ui::hud::SeaQuick hud() const
    {
        const bool offered = valid();
        return {true,
                revision_,
                offered,
                offered ? frame_.action : std::string(),
                offered ? frame_.label : std::string(),
                offered && frame_.map,
                offered && frame_.map ? frame_.mapLabel : std::string()};
    }
};
inline SeaQuickActionState seaQuickAction;
// The engine loop says whether the player can act at all (focus, lifecycle, no screen, no loading).
inline bool seaQuickLifecycleReady = false;
} // namespace storm::android
