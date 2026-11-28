#ifdef STORM_TOUCH_UI
#include "android_game.hpp"
#include "app_script_events.hpp"

#include "android_combat_state.hpp"
#include "android_commands.hpp"
#include "android_land_commands_state.hpp"
#include "android_sea_orders_state.hpp"
#include "android_ship_abilities.hpp"
#include "android_time_scale.hpp"
#include "android_worldmap_state.hpp"
#include "engine_run.hpp"
#include "platform/android_camera_zoom.hpp"
#include "platform/android_deck_walk.hpp"
#include "platform/android_held_controls.hpp"
#include "platform/android_movement.hpp"
#include "platform/android_ui.hpp"
#include "platform/frame_work.hpp"
#include "profiler.hpp"
#include "scene_load_status.hpp"
#include "controls.h"
#include "script_state.hpp"

#include <SDL2/SDL.h>
#include <spdlog/spdlog.h>

namespace storm::engine
{
AndroidGame::AndroidGame()
{
    android::ui::attachScripts();
    android::combatInput.guard = [this]() {
        return android::combatInput.player() == std::to_string(core.GetEntityId("Player")) &&
               android::combatContextCurrent(gameplay_, Live());
    };
    android::movementInput.guard = [this](std::string_view owner) -> std::optional<android::MovementPermission> {
        const bool ready = Live();
        android::GameplaySnapshot read;
        const bool stored = ready && android::gameplaySnapshotCurrent();
        if (!stored)
            read = android::readGameplaySnapshot(ready);
        const auto &current = stored ? gameplay_.snapshot() : read;
        if (!android::movementOwnerCurrent(owner, current))
            return std::nullopt;
        return android::movementPermission<USER_CONTROL>(*core.Controls, current.capabilities, UCT_ControlTree,
                                                         UNASSIGNED_CONTROL);
    };
    // A held HUD control acts only while the gameplay that binds it is live.
    android::heldControls.guard = [this](std::string_view control) { return Live() && gameplay_.allows(control); };
    // The deck walk stick acts where the gameplay binds walking on deck (first person), as its keys.
    android::deckWalk.guard = [] {
        return android::heldControls.guard("DeckCamera_Forward") || android::heldControls.guard("DeckCamera_Backward");
    };
    Block("starting");
}

// Why the game is not ready for gameplay this loop (GameplaySnapshot::blocked).
const char *AndroidGame::NotReady() const
{
    if (!lifecycle_.canRun() || !bActive)
        return "paused";
    if (android::loadingState == 1)
        return "loading"; // -1: nothing loaded yet (starting)
    if (!android::mobileScreen.empty())
        return "interface";
    return "starting";
}

// The player can act in the game now: it runs in front, loaded, with no interface open.
bool AndroidGame::Live() const
{
    return isRunning && bActive && lifecycle_.canRun() && core_private->initialized() && core.Controls &&
           android::loadingState == 0 && android::mobileScreen.empty();
}

// No gameplay (`blocked` says why): the menus close and every HUD channel says there is none.
void AndroidGame::Block(const char *blocked)
{
    android::requestedContextAction.clear(); // a quick action tapped before is not done after
    android::closeSeaOrders();
    android::closeLandCommands();
    android::publishLandCommands();
    android::publishSeaOrders();
    android::movementInput.updateOwner({});
    android::GameplaySnapshot snapshot;
    snapshot.blocked = blocked;
    gameplay_.update(std::move(snapshot));
    android::gameplayReadEpoch = 0;
    android::heldControls.update(gameplay_.revision());
    android::deckWalk.update(gameplay_.revision());
    android::updateCombatState(gameplay_, core_private);
    android::publishCombatState();
    android::publishGameplayState(gameplay_);
    android::worldMapHud.update({});
    android::publishWorldMapHud();
    android::shipTelemetry.update(gameplay_.revision(), {});
    android::publishShipTelemetry();
    android::seaQuickLifecycleReady = false;
    android::seaQuickAction.update({});
    android::publishSeaQuickAction();
}

uint64_t AndroidGame::TakeInput()
{
    const auto combatDrainEpoch = android::combatCancelEpoch.load(std::memory_order_acquire);
    const auto inputEpoch = android::inputEpoch.now();
    // The UI host first: it applies the touches the platform sent, and what they produce
    // is read in this iteration, together with the activity's own queue.
    {
        STORM_PROFILE_ZONE("UI host: input");
        const platform::frame_work::Scope hudInput(platform::frame_work::Phase::Hud);
        android::ui::tick(inputEpoch);
    }
    // A pinch acts only in the frame it came for (android_camera_zoom.hpp).
    if (android::cameraZoom.beginFrame())
        spdlog::warn("camera pinch dropped: no camera with a zoom took it (gameplay {}, {})", gameplay_.snapshot().mode,
                     gameplay_.snapshot().blocked);
    const auto uiCommands = android::ui::takeCommands();
    const auto messages = android::pollMobileActions();
    android::publishLocationReadiness();
    DispatchCommands(uiCommands, messages, gameplay_, shortcuts_, quickGame_, combatDrainEpoch);
    return inputEpoch;
}

void AndroidGame::AfterEvents(uint64_t inputEpoch)
{
    SDL_Event appEvent;
    while (SDL_PeepEvents(&appEvent, 1, SDL_GETEVENT, SDL_APP_TERMINATING, SDL_APP_DIDENTERFOREGROUND) > 0)
    {
        lifecycle_.consume(appEvent.type);
        if (appEvent.type == SDL_APP_WILLENTERBACKGROUND || appEvent.type == SDL_APP_DIDENTERBACKGROUND ||
            appEvent.type == SDL_APP_TERMINATING)
        {
            android::cancelAndroidCombatInput();
            android::closeSeaOrders();
            android::closeLandCommands();
            android::publishLandCommands();
            android::seaQuickLifecycleReady = false;
        }
    }
    if (SDL_HasEvent(SDL_QUIT))
        lifecycle_.consume(SDL_QUIT);
    // Audio pause/resume for the lifecycle goes through AppEvents and audioGate.
    if (!lifecycle_.canRun() || !android::mobileScreen.empty())
        shortcuts_.clear();
    if (!lifecycle_.canRun())
        quickGame_.clear(); // the menu's quick save waits for the menu to close, not across a pause
    // Whenever no game frame runs for the player (paused, an interface, loading), every HUD channel moves to a
    // new revision: a tap still waiting for its frame does not act when the game comes back. The activity's input
    // epoch says it first: what this iteration took in after the input went off (or before, when it went off and
    // came back meanwhile) is dropped, the interface's and the dialogue's too.
    if (!android::inputCurrent(inputEpoch, android::inputEpoch.now()))
    {
        Block("paused");
        android::refuseUiWidgetActions("the input went off before it was applied");
        android::dialogueActions.clear();
    }
    else if (!lifecycle_.canRun() || !android::mobileScreen.empty() || android::loadingState != 0)
        Block(NotReady());
    if (lifecycle_.terminating())
        isRunning = false;
}

void AndroidGame::BeforeFrame()
{
    // A new UI host releases what the old one held before any native Player or Control Activation
    // handler reads this frame's input.
    android::publishCombatState();
    // HUD input reaches the script before RunFrame, so that what it opens or changes is drawn
    // and published in this same frame.
    if (const auto action = gameplay_.take())
    {
        STORM_PROFILE_ZONE("HUD command: gameplay action");
        STORM_PROFILE_ZONE_TEXT(action->data(), action->size());
        // A battle ability goes through the command list, as the native menu launches it.
        if (action->starts_with("BI_"))
            android::launchShipAbility(*action);
        else if (const int step = android::timeScaleStep(*action); step >= 0)
            android::applyTimeScaleStep(step);
        // The spyglass toggle: the request its controls make (ISPYGLASS::Execute), so the
        // scripts' Telescope_Request decides (a spyglass equipped, the deck camera).
        else if (*action == "TelescopeIn" || *action == "TelescopeOut")
            core_private->Event("MSG_TELESCOPE_REQUEST", "l", *action == "TelescopeIn" ? 1 : 0);
        else
            core_private->Event("Control Activation", "s", action->c_str());
    }
    // Commands run at a game-frame boundary, outside Android dialog callbacks: no
    // timed key pulse or held bit can be lost between two slow native frames.
    if (!android::mobileScreen.empty())
        shortcuts_.clear();
    const bool shortcutReady = Live();
    const char *quickExpired = nullptr;
    if (const char *quick = quickGame_.take(SDL_GetTicks(), shortcutReady, quickExpired))
    {
        // The game's own key handler (seadogs.c ProcessControls): its checks decide.
        spdlog::info("pause menu: {} pressed after the menu closed", quick);
        core_private->Event("Control Activation", "s", quick);
    }
    if (quickExpired)
        spdlog::warn("pause menu: {} dropped, the game did not resume within {} ms", quickExpired,
                     android::QuickGame::lifetimeMs);
    if (const auto shortcut = shortcuts_.take(SDL_GetTicks(), shortcutReady))
    {
        STORM_PROFILE_ZONE("HUD command: shortcut");
        const auto *control = android::shortcutControl(*shortcut);
        const auto *screen = android::shortcutScreen(*shortcut);
        [[maybe_unused]] const std::string_view shortcutName = control ? control : screen; // Tracy only
        STORM_PROFILE_ZONE_TEXT(shortcutName.data(), shortcutName.size());
        // Use exactly the ordinary control event, or the app adapter's screen opening
        // (android_open_screen.c). Script guards (dialogRun, resume.enable, fight mode,
        // etc.) still decide whether to act.
        if (control)
            core_private->Event("Control Activation", "s", control);
        else
            core_private->Event(storm::app::script_event::AndroidOpenScreen, "s", screen);
    }
}

namespace
{
// A HUD request the frame ran: applied, shown by `revision` of its channel (the state just published), or refused.
void Answer(uint64_t request, bool applied, uint64_t revision)
{
    if (applied)
        android::hudAnswers.applied(request, revision);
    else
        android::hudAnswers.refused(request, "the game did not carry it out");
}
} // namespace

void AndroidGame::AfterFrame()
{
    const bool gameplayReady = Live();
    {
        STORM_PROFILE_ZONE("HUD adapter: sea orders");
        const platform::frame_work::Scope hudSeaOrders(platform::frame_work::Phase::Hud);
        // Asks the scripts only while the menu is open or when the ship icons were told their list changed
        // (SeaOrdersRefreshGate).
        android::pollSeaOrders(android::seaOrdersReady(gameplayReady));
        android::publishSeaOrders();
        if (const auto request = android::seaOrders.take())
        {
            const bool executed = android::executeSeaOrder(*request);
            android::refreshSeaOrders(android::seaOrdersReady(gameplayReady));
            android::publishSeaOrders();
            Answer(request->request, executed, android::seaOrders.revision());
        }
    }
    {
        STORM_PROFILE_ZONE("HUD adapter: land commands");
        const platform::frame_work::Scope hudLandCommands(platform::frame_work::Phase::Hud);
        // Asks the scripts only when the land interface was told its list changed (LandRefreshGate).
        android::pollLandCommands(android::landCommandsReady(gameplayReady));
        android::publishLandCommands();
        if (const auto request = android::landCommands.take())
        {
            const bool executed = android::executeLandCommand(*request);
            android::refreshLandCommands(android::landCommandsReady(gameplayReady));
            android::publishLandCommands();
            Answer(request->request, executed, android::landCommands.revision());
        }
    }
    STORM_PROFILE_ZONE("HUD adapter: gameplay, combat, map, ship");
    const platform::frame_work::Scope hudGameplay(platform::frame_work::Phase::Hud);
    auto gameplaySnapshot = android::readGameplaySnapshot(gameplayReady);
    if (!gameplayReady)
        gameplaySnapshot.blocked = NotReady(); // the same reason as the blocked branch above: one state per loop
    android::movementInput.updateOwner(gameplaySnapshot.inputOwner);
    gameplaySnapshot.analogMovement = !gameplaySnapshot.inputOwner.empty();
    gameplaySnapshot.movementGeneration = gameplaySnapshot.analogMovement ? android::movementInput.generation() : 0;
    gameplay_.update(std::move(gameplaySnapshot));
    android::gameplayReadEpoch = gameplayReady ? storm::ScriptStateEpoch() : 0;
    android::heldControls.update(gameplay_.revision());
    android::deckWalk.update(gameplay_.revision());
    android::seaQuickLifecycleReady = gameplayReady;
    android::publishSeaQuickAction();
    android::updateCombatState(gameplay_, core_private);
    android::publishCombatState();
    android::publishGameplayState(gameplay_);
    android::worldMapHud.update(android::readWorldMapFrame(gameplay_, gameplayReady));
    android::publishWorldMapHud();
    if (const auto command = android::worldMapHud.take())
    {
        const bool executed = android::executeWorldMapCommand(command->id);
        android::worldMapHud.update(android::readWorldMapFrame(gameplay_, gameplayReady));
        android::publishWorldMapHud();
        Answer(command->request, executed, android::worldMapHud.revision());
    }
    android::shipTelemetry.update(gameplay_.revision(),
                                  android::readShipFrame(gameplay_, android::seaOrders.frame().active));
    android::publishShipTelemetry();
    if (const auto sails = android::shipTelemetry.takeSails())
    {
        const bool set = android::applyShipSailTarget(gameplay_, sails->detent);
        if (set)
        {
            android::shipTelemetry.update(gameplay_.revision(),
                                          android::readShipFrame(gameplay_, android::seaOrders.frame().active));
            android::publishShipTelemetry();
        }
        Answer(sails->request, set, android::shipTelemetry.sequence());
    }
    if (core_private->initialized() && SDL_GetTicks() - lastCinematicCheck_ >= 200)
    {
        lastCinematicCheck_ = SDL_GetTicks();
        bool cinematic = false;
        auto *index = static_cast<VDATA *>(core.GetScriptVariable("nMainCharacterIndex"));
        auto *characters = static_cast<VDATA *>(core.GetScriptVariable("Characters"));
        if (index && characters && index->GetInt() >= 0)
        {
            if (auto *character = characters->GetAClass(index->GetInt()))
            {
                auto *type = character->FindAClass(character, "chr_ai.type");
                cinematic = type && type->HasValue() && std::string_view(type->GetValue()) == "actor";
            }
        }
        android::publishCinematic(cinematic || core.GetEntityId("CAviPlayer") != invalid_entity ||
                                  core.GetEntityId("WMVideoPlay") != invalid_entity);
    }
}

void AndroidGame::CancelLoadingInput()
{
    android::cancelAndroidCombatInput();
    shortcuts_.clear();
    quickGame_.clear();
}

void AndroidGame::ConsumeLoadingEvents()
{
    SDL_Event appEvent;
    while (SDL_PeepEvents(&appEvent, 1, SDL_GETEVENT, SDL_APP_TERMINATING, SDL_APP_DIDENTERFOREGROUND) > 0)
        lifecycle_.consume(appEvent.type);
    if (SDL_HasEvent(SDL_QUIT))
        lifecycle_.consume(SDL_QUIT);
    if (lifecycle_.terminating())
        isRunning = false;
}

std::optional<platform::LoadingPlatformState> AndroidGame::LoadingState(uint64_t generation, bool &cancelPending)
{
    using State = platform::LoadingPlatformState;
    const auto readiness = render::GetSceneLoadStatus();
    if (!readiness.active || readiness.generation != generation || !android::publishLocationReadiness())
        return State::Unavailable;
#ifdef __ANDROID__ // the activity's loading overlay can cancel the load; the desktop has none
    const auto cancel = android::pollLocationLoadCancel(generation);
    if (!cancel)
        return State::Unavailable;
    cancelPending = cancelPending || *cancel;
    if (cancelPending)
    {
        // A consumed request stays latched if JNI acknowledgement fails.
        // No compiler return/reveal permission until the app has the exit reason.
        if (!android::publishLocationReadiness("cancel", true))
            return State::Suspended;
        isRunning = false;
        return State::Cancel;
    }
#endif
    if (!lifecycle_.canRun())
        return State::Suspended;
    return std::nullopt;
}

void AndroidGame::Shutdown()
{
    android::combatInput.clear();
    android::combatInput.guard = {};
    android::movementInput.guard = {};
    android::heldControls.guard = {};
    android::deckWalk.guard = {};
    Block("starting");       // the engine is going down: nothing of the game is usable
    android::ui::shutdown(); // its textures and frame client go before the renderer
}
} // namespace storm::engine
#endif
