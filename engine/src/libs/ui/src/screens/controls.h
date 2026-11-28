// View builders shared by every screen: one widget (or one group of widgets) becomes the
// JSON of one row of the pack parts <s-control> / <s-block>, with its events bound.
#pragma once

#include "screen.h"
#include "../text_utf8.h"
#include "amount.h"
#include "pictures.h"

#include <functional>
#include <initializer_list>
#include <span>

namespace storm::ui::screens
{

inline constexpr double kMiB = 1048576.0, kGiB = 1073741824.0;

// A game text on one line: line breaks ("\r", "\n"), tabs and runs of spaces become one space, trimmed.
std::string OneLine(const std::string &text);
bool Short(const std::string &text, size_t chars); // at most `chars` code points, one line
// "{1}", "{2}" … of a string table entry replaced by the arguments.
std::string Fill(std::string text, std::span<const std::string> args);
inline std::string Fill(std::string text, std::initializer_list<std::string> args)
{
    return Fill(std::move(text), std::span<const std::string>(args.begin(), args.size()));
}
// One decimal with the decimal mark of the language ("2,3").
std::string Decimal(const Bindings &bindings, double value);
// A game number with a decimal point in the interface's language ("2.4" → "2,4" except in
// English): every point between two digits.
std::string Decimal(const std::string &text, const std::string &language);
// A size in GB from a tenth of a GB, else in MB; `scale` (when given) picks the unit, so that "done of total" share
// one.
std::string Size(const Bindings &bindings, double bytes, double scale = -1);

enum class ButtonRole
{
    Exit,
    Confirm,
    Danger,
    Section,
    Other
};
ButtonRole RoleOf(const Widget &button);
// Header section a navigation button opens ("hero", "ship", …), or empty.
std::string SectionOf(const Widget &button);

// {id, label, variant, disabled, reason, busy} of a c-button; a tap presses the widget.
Json ButtonView(Bindings &bindings, const Widget &button, const std::string &id, const char *variant);
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
// {kind:"action", id, title, disabled, reason, selected, busy}: a button or choice as a list row.
Json ActionRow(Bindings &bindings, const Widget &widget, const std::string &id);
// Sends the verb a tap on a choice row means: activate on the selected row when offered,
// else select, else open.
void TapChoice(Bindings &bindings, const Widget &choice);
// Appends the rows of a run of controls (check, edit, slider, stepper) to `rows`.
// `labels` (empty, or one per widget) holds the short text that preceded a control and names it.
void ControlRows(Bindings &bindings, std::span<const Widget *const> widgets, const std::string &id_prefix,
                 const std::vector<std::string> &labels, Json &rows);

} // namespace storm::ui::screens
