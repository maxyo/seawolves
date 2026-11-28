// The engine's side of the portable UI on Android: the UI host lives in this process and
// draws inside the engine frame (src/libs/ui_host). Engine code publishes its states and
// interface models here, on the game thread, in-process; nothing of it crosses JNI.
// Implemented by src/libs/ui_host/src/engine_ui.cpp.
#pragma once
#ifdef STORM_TOUCH_UI
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "storm_ui/dialogue_state.h"
#include "storm_ui/hud_command.h"
#include "storm_ui/hud_state.h"

namespace storm::ui
{
struct InterfaceModel; // storm_ui/interface_model.h
}

namespace storm::android::ui
{
// A state channel of the portable UI's screens: JSON as the UI host's SetState takes it ("null" =
// nothing). Kept until the host exists, then delivered in order.
void state(std::string_view channel, std::string json);
// The state of a HUD channel, taken as the struct itself: a state equal to the channel's last one publishes
// nothing; the newest of each channel reaches a host that starts later.
void state(storm::ui::HudState state);
// The game's dialogue (libs/dialog), taken as the struct itself: every call reaches the host, an identical one too (a
// refused answer is acknowledged each time); the newest reaches a host that starts later.
void dialogueState(storm::ui::Dialogue dialogue);
// The open interface opened (`screen` not empty, `revision` of the opening) or closed. An
// opening says how long the game took to make it: releasing the old interface and loading
// the new one's INI with its nodes (milliseconds, logged with the opening).
void screen(const std::string &screen, uint64_t revision, double release_ms = 0.0, double load_ms = 0.0);
// The interface that closed is followed by no other: the UI drops it now. Until this is said (or another interface
// opens, or a load starts) the UI keeps drawing the closed one: the scripts close one menu some frames before they
// open the next, and the game must not flash between them. Said every frame while no interface is open and none
// is scheduled (xinterface); a state of the game, not a time.
void interfaceGone();
// The game's language (its string service), in the call that sets it: the UI's strings and what it makes ahead of
// the first opening of a screen (Host::Prewarm) follow it from then on, before any interface model names it.
void language(std::string game_language);
// The interface model of the open screen (xinterface), in the frame it changed; the newest
// replaces older ones. Taken as the struct itself: nothing is serialised.
void interfaceModel(storm::ui::InterfaceModel &&model);
// The answer to a widget action of the open interface (storm::ui::Action::request): applied (its
// script events have run) or refused with the reason. Request 0 is nobody's: ignored.
void answer(uint64_t request, bool applied, std::string reason);
// The engine's facts of state channel "presentation", in the frame they change: a loading screen
// is up, a cinematic plays (the hero is an actor, a video runs), a dialogue covers the game. The
// open interface comes from screen(); only `live` (the game is in front and running) is Android's.
void loading(bool active);
void cinematic(bool active);
void dialogue(bool active);
// A location load (render::GetSceneLoadStatus) blocks the game: shown as loading.
void locationLoad(bool blocks);
// A passing line of the game's log: a toast over the game; `target` is where a tap on it
// leads (Host::GameMessage), "" = nowhere.
void toast(std::string text, std::string target = {});
// Something the player asked for failed: a lasting danger toast of the pack string `string_key`.
void alert(std::string string_key);
// Top of every engine loop iteration, before the queued commands are read: starts the host
// once the renderer is ready and applies what the platform thread sent (touches of `input_epoch`, presentation).
// The touch interface's scripts are attached to every program the game runs (storm::script::SetAttacher): called
// once, before the game's program starts.
void attachScripts();
void tick(uint64_t input_epoch);
// A command of the UI: the HUD's (storm_ui/hud_command.h), or the text one of a game interface's screen
// ("main:Load", "back").
using Command = std::variant<storm::ui::hud::Command, std::string>;
// The commands the UI produced since the last call, oldest first; a latest-value slot (a stick position)
// appears once, with its newest command.
std::vector<Command> takeCommands();
// Increments when a UI host starts (0 before the first): what the old host held (combat, the walk stick) and the
// world map's compass box are released for every new host.
uint64_t generation();
// Before the renderer goes away at engine exit.
void shutdown();
} // namespace storm::android::ui
#endif
