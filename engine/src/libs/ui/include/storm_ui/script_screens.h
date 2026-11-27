// Screens whose logic is a script of the game's VM (the touch interface's scripts). The runtime keeps the mechanics
// (admission, bindings, modals, busy marks, drawing); the script builds the view and answers its events through
// ScriptCall. The VM side (ScriptPort) is the engine's: this library knows no VM.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace storm::ui
{
struct InterfaceModel; // interface_model.h
struct Widget;

// One node a script screen draws: the node, the widget type it must have, and whether the screen cannot draw without
// it (screens::NodeRule).
struct ScriptNode
{
    std::string node;
    std::string type;
    bool required = false;
};

// A screen provided by a script segment: the capability it stands for (the id its events carry), the game interface
// it draws, the pack screen it fills and the nodes it reads.
struct ScriptScreen
{
    std::string capability;
    std::string interface;
    std::string component;
    std::vector<ScriptNode> nodes;
};

// The view a script built, as the VM holds it: a tree of named text values. Children keep the script's order.
struct ViewTree
{
    std::string name;
    std::string value;
    std::vector<ViewTree> children;
};

// What a script screen's code may do while it runs (the natives of the VM side).
class ScriptCall
{
  public:
    // The view of this build: converted once to the pack screen's props by their spec; a mismatch throws.
    virtual void Show(const ViewTree &view) = 0;
    // Events of element `id` (and `verb`; empty takes every verb) go to the script.
    virtual void On(const std::string &id, const std::string &verb) = 0;
    // Inside an event: the view the tap leads to, drawn from this frame until the game has answered what the event
    // sent, then compared with the view built after the answer (the router's "UI prediction corrected").
    virtual void Predict(const ViewTree &view) = 0;
    // A widget action on the admitted widget `node`, or on its row `row` (-1: the node's own widget); `guard`, when
    // given, is the identity of the row the action must still find (Bindings::Send).
    virtual void Send(const std::string &node, int row, const std::string &verb, const std::string &value,
                      const std::string &guard) = 0;
    // The same after `seconds`, replaced by a newer one for the widget and verb; `lead` (empty: none) goes out right
    // before it in the same flush (Bindings::SendAfter).
    virtual void SendAfter(const std::string &node, int row, const std::string &verb, const std::string &value,
                           const std::string &guard, double seconds, const std::string &lead) = 0;
    // True while a SendAfter of the widget and verb has not gone out.
    virtual bool Waiting(const std::string &node, int row, const std::string &verb) const = 0;
    // The player's way out through `node`, or the engine's back when it is empty.
    virtual void Back(const std::string &node) = 0;
    // The view shows modal `id`; its scrim and the back key raise an event of element `cancel` (no verb), or are
    // refused when `cancel` is empty.
    virtual void Modal(const std::string &id, const std::string &cancel) = 0;
    virtual bool Pending(const std::string &id) const = 0;
    virtual bool Offers(const std::string &node, const std::string &verb) const = 0;
    virtual std::string Text(const std::string &key) const = 0;
    // Events of `id` and `verb` need no answer; Bound: this build took them (On, Ignore).
    virtual void Ignore(const std::string &id, const std::string &verb) = 0;
    virtual bool Bound(const std::string &id, const std::string &verb) const = 0;

    // Inside a build: the screen shows what it showed before this model (a step of the game's answer that is not to
    // be drawn); the handlers bound now act on this model.
    virtual void Keep() = 0;
    // The game's answer comes in a later game frame (Bindings::Await).
    virtual void Await() = 0;
    // Build again now, or after `seconds` without a new model.
    virtual void Rebuild() = 0;
    virtual void RebuildAfter(double seconds) = 0;
    // The first build of this opening of the interface: the script's own state starts afresh.
    virtual bool First() const = 0;
    virtual uint64_t Revision() const = 0; // of the model built from
    virtual double Now() const = 0;        // s
    // The engine's answers to widget actions: RequestFloor, the number of the next one; AnsweredSince(floor): 0
    // waiting, 1 every one from `floor` on applied with its model in, 2 one of them refused (Bindings::AnsweredSince).
    virtual uint64_t RequestFloor() const = 0;
    virtual int AnsweredSince(uint64_t floor) const = 0;

    virtual void Toast(const std::string &text) = 0;
    virtual void Read(const std::string &title, const std::string &text) = 0;
    // Something the screen cannot do as designed: an error line.
    virtual void Error(const std::string &text) = 0;

    // The figures of the game's texts, read by the rules every screen reads them by (screens/amount.h): Amount and
    // Number say once when the text holds none and give `none`; HasAmount: whether it holds a whole number.
    virtual int Amount(const std::string &text, const std::string &what, int none) = 0;
    virtual bool HasAmount(const std::string &text) const = 0;
    virtual double Number(const std::string &text, const std::string &what, double none) = 0;
    // A game number with the decimal mark of the interface's language; a text on one line; "{1}" of `text` filled.
    virtual std::string Decimal(const std::string &text) const = 0;
    virtual std::string OneLine(const std::string &text) const = 0;
    virtual std::string Fill(const std::string &text, const std::string &argument) const = 0;

    // The amount a quantity control sets, kept by the engine per `id` while the screen is open (screens::AmountDraft):
    // the player's draft until he confirms it.
    virtual void DraftFollow(const std::string &id, const std::string &key, int origin, int other) = 0;
    virtual void DraftClose(const std::string &id) = 0;
    virtual int DraftOrigin(const std::string &id) const = 0;
    virtual int DraftOther(const std::string &id) const = 0;
    virtual int DraftValue(const std::string &id, int low, int high) const = 0;
    // `element` change and commit move the draft.
    virtual void DraftBind(const std::string &id, const std::string &element, int low, int high,
                           const std::string &more_reason, int grain) = 0;
    // The deal's button {label, sub, disabled, reason, busy} (no children: none while the amount is unchanged),
    // labelled by the string keys `more` / `less`; its press raises the event `commit` with the change as its value.
    virtual ViewTree DraftButton(const std::string &id, int low, int high, const std::string &more,
                                 const std::string &less, const std::string &sub, bool disabled,
                                 const std::string &reason, const std::string &commit) = 0;

  protected:
    ~ScriptCall() = default;
};

// The VM side: runs the script of a screen. Both calls return only after the script has run.
class ScriptPort
{
  public:
    virtual ~ScriptPort() = default;
    // Builds the view of `screen` from the admitted widgets of the model; a node's first widget stands for it (a text
    // node's lines come as rows), its widgets with an index are its rows.
    virtual void Build(const ScriptScreen &screen, const std::vector<const Widget *> &widgets, ScriptCall &call) = 0;
    // An event the script asked for (ScriptCall::On, a modal's cancel).
    virtual void Event(const ScriptScreen &screen, const std::string &id, const std::string &verb,
                       const std::string &value, ScriptCall &call) = 0;
};
} // namespace storm::ui
