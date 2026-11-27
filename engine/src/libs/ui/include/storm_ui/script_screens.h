// Screens whose logic is a script of the game's VM (the touch interface's scripts). The runtime keeps the mechanics
// (admission, bindings, modals, busy marks, drawing); the script builds the view and answers its events through
// ScriptCall. The VM side (ScriptPort) is the engine's: this library knows no VM.
#pragma once

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
    // A widget action on the admitted widget `node`.
    virtual void Send(const std::string &node, const std::string &verb, const std::string &value) = 0;
    // The player's way out through `node`, or the engine's back when it is empty.
    virtual void Back(const std::string &node) = 0;
    // The view shows modal `id`; its scrim and the back key raise an event of element `cancel` (no verb), or are
    // refused when `cancel` is empty.
    virtual void Modal(const std::string &id, const std::string &cancel) = 0;
    virtual bool Pending(const std::string &id) const = 0;
    virtual bool Offers(const std::string &node, const std::string &verb) const = 0;
    virtual std::string Text(const std::string &key) const = 0;

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
