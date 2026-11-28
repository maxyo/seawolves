// The amount of a quantity control as the player sets it until he confirms it (the deal of UiDeal).
#pragma once

#include "screen.h"

#include <functional>

namespace storm::ui::screens
{

// The amount a quantity control sets is the player's draft until he confirms it (picking an
// amount moves nothing). A change or a drag of the control moves only the draft; the deal's button
// («Взять / Отдать N» for a free transfer, «Купить / Продать N» for a money deal) sends it to the game.
// A transfer has no «Отмена»: tapping another row, or the control back at its
// origin, drops the draft. Every screen whose quantity moves something keeps one per open control.
class AmountDraft
{
  public:
    struct Labels
    {
        const char *more;      // string key of the operation that raises our side («Взять», «Купить»)
        const char *less;      // … and of the one that lowers it («Отдать», «Продать»)
        bool absolute = false; // the button names the amount set, not the change («Сохранить 500» of an order)
    };
    // The control of `key` (a row) shows `origin` (ours) beside `other` (theirs) as the game has them now. A new
    // key, or figures the game changed (a move went through), open the draft again at the origin. A draft that
    // was sent waits for the engine's answers to its actions (Bindings::AnsweredSince): the control keeps showing
    // it, and no second deal can be sent meanwhile. Refused, or applied with the game's figures unchanged in the
    // model that followed (the game's script turned the deal down), it opens again at the game's figures. No
    // answer in kNoAnswer s is an error.
    void Follow(const std::string &key, int origin, int other, Bindings &bindings);
    void Close(); // the control is gone (the game closed its window)
    int Origin() const
    {
        return origin_;
    }
    int Other() const
    {
        return other_;
    }
    // The amount the control shows, within [low, high].
    int Value(int low, int high) const;
    // Binds `<id> change` and `<id> commit` of the control: both move the draft and build again; `moved`, when
    // given, is told the new amount (a script screen offers it to the game). Past `high` a `more_reason` (the
    // purse, the room) is said once per push, past `low` a `less_reason` (the other side's room or money).
    void Bind(Bindings &bindings, const std::string &id, int low, int high, const std::string &more_reason,
              const std::string &less_reason = {}, std::function<void(int)> moved = {});
    // The game's window holds `value` now: the draft shows it (not while a press of its button is answered).
    void Answered(int value)
    {
        if (open_ && !sent_)
            target_ = value;
    }
    // The deal's button while the draft differs from its origin: returns the primary {label "<operation> N",
    // sub, disabled, reason, busy}, whose press calls `commit(delta)` (delta > 0 raises our side). Null while
    // the amount is unchanged; while a sent draft waits for its answer the button stays, busy, and takes no press.
    Json Buttons(Bindings &bindings, int low, int high, Labels labels, const std::string &sub, bool disabled,
                 const std::string &reason, std::function<void(int)> commit);
    // «Отмена» (id "deal-cancel") appended to `actions` while a changed draft is not sent: it drops the draft.
    // Only for a setting kept as a draft (the treasurer's standing order), never for a transfer.
    void CancelAction(Bindings &bindings, Json &actions);
    // Drops a changed draft (the platform back); false when there was nothing to drop.
    bool Drop();

  private:
    static constexpr double kNoAnswer = 5.0; // s: a sent draft without an answer by then is an error
    void Open(const std::string &key, int origin, int other);
    // The deal was sent: it waits for the answer to the actions sent from now on.
    void Sent(int value, Bindings &bindings);
    bool open_ = false;
    std::string key_;
    int origin_ = 0, other_ = 0;
    int target_ = 0;
    bool sent_ = false;
    double sent_at_ = 0.0;
    uint64_t floor_ = 0;       // the first request of the deal (Bindings::RequestFloor)
    double reason_at_ = -10.0; // when the "cannot go further" reason was last said
};

} // namespace storm::ui::screens
