#include "deal.h"

#include "amount.h"

#include <algorithm>
#include <cstdlib>

namespace storm::ui::screens
{

void AmountDraft::Open(const std::string &key, int origin, int other)
{
    open_ = true;
    key_ = key;
    origin_ = origin;
    other_ = other;
    target_ = origin;
    sent_ = false;
}

void AmountDraft::Follow(const std::string &key, int origin, int other, Bindings &bindings)
{
    if (!open_ || key != key_ || origin != origin_ || other != other_)
        Open(key, origin, other); // a new control, or the game's figures changed (a move went through)
    else if (sent_)
    {
        switch (bindings.AnsweredSince(floor_))
        {
        case Bindings::Answers::Refused: // the engine refused an action of the deal (logged by the router)
        // all applied, and the model after them still has the old figures: the game said no
        case Bindings::Answers::Applied:
            Open(key, origin, other);
            break;
        case Bindings::Answers::Waiting:
            if (bindings.Now() - sent_at_ >= kNoAnswer)
            {
                bindings.Error("the deal on " + key + " got no answer from the game in 5 s");
                Open(key, origin, other);
            }
            else
                bindings.RebuildAfter(sent_at_ + kNoAnswer - bindings.Now());
            break;
        }
    }
}

void AmountDraft::Close()
{
    open_ = false;
    key_.clear();
    sent_ = false;
}

int AmountDraft::Value(int low, int high) const
{
    return !open_ ? origin_ : std::clamp(target_, low, std::max(low, high));
}

void AmountDraft::Bind(Bindings &bindings, const std::string &id, int low, int high, const std::string &more_reason,
                       const std::string &less_reason, std::function<void(int)> moved)
{
    const auto set = [this, &bindings, low, high = std::max(low, high), more_reason, less_reason,
                      moved](const Event &event) {
        int wanted = 0;
        if (!EventNumber(bindings, event, wanted))
            return;
        // Past an end the purse or the room allows: the reason, once per push (a drag reports many samples).
        const std::string &reason = wanted > high ? more_reason : wanted < low ? less_reason : std::string();
        if (!reason.empty() && bindings.Now() - reason_at_ > 1.5)
        {
            reason_at_ = bindings.Now();
            bindings.Toast(reason);
        }
        if (!sent_)
        {
            target_ = std::clamp(wanted, low, high);
            if (moved)
                moved(target_);
        }
        bindings.Rebuild();
    };
    bindings.On(id, "change", set);
    bindings.On(id, "commit", set);
}

Json AmountDraft::Buttons(Bindings &bindings, int low, int high, Labels labels, const std::string &sub, bool disabled,
                          const std::string &reason, std::function<void(int)> commit)
{
    const int value = Value(low, high);
    const int delta = value - origin_;
    if (!open_ || (!sent_ && delta == 0))
        return Json();
    const std::string label =
        bindings.Text(delta > 0 ? labels.more : labels.less) + " " + std::to_string(std::abs(delta));
    if (sent_)
    {
        // Sent, the game's answer not here yet: the same button waits, and a second press sends nothing (nor
        // what the screen's bar has in its place without a deal, such as a take-all).
        bindings.On("primary", "", [](const Event &) {});
        return Json{{"label", label}, {"sub", sub}, {"disabled", false}, {"reason", ""}, {"busy", true}};
    }
    bindings.On("primary", "", [this, &bindings, value, delta, disabled, commit](const Event &) {
        if (disabled || sent_)
            return;
        Sent(value, bindings);
        commit(delta);
        bindings.Rebuild();
    });
    return Json{{"label", label},
                {"sub", sub},
                {"disabled", disabled},
                {"reason", reason},
                {"busy", bindings.Pending("primary")}};
}

void AmountDraft::Sent(int value, Bindings &bindings)
{
    target_ = value;
    sent_ = true;
    sent_at_ = bindings.Now();
    floor_ = bindings.RequestFloor();
}

} // namespace storm::ui::screens
