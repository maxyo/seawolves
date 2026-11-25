#pragma once
#include "android_gameplay.hpp"
#include "storm_ui/hud_command.h"
#include "storm_ui/hud_state.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

namespace storm::android
{
struct LandCommandItem
{
    std::string id, kind, label, source;
    // Identity is native-only and includes the complete command/target tuple.
    std::string identity;
    bool enabled = true;
    double cooldown = 1;
    // The picture the game draws for the item: "<texture file stem>:<cell>" in lower case (a travel
    // place is a cell of battle_interface\LandTarget_SLB: what the place is), "" = not reported.
    std::string picture;
    bool operator==(const LandCommandItem &) const = default;
};
struct LandCommandsFrame
{
    bool available = false, active = false, complete = true;
    std::string language, stage = "commands", pendingLabel, selectedItemId;
    std::string ownerIdentity; // Actual native command-list instance; never an executable Android token.
    int mode = 0;
    std::vector<LandCommandItem> items;
    // The script events of the root commands the menu would offer now (BLI_SetPossibleCommands'
    // enabled Commands), open or not: the touch HUD shows a button per group before the menu opens.
    std::vector<std::string> offered;
    // The travel stage only: the place of the town's table the hero stands in, which the game leaves out of
    // the list (its picture as LandCommandItem::picture, its name as the game's note); "" = none.
    std::string herePicture, hereLabel;
    bool operator==(const LandCommandsFrame &) const = default;
};
// Missing presentation support is not a gameplay prohibition. The original
// native menu may have been opened without any Android ownership at all.
enum class LandCommandSource
{
    Missing,
    Forbidden,
    Ready
};
class LandCommandsState
{
    inline static std::atomic<uint64_t> nextEpoch_{0};
    LandCommandsFrame frame_;
    const char *defect_ = nullptr;
    struct Receipt
    {
        uint64_t serial = 0;
        HudMenuRequest request;
        bool accepted = false;
        std::string reason;
    } receipt_;
    std::deque<Receipt> receipts_;
    std::string sceneKey_;
    uint64_t scene_ = 0, revision_ = 1;
    // Whether the stock command list draws is not decided here: while the touch HUD presents the
    // land HUD it never draws (android_hud_presenter.hpp). This state only carries the menu and
    // the authority of the sheet's taps (its revision).
    std::deque<HudMenuRequest> pending_;

  public:
    const LandCommandsFrame &frame() const
    {
        return frame_;
    }
    // What the last update cut or replaced in the script's frame, or nullptr: the publisher
    // logs it (android_ui.hpp, once per reason).
    const char *defect() const
    {
        return defect_;
    }
    // A receipt records native dispatch, never that the script began a reload.
    void result(const HudMenuRequest &request, bool accepted, const std::string &reason)
    {
        if (request.verb != "activateItem")
            return;
        // A duplicate stale enqueue may not downgrade an accepted dispatch of this
        // exact lease. Another tap's receipt must not erase it before the UI reads it.
        for (const auto &old : receipts_)
            if (old.accepted && old.request.revision == request.revision && old.request.id == request.id)
                return;
        ++receipt_.serial;
        receipt_.request = request;
        receipt_.accepted = accepted;
        receipt_.reason = reason;
        receipts_.push_back(receipt_);
        if (receipts_.size() > 8)
        {
            // Keep the newest accepted dispatch even through a burst of unrelated
            // refusals. Only a later accepted action may retire that final answer.
            const auto newestAccepted =
                std::find_if(receipts_.rbegin(), receipts_.rend(), [](const auto &r) { return r.accepted; });
            auto drop = receipts_.begin();
            if (newestAccepted != receipts_.rend() && &*drop == &*newestAccepted)
                ++drop;
            receipts_.erase(drop);
        }
    }
    uint64_t sceneEpoch() const
    {
        return scene_;
    }
    uint64_t revision() const
    {
        return revision_;
    }
    // A new revision: every tap made for the old one is stale (also after an executed one).
    void invalidate()
    {
        ++revision_;
        for (const auto &request : pending_)
            result(request, false, "invalidated-before-dispatch");
        pending_.clear();
    }
    void update(std::string sceneKey, LandCommandsFrame frame)
    {
        defect_ = nullptr;
        checkMenuText(frame.language, false, frame.complete, defect_);
        checkMenuText(frame.pendingLabel, false, frame.complete, defect_);
        for (auto &item : frame.items)
        {
            checkMenuText(item.label, true, frame.complete, defect_);
            checkMenuText(item.source, false, frame.complete, defect_);
            checkMenuText(item.picture, false, frame.complete, defect_);
        }
        for (auto &event : frame.offered)
            checkMenuText(event, false, frame.complete, defect_);
        if (!frame.herePicture.empty())
        {
            checkMenuText(frame.herePicture, false, frame.complete, defect_);
            checkMenuText(frame.hereLabel, true, frame.complete, defect_);
        }
        if (sceneKey_ != sceneKey || !scene_)
        {
            sceneKey_ = std::move(sceneKey);
            scene_ = nextEpoch_.fetch_add(1, std::memory_order_relaxed) + 1;
            invalidate();
        }
        if (!sameMenuButCooldown(frame, frame_))
            invalidate();
        frame_ = std::move(frame);
    }
    bool releaseUnavailableSource(LandCommandSource source)
    {
        if (source == LandCommandSource::Ready)
            return false;
        // Revoke all Android authority on disappearance, including an in-flight
        // visual handoff, without closing an unowned/stock command tree.
        update({}, {});
        return source == LandCommandSource::Forbidden;
    }
    bool offer(const storm::ui::hud::MenuStep &step)
    {
        const HudMenuRequest request{scene_, step.revision, step.verb, step.id};
        // Every refused activation gets its receipt.
        const auto refuse = [&](const char *reason) {
            result(request, false, reason);
            return false;
        };
        if (step.revision != revision_)
            return refuse("stale-at-enqueue");
        if (!frame_.available)
            return refuse("unavailable-at-enqueue");
        if (request.verb != "open" && request.verb != "selectItem" && request.verb != "activateItem" &&
            request.verb != "back" && request.verb != "close")
            return false;
        if ((request.verb == "back" || request.verb == "close") && !request.id.empty())
            return false;
        if (request.verb != "open" && !frame_.active)
            return refuse("menu-closed-at-enqueue");
        if (request.verb == "open" && frame_.active)
            return false;
        // "open" enters only a root command offered now; its id is a script event.
        if (request.verb == "open" && !request.id.empty() &&
            std::find(frame_.offered.begin(), frame_.offered.end(), request.id) == frame_.offered.end())
            return false;
        if (request.verb == "selectItem" || request.verb == "activateItem")
        {
            if (!frame_.complete)
                return refuse("incomplete-at-enqueue");
            if (std::none_of(frame_.items.begin(), frame_.items.end(), [&](const auto &item) {
                    return item.id == request.id &&
                           (request.verb != "activateItem" || (item.enabled && frame_.selectedItemId == request.id));
                }))
                return refuse("selection-changed-at-enqueue");
        }
        if (!pending_.empty())
        {
            const auto &pending = pending_.front();
            if (request.verb == pending.verb && request.revision == pending.revision && request.id == pending.id)
                return false;
            return refuse("another-command-pending");
        }
        pending_.push_back(request);
        return true;
    }
    // The step for this revision of the menu; a new revision refuses it with a receipt (invalidate).
    std::optional<HudMenuRequest> take()
    {
        if (pending_.empty())
            return {};
        auto request = std::move(pending_.front());
        pending_.pop_front();
        return request;
    }
    storm::ui::hud::LandCommands hud() const
    {
        storm::ui::hud::LandCommands out;
        out.known = true;
        out.scene = scene_;
        out.revision = revision_;
        out.available = frame_.available;
        out.active = frame_.active;
        out.can_back = out.can_close = frame_.active;
        out.stage = frame_.stage;
        out.title = frame_.pendingLabel;
        out.selected = frame_.selectedItemId;
        out.offered = frame_.offered;
        out.here_picture = frame_.herePicture;
        out.here_label = frame_.herePicture.empty() ? std::string() : frame_.hereLabel;
        for (const auto &item : frame_.items)
            out.items.push_back(hudMenuItem(item, item.picture));
        out.complete = frame_.complete && out.Drawable();
        const auto activation = [](const Receipt &r) {
            return storm::ui::hud::LandCommands::Activation{r.serial,     r.request.scene, r.request.revision,
                                                            r.request.id, r.reason,        r.accepted};
        };
        out.activation = activation(receipt_);
        for (const auto &receipt : receipts_)
            out.activations.push_back(activation(receipt));
        return out;
    }
    // What hud() reads: every change of the receipts advances the receipt serial.
    auto publicationKey() const
    {
        return std::tie(frame_, scene_, revision_, receipt_.serial);
    }
};
inline LandCommandsState landCommands;
// When the touch HUD asks the scripts for the menu again (AndroidLandRefresh, which runs the
// original BLI_SetPossibleCommands). As in the original land interface, nothing polls: the list
// is rebuilt when BIManSign is told it changed (BI_MSG_CARE_COMMANDLIST_UPDATE from
// BLI_RefreshCommandMenu / LI_CareUpdateCommandList, MSG_BATTLE_LAND_START, the menu opening or
// closing, a command executed), and right before a tap acts (executeLandCommand). Besides that
// invalidation, only this presentation's own gating changes the published menu: readiness (a
// dialogue, an interface, loading) and the land interface entity itself.
class LandRefreshGate
{
    bool ready_ = false, forced_ = true, fight_ = false;
    uint64_t owner_ = 0;

  public:
    // invalidated: the land interface's own "list changed" flag, taken this iteration. fight: the
    // hero's fight mode, which the scripts' choice of commands depends on (no transitions in a
    // fight) and which the original asks only when its menu opens: the touch HUD shows the
    // transitions button before that.
    bool due(bool ready, uint64_t owner, bool invalidated, bool fight)
    {
        const bool result = forced_ || invalidated || ready != ready_ || owner != owner_ || fight != fight_;
        ready_ = ready;
        owner_ = owner;
        fight_ = fight;
        forced_ = false;
        return result;
    }
    // The menu was changed natively (closeLandCommands): the next poll asks.
    void force()
    {
        forced_ = true;
    }
};
inline LandRefreshGate landRefresh;
// A tap on the land menu: refused when the scene or the candidates moved since it was offered, checked again after
// refresh() rebuilt them from the game's own predicates (a refreshed target never runs); dispatch() runs it natively.
// hasOwner: the land interface entity exists. Records the receipt.
struct LandTapOutcome
{
    bool accepted;
    const char *reason;
};
template <class Refresh, class Dispatch>
LandTapOutcome executeLandCommandWith(const HudMenuRequest &request, bool hasOwner, Refresh &&refresh,
                                      Dispatch &&dispatch)
{
    const auto done = [&](bool accepted, const char *reason) {
        landCommands.result(request, accepted, reason);
        return LandTapOutcome{accepted, reason};
    };
    if (request.scene != landCommands.sceneEpoch())
        return done(false, "scene-changed");
    if (request.revision != landCommands.revision())
        return done(false, "revision-changed");
    if (!landCommands.frame().available)
        return done(false, "unavailable");
    if (!hasOwner)
        return done(false, "no-land-owner");
    refresh();
    if (request.scene != landCommands.sceneEpoch())
        return done(false, "target-context-changed-on-refresh");
    if (request.revision != landCommands.revision())
        return done(false, "candidates-changed-on-refresh");
    const auto &frame = landCommands.frame();
    if (request.verb == "activateItem" && (request.id != frame.selectedItemId ||
                                           std::none_of(frame.items.begin(), frame.items.end(), [&](const auto &item) {
                                               return item.id == request.id && item.enabled;
                                           })))
        return done(false, "selection-no-longer-enabled");
    const bool result = dispatch();
    landCommands.invalidate(); // the menu moved on: taps made for this revision are stale
    return done(result, result ? "native-dispatched" : "native-refused");
}
// Defined by battle_interface. All entry points run on the game thread and
// resolve the live entity on each call; no retained pointers cross a frame.
// The main loop's entry: refreshes when landRefresh says so.
void pollLandCommands(bool ready);
// Asks the scripts now.
void refreshLandCommands(bool ready);
bool executeLandCommand(const HudMenuRequest &request);
void closeLandCommands();
} // namespace storm::android
