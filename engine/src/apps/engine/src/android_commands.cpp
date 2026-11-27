#ifdef __ANDROID__
#include "android_commands.hpp"

#include "android_dialogue_watchdog.hpp"
#include "engine_run.hpp"
#include "platform/android_camera_zoom.hpp"
#include "platform/android_deck_walk.hpp"
#include "platform/android_held_controls.hpp"
#include "platform/android_movement.hpp"
#include "platform/android_ui.hpp"
#include "platform/android_ui_commands.hpp"
#include "shared/interface/messages.h"
#include "scene_load_status.hpp"

#include <SDL2/SDL.h>
#include <spdlog/spdlog.h>

#include <set>
#include <string>

namespace storm::engine
{
namespace
{
// The HUD's commands (storm_ui/hud_command.h), each to the engine state that takes it, at the top of a game frame.
struct HudCommands
{
    storm::android::GameplayState &gameplay;
    uint64_t cancelEpoch;

    void operator()(const storm::ui::hud::GameplayControl &tap) const
    {
        // Refused: a tap of an older gameplay revision, a control the gameplay does not bind, or one this entry
        // point does not carry (GameplayState::offer). Said, not dropped.
        if (!gameplay.offer(tap))
            spdlog::warn("refused gameplay command {}", storm::ui::hud::to_string(tap));
    }
    void operator()(const storm::ui::hud::Hold &hold) const
    {
        storm::android::heldControls.offer(hold, cancelEpoch);
    }
    void operator()(const storm::ui::hud::CombatAction &tap) const
    {
        storm::android::combatInput.offer(tap, cancelEpoch);
    }
    void operator()(const storm::ui::hud::Move &move) const
    {
        storm::android::movementInput.offer(move, cancelEpoch);
    }
    void operator()(const storm::ui::hud::DeckWalk &walk) const
    {
        storm::android::deckWalk.offer(walk, cancelEpoch);
    }
    void operator()(const storm::ui::hud::MenuStep &step) const
    {
        if (step.menu == storm::ui::hud::MenuStep::Menu::Land)
            storm::android::landCommands.offer(step);
        else
            storm::android::seaOrders.offer(step);
    }
    void operator()(const storm::ui::hud::SeaQuickTap &tap) const
    {
        const bool accepted = storm::android::seaQuickAction.offer(tap);
        storm::android::traceContextAction("sea-quick", accepted ? "accepted" : "rejected",
                                           storm::ui::hud::to_string(tap));
    }
    void operator()(const storm::ui::hud::MapCommand &command) const
    {
        if (!storm::android::worldMapHud.offer(command))
            storm::android::traceContextAction("worldmap", "rejected", storm::ui::hud::to_string(command));
    }
    void operator()(const storm::ui::hud::MapLabelArea &area) const
    {
        if (!storm::android::worldMapHud.offer(area))
            storm::android::traceContextAction("worldmap", "rejected", storm::ui::hud::to_string(area));
    }
    void operator()(const storm::ui::hud::Sails &sails) const
    {
        storm::android::shipTelemetry.offerSails(sails);
    }
    void operator()(const storm::ui::hud::Navigator &command) const
    {
        const auto &telemetry = storm::android::shipTelemetry;
        const bool accepted = storm::android::mobileScreen.empty() &&
                              storm::android::seaNavigator.offer(command, telemetry.revision(), telemetry.hudRevision(),
                                                                 telemetry.frame().navigatorGeneration);
        storm::android::traceContextAction("navigator", accepted ? "accepted" : "rejected",
                                           std::string(storm::android::seaNavigator.result()) + " " +
                                               storm::ui::hud::to_string(command));
    }
    void operator()(const storm::ui::hud::Context &context) const
    {
        const bool accepted = storm::android::mobileScreen.empty() && context.name == storm::android::contextAction;
        storm::android::traceContextAction("receive", context.name,
                                           std::string("accepted=") + (accepted ? "1" : "0") +
                                               " current=" + storm::android::contextAction);
        if (accepted)
            storm::android::requestedContextAction = context.name;
    }
    void operator()(const storm::ui::hud::Presenter &presenter) const
    {
        // Runtime failures are errors; Android never restores legacy HUD drawing.
        if (!storm::android::hudPresenters.declare(presenter.name))
            spdlog::error("unknown HUD presenter declaration '{}'", presenter.name);
    }
    void operator()(const storm::ui::hud::DialogueLost &lost) const
    {
        storm::android::endUnseenDialogue(lost.report);
    }
    // The platform host's (engine_ui.cpp sends them to the activity): never queued for the engine.
    void operator()(const storm::ui::hud::Shortcut &shortcut) const
    {
        spdlog::error("HUD shortcut {} reached the engine queue", shortcut.name);
    }
    void operator()(const storm::ui::hud::Presence &presence) const
    {
        spdlog::error("HUD presence {} reached the engine queue", presence.state);
    }
};

// Back from the interface or the activity. During a full-screen game video it is the game's Cancel: the scripts'
// video-break handlers decide whether this video may be skipped (as the player's tap does), and the interface behind
// it (often a stale one: the main menu leaves without a release) gets nothing. Otherwise one that arrives after its
// interface closed has nothing to close.
void AndroidBack()
{
    if (storm::android::fullscreenVideo())
    {
        core_private->Event("Control Activation", "s", "ICancel");
        core_private->Event("Control Deactivation", "s", "ICancel");
    }
    else if (!storm::android::mobileScreen.empty())
        core_private->Event("exitCancel");
}

// The text commands of the game interfaces' screens (android_ui_commands.hpp).
void DispatchUiCommand(const std::string &text, storm::android::QuickGame &quickGame)
{
    using storm::android::UiCommand;
    const auto parsed = storm::android::parseUiCommand(text);
    if (!parsed)
    {
        // A broken contract (tools/android/app_contract.json), never dropped quietly; logged once per command family,
        // so a per-frame sender cannot flood the log.
        static std::set<std::string> reported;
        const std::string family = text.substr(0, text.find_first_of(":\t"));
        if (reported.insert(family).second)
            spdlog::error("unknown engine command '{}' (later '{}' commands are not logged)", text, family);
        return;
    }
    switch (parsed->command)
    {
    case UiCommand::Back:
        AndroidBack();
        break;
    case UiCommand::PressSound:
        // Once per gesture: the widget actions that follow are silent (mobile_action_frame.hpp).
        if (!storm::android::mobileScreen.empty())
            core_private->Event(ISOUND_EVENT, "l", 1);
        break;
    case UiCommand::Title: {
        // One that arrives after the title closed is stale. «Продолжить» loads the newest record directly
        // (xinterface: the ANDROID_CONTINUE action), the others are the game's own menu events.
        const std::string action(parsed->argument);
        if (action != "New" && action != "Load" && action != "Continue" && action != "Options" && action != "Credits" &&
            action != "Exit")
            spdlog::error("unknown title command '{}'", text);
        else if (storm::android::mobileScreen == "mainmenu" && action == "Continue")
        {
            storm::ui::Action continued;
            continued.screen = "mainmenu";
            continued.node = "ANDROID_CONTINUE";
            continued.verb = "activate";
            continued.opening = storm::android::mobileScreenRevision;
            storm::android::uiWidgetActions.push_back(std::move(continued));
        }
        else if (storm::android::mobileScreen == "mainmenu")
            core_private->Event("backgroundcommand", "s", action.c_str());
        break;
    }
    case UiCommand::Quick:
        if (!quickGame.offer(parsed->argument, SDL_GetTicks()))
            spdlog::error("pause menu: unknown quick command {}", text);
        break;
    case UiCommand::Dialogue:
        if (storm::android::dialogueActions.size() < 8)
            storm::android::dialogueActions.emplace_back(parsed->argument);
        break;
    case UiCommand::GameOverEnd:
        // The game's own end of the death picture (interface.c StartPictureAsVideo -> PictureAsVideoBreak) goes on
        // to its title screen, as after the picture's time.
        if (storm::android::gameOverShown)
            core_private->Event("PictureAsVideoBreak");
        else
            spdlog::warn("death screen: end of the death picture asked while none is shown");
        break;
    }
}

// The activity's messages (android_mailbox.hpp), after the interface's commands of the same frame.
struct MailboxMessages
{
    storm::android::GameplayShortcuts &shortcuts;
    bool &loadCancelled; // the load was cancelled: the game restarts at its menu, nothing after it applies

    void operator()(const storm::android::mailbox::AudioFocus &focus) const
    {
        audioGate.audioFocus = focus.audible;
        audioGate.duck = focus.ducked ? 0.25f : 1.0f;
    }
    void operator()(const storm::android::mailbox::GraphicsReload &) const
    {
        storm::android::graphicsSettingsChanged = true;
    }
    void operator()(const storm::android::mailbox::HudOwnerLost &) const
    {
        // The activity saw the UI runtime fail: an error; Android never restores legacy HUD drawing.
        if (const auto line = storm::android::hudPresenters.lost(); !line.empty())
            spdlog::error("{}", line);
    }
    void operator()(const storm::android::mailbox::Back &) const
    {
        AndroidBack();
    }
    void operator()(const storm::android::mailbox::LocationLoadCancel &cancel) const
    {
        const auto state = storm::render::GetSceneLoadStatus();
        if (!state.active || cancel.generation != state.generation)
            storm::android::publishLocationReadiness("stale");
        else if (storm::android::publishLocationReadiness("cancel", true))
        {
            isRunning = false; // the app accepted the menu restart; ordinary quit cleanup, no save
            loadCancelled = true;
        }
    }
    void operator()(const storm::android::mailbox::CameraZoom &zoom) const
    {
        if (!storm::android::cameraZoom.offer(zoom.scale))
            spdlog::error("camera pinch refused: scale {}", zoom.scale);
    }
    void operator()(const storm::android::LookEvent &look) const
    {
        if (!storm::android::cameraLook.offer(look))
            spdlog::error("camera look refused: step {} without a finger down or with invalid values", look.index());
    }
    void operator()(const storm::android::mailbox::Shortcut &shortcut) const
    {
        if (shortcut.name == "mainmenu" && storm::android::fullscreenVideo())
            AndroidBack();
        else if (storm::android::mobileScreen.empty())
            shortcuts.offer(shortcut.name, SDL_GetTicks());
    }
};
} // namespace

void DispatchCommands(const std::vector<android::ui::Command> &uiCommands,
                      const std::vector<android::mailbox::Message> &messages, android::GameplayState &gameplay,
                      android::GameplayShortcuts &shortcuts, android::QuickGame &quickGame, uint64_t cancelEpoch)
{
    const HudCommands hudCommands{gameplay, cancelEpoch};
    for (const auto &queued : uiCommands)
    {
        if (const auto *hud = std::get_if<storm::ui::hud::Command>(&queued))
            std::visit(hudCommands, *hud);
        else
            DispatchUiCommand(std::get<std::string>(queued), quickGame);
    }
    bool loadCancelled = false;
    const MailboxMessages mailboxMessages{shortcuts, loadCancelled};
    for (const auto &message : messages)
    {
        std::visit(mailboxMessages, message);
        if (loadCancelled)
            break;
    }
}
} // namespace storm::engine
#endif
