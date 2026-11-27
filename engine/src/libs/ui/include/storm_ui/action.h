// What the UI asks of the engine or the platform host: one struct from the tap to the node of the game's window.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "storm_ui/hud_command.h"

namespace storm::ui
{

// The one path out of the UI. For a game interface: the widget (`node`, `index`) of the open `screen`, the verb the
// player chose and its value. For a document loaded with LoadDocument (gallery): `node` is the element id, `screen`
// is empty. HUD input has `screen` "hud" and its `command` (hud_command.h). A non-empty `node` then names a
// latest-value slot (the stick position, a geometry): a host that queues actions keeps only the newest action of a
// slot. Shortcut and Presence are for the platform host, not the engine.
struct Action
{
    std::string node;
    std::string verb;  // press, select, activate, open, toggle, edit, set, left, right, back, …
    std::string value; // row identity for select/activate/open; new value for toggle/edit/set
    std::string screen;
    // InterfaceModel::opening the action belongs to. The engine refuses a widget action of another opening: the
    // player tapped a window the game has closed and opened again since.
    uint64_t opening = 0;
    int index = -1;
    std::string guard; // row the action must still apply to (commerce quantities); may be empty
    // A widget action of a game interface (AnsweredAction) carries its number of the request ledger: the engine
    // answers each one (Host::Answer). 0 for every other action.
    uint64_t request = 0;
    // HUD input (screen "hud"): what the HUD asks of the engine or the platform host (hud_command.h).
    std::optional<hud::Command> command;
};
// True for an action on a widget of a game interface: the engine applies the Action itself to the interface's node
// and answers it (Host::Answer).
bool AnsweredAction(const Action &action);
// The line of the engine's command queue for an action that is not a widget action ("back", "main:New", "hud"
// input, "game_over:END"); empty for a widget action and for actions the platform host takes.
std::string EngineCommand(const Action &action);
// One line for logs, captures and checks: the engine command, or for a widget action
// "widget:<screen>\t<node>\t<index>\t<verb>@<opening>:<value>" (guarded: "guard@<opening>:<row>|…").
std::string to_string(const Action &action);

} // namespace storm::ui
