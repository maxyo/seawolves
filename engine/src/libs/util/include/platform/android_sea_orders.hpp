#pragma once
#include "android_gameplay.hpp"
#include "storm_ui/hud_command.h"
#include "storm_ui/hud_state.h"
#include <cmath>
#include <limits>
#include <tuple>

namespace storm::android
{
struct SeaOrderRecipient
{
    std::string id, name, entityId;
    int characterIndex = -1;
    bool main = false;
    bool operator==(const SeaOrderRecipient &) const = default;
};
struct SeaOrderItem
{
    std::string id, kind, label;
    // Identity is native-only and includes the complete command/target tuple.
    std::string identity;
    bool enabled = true;
    double cooldown = 1;
    // The script command behind the item ("BI_Moor"): what a touch UI picks its icon by.
    std::string source;
    bool operator==(const SeaOrderItem &) const = default;
};
struct SeaOrdersFrame
{
    bool available = false, active = false, complete = true;
    std::string language, stage = "commands", recipientId, pendingLabel, selectedItemId;
    int mode = 0;
    std::vector<SeaOrderRecipient> recipients;
    std::vector<SeaOrderItem> items;
    bool operator==(const SeaOrdersFrame &) const = default;
};
class SeaOrdersState
{
    inline static std::atomic<uint64_t> nextEpoch_{0};
    SeaOrdersFrame frame_;
    const char *defect_ = nullptr;
    std::string sceneKey_;
    uint64_t scene_ = 0, revision_ = 1;
    bool covered_ = false;
    std::deque<HudMenuRequest> pending_;

  public:
    const SeaOrdersFrame &frame() const
    {
        return frame_;
    }
    // What the last update cut or replaced in the script's frame, or nullptr: the publisher
    // logs it (android_ui.hpp, once per reason).
    const char *defect() const
    {
        return defect_;
    }
    uint64_t sceneEpoch() const
    {
        return scene_;
    }
    uint64_t revision() const
    {
        return revision_;
    }
    bool covered() const
    {
        return covered_ && frame_.active && frame_.complete;
    }
    void invalidate()
    {
        ++revision_;
        covered_ = false;
        for (const auto &request : pending_)
            hudAnswers.refused(request.request, "the menu changed before the step ran");
        pending_.clear();
    }
    void update(std::string sceneKey, SeaOrdersFrame frame)
    {
        defect_ = nullptr;
        checkMenuText(frame.language, false, frame.complete, defect_);
        checkMenuText(frame.pendingLabel, false, frame.complete, defect_);
        for (auto &recipient : frame.recipients)
            checkMenuText(recipient.name, true, frame.complete, defect_);
        for (auto &item : frame.items)
        {
            checkMenuText(item.label, true, frame.complete, defect_);
            if (!hudTextValid(item.source))
            {
                item.source.clear();
                defect_ = "an invalid item source is dropped";
            }
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
    bool offer(const storm::ui::hud::MenuStep &step)
    {
        if (step.revision != revision_ || !frame_.available)
            return false;
        const HudMenuRequest request{scene_, revision_, step.verb, step.id, step.request};
        if (request.verb == "shown")
        {
            if (request.id != "0" && request.id != "1")
                return false;
            covered_ = request.id == "1" && frame_.active && frame_.complete;
            return true;
        }
        if (request.verb != "open" && request.verb != "selectRecipient" && request.verb != "selectItem" &&
            request.verb != "activateItem" && request.verb != "back" && request.verb != "close")
            return false;
        if ((request.verb == "back" || request.verb == "close") && !request.id.empty())
            return false;
        if (request.verb != "open" && !frame_.active)
            return false;
        if (request.verb == "open" && frame_.active)
            return false;
        if (request.verb == "selectRecipient" || (request.verb == "open" && !request.id.empty()))
        {
            if (std::none_of(frame_.recipients.begin(), frame_.recipients.end(),
                             [&](const auto &r) { return r.id == request.id; }))
                return false;
        }
        if (request.verb == "selectItem" || request.verb == "activateItem")
        {
            if (std::none_of(frame_.items.begin(), frame_.items.end(), [&](const auto &item) {
                    return item.id == request.id && (request.verb != "activateItem" || item.enabled);
                }))
                return false;
        }
        pending_.push_back(request);
        return true;
    }
    // The next step for this revision of the menu; a new revision drops the queue (invalidate).
    std::optional<HudMenuRequest> take()
    {
        if (pending_.empty())
            return {};
        auto request = std::move(pending_.front());
        pending_.pop_front();
        return request;
    }
    storm::ui::hud::SeaOrders hud() const
    {
        storm::ui::hud::SeaOrders out;
        out.known = true;
        out.scene = scene_;
        out.revision = revision_;
        out.available = frame_.available;
        out.active = frame_.active;
        out.can_back = out.can_close = frame_.active;
        out.stage = frame_.stage;
        out.recipient = frame_.recipientId;
        out.title = frame_.pendingLabel;
        out.selected = frame_.selectedItemId;
        for (const auto &r : frame_.recipients)
            out.recipients.push_back({r.id, r.name, r.entityId, r.characterIndex, r.main});
        for (const auto &item : frame_.items)
            out.items.push_back(hudMenuItem(item, {}));
        out.complete = frame_.complete && out.Drawable();
        return out;
    }
    // What hud() reads.
    auto publicationKey() const
    {
        return std::tie(frame_, scene_, revision_);
    }
};
inline SeaOrdersState seaOrders;
// When the touch HUD asks the scripts for the sea orders again (BIShipIcon::Recollect and the
// command list). As in the original sea interface, a closed menu is not polled: its companions
// and their names are re-read when BIShipIcon is told its list changed (BIShipIcon::SetUpdate from
// BI_IN_CREATE_SHIP, BI_IN_DELETE_SHIP, BI_IN_DEAD_SHIP, BI_MSG_REFRESH, the menu opening or
// closing, a command executed), when this presentation's own gating changes (readiness, the
// battle interface entity, the flagship and its character, the interface showing its command
// menu) and right before a tap acts (executeSeaOrder). An open menu keeps its live script
// filtering, distances and cooldown sampling every loop.
// Keep identities, never engine pointers, across frames.
class SeaOrdersRefreshGate
{
    bool initialized_ = false, ready_ = false, active_ = false, shown_ = false;
    uint64_t battle_ = 0, ship_ = 0;
    int character_ = -1;

  public:
    // invalidated: the ship icons' own "list changed" flag, taken this iteration. shown: the battle
    // interface shows its command menu (all shown, visible, ShowCommands).
    bool due(bool ready, uint64_t battle, uint64_t ship, int character, bool active, bool shown, bool invalidated)
    {
        const bool refresh = !initialized_ || ready != ready_ || battle != battle_ || ship != ship_ ||
                             character != character_ || active != active_ || shown != shown_ ||
                             (ready && (active || invalidated));
        initialized_ = true;
        ready_ = ready;
        battle_ = battle;
        ship_ = ship;
        character_ = character;
        active_ = active;
        shown_ = shown;
        return refresh;
    }
    void force()
    {
        initialized_ = false;
    }
};
inline SeaOrdersRefreshGate seaOrdersRefresh;
// Defined by battle_interface. All entry points run on the game thread and
// resolve the live entity on each call; no retained pointers cross a frame.
// The main loop's entry: refreshes when seaOrdersRefresh says so.
void pollSeaOrders(bool ready);
void refreshSeaOrders(bool ready);
bool executeSeaOrder(const HudMenuRequest &request);
void closeSeaOrders();
} // namespace storm::android
