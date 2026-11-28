#pragma once
#include <optional>
#ifdef STORM_TOUCH_UI
#include <SDL2/SDL.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <nlohmann/json.hpp>
#include <set>
#include <spdlog/spdlog.h>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include "../istring.hpp"
#include "android_hud_channel.hpp"
#include "android_ship_telemetry.hpp"
#include "android_spyglass.hpp"
#include "android_sea_navigator.hpp"
#include "android_sea_orders.hpp"
#include "android_sea_quick_action.hpp"
#include "android_land_commands.hpp"
#include "android_land_vitals.hpp"
#include "android_worldmap.hpp"
#include "android_interface_draw.hpp"
#include "android_land_diagnostics.hpp"
#include "android_mailbox.hpp"
#include "android_runtime.hpp"
#include "android_ui_sink.hpp"
#include "storm_ui/action.h"
#include "jni_text.hpp"
#include "scene_load_status.hpp"
#ifdef __ANDROID__
// The activity's copies of the engine's facts (its own controls, overlays and settings): the Java host's only.
#include <SDL2/SDL_system.h>
#include <android/log.h>
#include <jni.h>
#endif

namespace storm::android
{
// All native state is accessed only by the game thread.
inline std::string mobileScreen;
// The JSON text of a state for the UI's screens: text that is not UTF-8 is replaced (U+FFFD), never
// thrown on.
inline std::string uiJson(const nlohmann::ordered_json &value)
{
    return value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}
// What a channel's data lost on its way to the UI (cut, replaced, not published): logged once
// per channel and reason, so a mismatch between the scripts and the UI is never silent and
// never floods the log.
inline void logChannelDefect(const char *channel, const std::string &reason)
{
    static std::set<std::string> logged;
    if (logged.insert(std::string(channel) + '\x1f' + reason).second)
        spdlog::error("UI channel {}: {} (logged once)", channel, reason);
}
// Text of the game's scripts for a HUD channel: the UI's renderer takes UTF-8 only. Text that is not is left out
// and reported.
inline std::string scriptText(std::string_view text, const char *channel)
{
    const char *defect = nullptr;
    std::string out = hudScriptText(text, defect);
    if (defect)
        logChannelDefect(channel, defect);
    return out;
}
// A caption of the game for a HUD channel (hudCaption), or "" (reported).
inline std::string scriptCaption(std::string_view text, const char *channel)
{
    const char *defect = nullptr;
    std::string out = hudCaption(text, defect);
    if (defect)
        logChannelDefect(channel, defect);
    return out;
}
// A FaceId of the scripts for a portrait: one the HUD can build a texture path from, or none (reported).
inline std::string scriptFaceId(std::string_view id, const char *channel)
{
    const char *defect = nullptr;
    std::string out = hudFaceId(id, defect);
    if (defect)
        logChannelDefect(channel, std::string(defect) + ": " + scriptText(id, channel));
    return out;
}
// A number of the game's scripts that the HUD looks up in a table of `count` entries from `first`: -1 (reported)
// outside it; negative stays -1 (not shown).
inline int scriptIndex(int value, int first, int count, const char *channel, const char *what)
{
    if (value < 0)
        return -1;
    if (value >= first && value < first + count)
        return value;
    logChannelDefect(channel, std::string(what) + " " + std::to_string(value) + " is outside its table");
    return -1;
}
#ifdef __ANDROID__
// A local Java string of engine text for an upcall to the activity; null (logged) when the text is not valid UTF-8 or
// the VM failed (its exception is then pending).
inline jstring javaText(JNIEnv *env, const std::string &text, const char *upcall)
{
    try
    {
        if (const jstring value = jni_text::ToJava(env, text))
            return value;
        spdlog::error("GameActivity.{}: the VM could not make a string", upcall);
    }
    catch (const std::exception &error)
    {
        spdlog::error("GameActivity.{}: text not sent: {}", upcall, error.what());
    }
    return nullptr;
}

// An engine error that ends the game, in the app's dialog in the player's language (GameActivity.showEngineError):
// `title` is a key of the strings tables, `reason` the engine's text. Returns once the player dismissed it; false
// (logged) when the activity could not show it.
inline bool showEngineError(const char *title, const std::string &reason)
{
    const bool shown =
        CallActivity(upcall::GameActivity::showEngineError, [&](JNIEnv *env, jobject activity, jmethodID method) {
            const jstring key = javaText(env, title, "showEngineError");
            const jstring text = key ? javaText(env, reason, "showEngineError") : nullptr;
            if (text)
                env->CallVoidMethod(activity, method, key, text);
            if (key)
                env->DeleteLocalRef(key);
            if (text)
                env->DeleteLocalRef(text);
            return text != nullptr;
        });
    if (!shown)
        spdlog::error("GameActivity.showEngineError failed: the error is shown in SDL's dialog");
    return shown;
}
#endif
inline uint64_t mobileScreenRevision = 1;
// The touch UI's widget actions for the open interface, as the UI made them (XINTERFACE::DoControl applies them), and
// for the dialogue (a reply, a typed answer: libs/dialog takeDialogueChoice). Every one is answered (ui::answer);
// request 0 = nobody waits for the answer.
inline std::deque<storm::ui::Action> uiWidgetActions, dialogueActions;
inline std::string contextAction, requestedContextAction;
inline int cinematicState = -1;
// The game's death picture is up (xinterface info_handler.cpp): the death screen's choice may
// end it at once ("game_over:END", android_commands.cpp).
inline bool gameOverShown = false;
// Gameplay state: channel "gameplay" of the UI host, and the activity's copy for its own controls (look surface,
// virtual keys). Both are made for a new gameplay revision only.
inline uint64_t gameplayRevisionPublished = 0;
inline void publishGameplayState(const GameplayState &state)
{
    if (state.revision() == gameplayRevisionPublished)
        return;
    gameplayRevisionPublished = state.revision();
    ui::state(state.hud());
#ifdef __ANDROID__
    const auto &snapshot = state.snapshot();
    if (!CallActivity(upcall::GameActivity::showGameplayState, [&](JNIEnv *env, jobject activity, jmethodID method) {
            const auto mode = javaText(env, snapshot.mode, "showGameplayState");
            const auto language = javaText(env, snapshot.language, "showGameplayState");
            const auto owner = javaText(env, snapshot.inputOwner, "showGameplayState");
            if (mode && language && owner)
                env->CallVoidMethod(activity, method, static_cast<jlong>(state.revision()), mode, language, owner,
                                    static_cast<jboolean>(snapshot.analogMovement),
                                    static_cast<jlong>(snapshot.movementGeneration));
            for (const auto text : {mode, language, owner})
                if (text)
                    env->DeleteLocalRef(text);
            return mode && language && owner;
        }))
        spdlog::error("gameplay state: GameActivity.showGameplayState failed");
#endif
}
// Live ship telemetry (channel "ship"): a change goes at most every 100 ms, a new revision or main ship at once.
inline void publishShipTelemetry()
{
    if (auto state = shipTelemetry.publication(SDL_GetTicks()))
        ui::state(std::move(*state));
}
inline uint64_t worldMapHost = 0;
inline void publishWorldMapHud()
{
    if (worldMapHost != ui::generation())
    {
        worldMapHost = ui::generation();
        worldMapHud.resetActivity();
    } // a new activity: published at once
    const auto tick = SDL_GetTicks();
    if (worldMapHud.due(tick))
    {
        ui::state(worldMapHud.hud());
        worldMapHud.published(tick);
    }
}
inline std::optional<decltype(changeKey(seaOrders.publicationKey()))> seaOrdersPublished;
inline void publishSeaOrders()
{
    if (!changed(seaOrdersPublished, seaOrders.publicationKey()))
        return;
    if (const char *defect = seaOrders.defect())
        logChannelDefect("seaOrders", defect);
    ui::state(seaOrders.hud());
}
// The sea quick action: a new offer goes when its action, target or revision changed.
inline std::optional<decltype(changeKey(seaQuickAction.publicationKey()))> seaQuickActionPublished;
inline void publishSeaQuickAction()
{
    if (changed(seaQuickActionPublished, seaQuickAction.publicationKey()))
        ui::state(seaQuickAction.hud());
}
inline std::optional<decltype(changeKey(landCommands.publicationKey()))> landCommandsPublished;
inline void publishLandCommands()
{
    if (!changed(landCommandsPublished, landCommands.publicationKey()))
        return;
    if (const char *defect = landCommands.defect())
        logChannelDefect("landCommands", defect);
    ui::state(landCommands.hud());
}
// The land hero's bars and status (android_land_vitals.hpp), nothing when the engine draws no land
// hero; a change goes at most every 100 ms, the hero's disappearance at once. `picture` gives a
// portrait's picture (battle_interface land_status.hpp).
inline VitalsPublication vitalsPublication;
template <class Picture> inline void publishVitals(const std::optional<LandVitals> &vitals, Picture &&picture)
{
    if (vitalsPublication.due(vitals, SDL_GetTicks()))
        ui::state(vitals ? landVitalsHud(*vitals, picture) : storm::ui::hud::Vitals());
}
inline void publishVitals(std::nullopt_t)
{
    publishVitals(std::optional<LandVitals>(), [](const LandPortrait &) { return storm::ui::hud::Picture(); });
}
inline std::optional<std::string> worldInfoPublished;
inline void publishWorldInfo(const std::string &text)
{
    if (changed(worldInfoPublished, text))
        ui::state(storm::ui::hud::WorldInfo{text});
}
// A passing line of the game's event log (Log_Info and alike): the portable UI shows it as
// a toast (the HUD's top-centre log over a free game). Every line goes; the UI counts a
// repeat of a shown one («×2») instead of stacking it.
// > 0 while the engine runs a script action whose log line the HUD already shows otherwise
// (the time scale: its button carries the multiplier).
inline int gameMessagesMuted = 0;
// `target`: where a tap on the toast leads ("" = nowhere; game_message_target.hpp tags it at the
// script function that wrote the line).
inline void publishGameMessage(const std::string &text, const std::string &target = {})
{
    if (gameMessagesMuted > 0 || text.empty())
        return;
    ui::toast(text, target);
}
// Set from the activity's mailbox (its graphics reload, sent by the platform host after
// the settings screen stored new renderer preferences); the renderer re-applies the graphics
// effects from the preferences at its next frame (renderer.cpp).
inline std::atomic<bool> graphicsSettingsChanged{false};
// A debug build of the app: ApplicationInfo.FLAG_DEBUGGABLE, which tools/android/build_apk.py sets for
// --build-type debug (development and test builds) and never for release (the GitHub and Play release APKs).
// The engine's one source for debug-only looks and notices: non-fatal errors are shown on screen only here
// (Config::show_errors of the UI, publishDiagnostic, the script patch notice); release builds log them.
// False when there is no activity to ask.
inline bool debugBuild()
{
#ifndef __ANDROID__
    return true; // the desktop's touch interface is a development build only (engine/README.md, "Linux")
#else
    bool result = false;
    const bool asked = CallActivity(JavaMethod{"getApplicationInfo", "()Landroid/content/pm/ApplicationInfo;"},
                                    [&](JNIEnv *env, jobject activity, jmethodID method) {
                                        const auto info = env->CallObjectMethod(activity, method);
                                        const auto type = info ? env->GetObjectClass(info) : nullptr;
                                        const auto flags = type ? env->GetFieldID(type, "flags", "I") : nullptr;
                                        constexpr jint kFlagDebuggable = 2; // ApplicationInfo.FLAG_DEBUGGABLE
                                        if (flags)
                                            result = (env->GetIntField(info, flags) & kFlagDebuggable) != 0;
                                        if (type)
                                            env->DeleteLocalRef(type);
                                        if (info)
                                            env->DeleteLocalRef(info);
                                        return flags != nullptr;
                                    });
    return asked && result;
#endif
}
// A non-fatal problem the engine has already logged (error.log / logcat): a toast in debug builds, nothing on
// the screen in release builds. The one gate for the engine's diagnostic toasts outside the UI library.
inline void publishDiagnostic(const std::string &text)
{
    if (debugBuild())
        ui::toast(text);
}
#ifdef __ANDROID__
inline int graphicsSetting(const char *key, int fallback)
{
    int value = fallback;
    const bool read =
        CallActivity(upcall::GameActivity::getGraphicsSetting, [&](JNIEnv *env, jobject activity, jmethodID method) {
            const auto name = env->NewStringUTF(key);
            if (!name)
                return false;
            value = env->CallIntMethod(activity, method, name, static_cast<jint>(fallback));
            env->DeleteLocalRef(name);
            return true;
        });
    return read ? value : fallback;
}
// The automatic graphics preset chose `preset` (renderer auto_preset.hpp, measurement `version`, `report` its
// 0: the preset's own): GameActivity.autoPresetChosen stores it unless the player chose a preset meanwhile, tells the
// player in the game's `language` (the string service's, as the interfaces' models carry it) and asks for
// a graphics reload. False (logged) when the host could not be told.
inline bool reportAutoPreset(int preset, int render_height, int version, const std::string &report,
                             const std::string &language)
{
    const bool reported =
        CallActivity(upcall::GameActivity::autoPresetChosen, [&](JNIEnv *env, jobject activity, jmethodID method) {
            const auto text = javaText(env, report, "autoPresetChosen");
            const auto lang = text ? javaText(env, language, "autoPresetChosen") : nullptr;
            if (lang)
                env->CallVoidMethod(activity, method, static_cast<jint>(preset), static_cast<jint>(render_height),
                                    static_cast<jint>(version), text, lang);
            for (const jobject local : {static_cast<jobject>(text), static_cast<jobject>(lang)})
                if (local)
                    env->DeleteLocalRef(local);
            return lang != nullptr;
        });
    if (!reported)
        spdlog::error(
            "graphics auto preset: preset {} ({}-line scene) not stored: GameActivity.autoPresetChosen failed", preset,
            render_height);
    return reported;
}
// The graphics API the game's device runs and why (renderer_bgfx frame_owner.hpp RenderBackendChoice), after each
// device creation: GameActivity.renderBackendChosen keeps it for the graphics settings screen. Logged when the host
// could not be told (the "render backend:" log line stays the record either way).
inline void reportRenderBackend(const std::string &backend, const std::string &reason)
{
    const bool reported =
        CallActivity(upcall::GameActivity::renderBackendChosen, [&](JNIEnv *env, jobject activity, jmethodID method) {
            const auto name = javaText(env, backend, "renderBackendChosen");
            const auto why = name ? javaText(env, reason, "renderBackendChosen") : nullptr;
            if (why)
                env->CallVoidMethod(activity, method, name, why);
            for (const jobject local : {static_cast<jobject>(name), static_cast<jobject>(why)})
                if (local)
                    env->DeleteLocalRef(local);
            return why != nullptr;
        });
    if (!reported)
        spdlog::error("render backend: GameActivity.renderBackendChosen failed; the settings do not show {}", backend);
}
#endif
// The door the hero stands at (the touch HUD's bottom hint): where its reload locator leads and
// whether it is locked now (the game's "Closed" quick action); "" = at no door with a name.
inline std::optional<std::tuple<std::string, bool>> reloadPlacePublished;
inline void publishReloadPlace(const std::string &place, bool locked)
{
    if (changed(reloadPlacePublished, std::tie(place, locked)))
        ui::state(place.empty() ? storm::ui::hud::Door() : storm::ui::hud::Door{place, locked});
}
inline void publishContextAction(const std::string &name)
{
    if (contextAction == name)
        return;
    traceContextAction("publish", name, "previous=" + contextAction);
    contextAction = name;
    requestedContextAction.clear();
    ui::state(storm::ui::hud::ContextAction{name});
}
// The host starts this engine straight at the game's main menu (GameActivity.startsAtMainMenu,
// after the pause menu's exit): until the first interface opens, the videos are broken off
// through the game's own video break (CAviPlayer, ICancel as a tap sends).
inline bool startAtMainMenu = false;
#ifdef __ANDROID__
inline void readStartAtMainMenu()
{
    bool starts = false;
    if (!CallActivity(upcall::GameActivity::startsAtMainMenu, [&](JNIEnv *env, jobject activity, jmethodID method) {
            starts = env->CallBooleanMethod(activity, method);
            return true;
        }))
    {
        spdlog::error("Start at the main menu: GameActivity.startsAtMainMenu failed; the game starts with its videos");
        return;
    }
    startAtMainMenu = starts;
    if (startAtMainMenu)
        spdlog::info("Start at the main menu: the videos before it are skipped");
}
#endif
inline int loadingState = -1;
inline void publishLoading(bool active)
{
    if (loadingState == static_cast<int>(active))
        return;
    loadingState = active;
    ui::loading(active);
#ifdef __ANDROID__
    CallActivity(upcall::GameActivity::showLoading, [&](JNIEnv *env, jobject activity, jmethodID method) {
        env->CallVoidMethod(activity, method, static_cast<jboolean>(active));
        return true;
    });
#endif
}
// Loading overlay consumer is supplied by the app. Synchronous cancel acknowledgement sets its restart reason
// before the engine loop exits; the Activity marshals only view updates to its UI thread.
// Selective mailbox read: no gameplay, shortcut, settings or stale-generation
// command is consumed. Nothing (logged) when the activity could not be asked, which is distinct from an empty
// mailbox; the activity ships with this engine (tools/android/app_contract.json), so that is an error.
#ifdef __ANDROID__
inline std::optional<bool> pollLocationLoadCancel(uint64_t generation)
{
    bool cancel = false;
    if (!CallActivity(upcall::GameActivity::pollLocationLoadCancel,
                      [&](JNIEnv *env, jobject activity, jmethodID method) {
                          cancel = env->CallBooleanMethod(activity, method, static_cast<jlong>(generation)) == JNI_TRUE;
                          return true;
                      }))
    {
        spdlog::error("location load: GameActivity.pollLocationLoadCancel(J)Z failed; the load cannot be cancelled");
        return std::nullopt;
    }
    return cancel;
}
#endif
inline bool publishLocationReadiness(const char *ack = "", bool cancelled = false)
{
    const auto r = render::GetSceneLoadStatus();
    const bool active = r.active && !cancelled;
    const std::string state = cancelled ? "cancelled" : r.state;
    // A location load under way (or being cancelled) covers the game like the loading screen.
    ui::locationLoad(active || cancelled);
    static std::tuple<uint64_t, bool, std::string, std::string> previous;
    const auto current = std::make_tuple(r.generation, active, state, std::string(ack));
    if (previous == current && !cancelled)
        return true;
#ifndef __ANDROID__
    previous = current; // the desktop has no loading overlay of its own to tell
    return true;
#else
    const bool delivered =
        CallActivity(upcall::GameActivity::onLocationReadiness, [&](JNIEnv *env, jobject activity, jmethodID method) {
            const auto name = javaText(env, state, "onLocationReadiness");
            const auto answer = javaText(env, ack, "onLocationReadiness");
            if (name && answer)
                env->CallVoidMethod(activity, method, static_cast<jlong>(r.generation), name,
                                    static_cast<jboolean>(active), answer);
            for (const auto text : {name, answer})
                if (text)
                    env->DeleteLocalRef(text);
            return name && answer;
        });
    // Undelivered readiness never permits the reveal (the caller stops); the activity ships with this engine.
    if (!delivered)
        spdlog::error("location load: GameActivity.onLocationReadiness failed");
    else
        previous = current;
    return delivered;
#endif
}
inline void publishSpyglass()
{
    ui::state(spyglass.hud());
}
// Channel "video": {"fullscreen": true} while a game video plays over the whole screen (the
// scripts' StartVideo player), null when it ends. A player embedded in an interface node
// (xi_video_rect, SetShowVideo(false)) never sets it: its screen stays. Back is then the
// game's Cancel to the video (main.cpp).
inline const void *fullscreenVideoOwner = nullptr;
inline void publishFullscreenVideo(const void *owner, bool active)
{
    if (active)
    {
        if (fullscreenVideoOwner == owner)
            return;
        fullscreenVideoOwner = owner;
        ui::state("video", R"({"fullscreen":true})");
    }
    else if (fullscreenVideoOwner == owner)
    {
        fullscreenVideoOwner = nullptr;
        ui::state("video", "null");
    }
}
inline bool fullscreenVideo()
{
    return fullscreenVideoOwner != nullptr;
}
inline void publishCinematic(bool active)
{
    if (cinematicState == static_cast<int>(active))
        return;
    cinematicState = active;
    ui::cinematic(active);
#ifdef __ANDROID__
    CallActivity(upcall::GameActivity::showCinematic, [&](JNIEnv *env, jobject activity, jmethodID method) {
        env->CallVoidMethod(activity, method, static_cast<jboolean>(active));
        return true;
    });
#endif
}

// Widget actions still queued that are not applied: each is answered as refused.
inline void refuseActions(std::deque<storm::ui::Action> &queued, const char *reason)
{
    for (const auto &action : queued)
        ui::answer(action.request, false, reason);
    queued.clear();
}
inline void showMobileScreen(const std::string &name, double release_ms = 0.0, double load_ms = 0.0)
{
    auto screen = name;
    std::replace(screen.begin(), screen.end(), '\\', '/');
    screen = screen.substr(screen.find_last_of('/') + 1);
    const auto suffix = screen.find_last_of('.');
    if (suffix != std::string::npos)
        screen.resize(suffix);
    screen = AsciiLower(std::move(screen));
    mobileScreen = screen;
    if (startAtMainMenu && !screen.empty())
    {
        startAtMainMenu = false;
        spdlog::info("Start at the main menu: interface {} opened", screen);
    }
    ++mobileScreenRevision;
    refuseActions(uiWidgetActions,
                  "the interface changed before it was applied"); // queued for the interface that went away
    ui::screen(screen, mobileScreenRevision, release_ms, load_ms);
#ifdef __ANDROID__
    if (!CallActivity(upcall::GameActivity::showMobileScreen, [&](JNIEnv *env, jobject activity, jmethodID method) {
            const auto value = javaText(env, screen, "showMobileScreen");
            if (!value)
                return false;
            env->CallVoidMethod(activity, method, value);
            env->DeleteLocalRef(value);
            return true;
        }))
        spdlog::error("GameActivity.showMobileScreen failed: interface {} not reported", screen);
#endif
}

#ifdef __ANDROID__
// The messages of the activity's mailbox, in its order (GameActivity.deliverMobileActions calls a typed native for
// each; android_mailbox.hpp).
inline std::vector<mailbox::Message> pollMobileActions()
{
    std::vector<mailbox::Message> result;
    auto *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    auto activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity)
        return result;
    // Polled every engine frame: the method id is looked up once (the activity class ships with
    // this engine and stays loaded). Missing is an error.
    static const jmethodID method = [&] {
        const auto type = env->GetObjectClass(activity);
        const auto found = type ? env->GetMethodID(type, upcall::GameActivity::deliverMobileActions.name,
                                                   upcall::GameActivity::deliverMobileActions.signature)
                                : nullptr;
        if (!found)
        {
            if (env->ExceptionCheck())
                env->ExceptionClear();
            spdlog::error("GameActivity.deliverMobileActions()V is missing: no message of the activity reaches the "
                          "engine");
        }
        if (type)
            env->DeleteLocalRef(type);
        return found;
    }();
    if (method)
    {
        mailbox::delivering = &result;
        env->CallVoidMethod(activity, method);
        mailbox::delivering = nullptr;
    }
    if (env->ExceptionCheck())
    {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
    env->DeleteLocalRef(activity);
    return result;
}
#else
inline std::vector<mailbox::Message> pollMobileActions()
{
    return std::exchange(mailbox::pending, {});
}
#endif
} // namespace storm::android
#endif
