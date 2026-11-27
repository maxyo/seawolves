// Screens whose logic is a script of the game's VM (the touch interface's scripts). The runtime keeps the mechanics
// (admission, bindings, modals, busy marks, drawing); the script builds the view and answers its events through
// ScriptCall. The VM side (ScriptPort) is the engine's: this library knows no VM.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
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

// The view a script built, as the VM holds it: a tree of named text values. Children keep the script's order.
struct ViewTree
{
    std::string name;
    std::string value;
    std::vector<ViewTree> children;
};

// A screen provided by a script segment: the capability it stands for, the game interface it draws, the pack screen
// it fills and the nodes it reads; `script` is the stem of the screen's file (its events name it), `look` the
// presentation its entry gives and `data` what the adapter's binding gives (both handed to the script).
struct ScriptScreen
{
    std::string capability;
    std::string interface;
    std::string component;
    std::vector<ScriptNode> nodes;
    std::string script;
    ViewTree look, data;
    // The engine makes the interface's model itself (the death screen of the game's death picture): the game's
    // interface table has no such window, so a binding of it is not looked up there.
    bool engine = false;
};

// A deal of a quantity control (screens::AmountDraft), as a script asks for it in a build: our side holds `origin`
// of row `key` and theirs `other`; the control moves our side within [low, high]. The game decides the amount: each
// move of `element` raises the event `offer` with the change asked for, and the control then shows `answer`, our side
// as the game's window holds it now (the game clamps), saying `cut_reason` when that is short of what was asked. The
// deal's button names the change with the string keys `more` / `less`; its press raises the event `commit` with it.
struct DealSpec
{
    std::string key, element;
    int origin = 0, other = 0, low = 0, high = 0, answer = 0;
    std::string cut_reason;
    std::string more, less;
    bool disabled = false;
    std::string reason, commit, offer;
};
// The draft's amount on our side, its change from the origin and the button {label, sub, disabled, reason, busy}
// (no children while there is none to press: the amount unchanged).
// What a script asks of the engine or the platform host that is not a node of the game's window (UiCommand), one
// struct per kind: an action of the application's contract (app_ui_names.hpp host_action), the game's quick save or
// load once the menu has resumed ("save", "load"), a command of the title interface ("New", "Load", …).
struct HostCommand
{
    std::string action;
};
struct QuickCommand
{
    std::string which;
};
struct TitleCommand
{
    std::string command;
};
using ScriptCommand = std::variant<HostCommand, QuickCommand, TitleCommand>;

struct DealView
{
    int value = 0, change = 0;
    ViewTree button;
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
    // not empty, is the identity of the row the action must still find (Bindings::Send); `lead`, when not empty,
    // goes out right before the value.
    virtual void Send(const std::string &node, int row, const std::string &verb, const std::string &value,
                      const std::string &guard, const std::string &lead) = 0;
    // The player's way out through `node`, or the engine's back when it is empty.
    virtual void Back(const std::string &node) = 0;
    // The view shows modal `id`; its scrim and the back key raise an event of element `cancel` (no verb), or are
    // refused when `cancel` is empty.
    virtual void Modal(const std::string &id, const std::string &cancel) = 0;
    // Inside a build: while its view is shown, the back key raises an event of element `id` (no verb) instead of
    // leaving the interface (a sheet of the screen's own open over it).
    virtual void BackTo(const std::string &id) = 0;
    virtual bool Pending(const std::string &id) const = 0;
    virtual bool Offers(const std::string &node, const std::string &verb) const = 0;
    virtual std::string Text(const std::string &key) const = 0;

    // In a build or an event: the screen waits for the game's answer to the widget actions this call sent (and to
    // those sent while it waits). A build from an answer its game completes in later frames (it says Await) is not
    // drawn: the view shown stays, and what it shows of what the answer closed takes no input. At most `seconds`;
    // then the build is "late", and an error line unless the game had answered and the build awaits nothing more.
    virtual void Hold(double seconds) = 0;
    // Inside a build: what became of the hold it runs under: "" none, "waiting", "applied" (answered, the model after
    // it in), "refused" (the engine refused an action of it), "late" (`seconds` passed). A build that is not
    // "waiting" and does not Await ends the hold and is shown.
    virtual std::string Answer() const = 0;
    // Inside a build: the game's answer is not complete in this model; more of it comes in a later game frame.
    virtual void Await() = 0;
    // Build again now: local state of the script changed.
    virtual void Rebuild() = 0;
    // The first build of this opening of the interface: the script's own state starts afresh.
    virtual bool First() const = 0;
    // A command of the engine or the platform host; a value it does not know throws.
    virtual void Command(const ScriptCommand &command) = 0;
    // A note for screen `screen` that opens later (what the death screen asks of the title, the subject a HUD tap
    // asks of the character screen): kept by the runtime across interfaces and script programs, replaced by a newer
    // one, taken once.
    virtual void Pass(const std::string &screen, const ViewTree &note) = 0;
    virtual std::optional<ViewTree> Take(const std::string &screen) = 0;
    // The last state the engine or the host published on state channel `channel` (app settings, the renderer's
    // device caps), none before the first; a channel no consumer reads throws.
    virtual std::optional<ViewTree> State(const std::string &channel) const = 0;

    virtual void Toast(const std::string &text) = 0;
    virtual void Read(const std::string &title, const std::string &text) = 0;
    // Inside an event: the text editor over the admitted field `node` with its text; the text the player accepts goes
    // to the field as its `edit`, nothing when he cancels.
    virtual void EditText(const std::string &node, const std::string &title) = 0;
    // Something the screen cannot do as designed: the build or event fails with it, and the interface is closed.
    virtual void Error(const std::string &text) = 0;

    // The figures of the game's texts, read by the rules every screen reads them by (screens/amount.h): Amount and
    // Number throw, naming `what`, when the text holds none; HasAmount: whether it holds a whole number.
    virtual int Amount(const std::string &text, const std::string &what) const = 0;
    virtual bool HasAmount(const std::string &text) const = 0;
    virtual double Number(const std::string &text, const std::string &what) const = 0;
    // A game number with the decimal mark of the interface's language; a text on one line; "{1}" of `text` filled.
    virtual std::string Decimal(const std::string &text) const = 0;
    virtual std::string OneLine(const std::string &text) const = 0;
    virtual std::string Fill(const std::string &text, const std::string &argument) const = 0;

    // Inside a build: the open deal of `spec.element`, kept by the engine while the screen is open: the player's
    // draft until he presses its button. A build that asks for no deal of an element closes it.
    virtual DealView Deal(const DealSpec &spec) = 0;

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
