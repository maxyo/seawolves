#pragma once
// The engine's UI host, one EngineUi on the game thread with the platform thread's inbox: ../README.md.
#include "dialogue_relay.hpp"
#include "scripts/script_port.hpp"
#include "v_file_service.h"
#include "loading_transaction.hpp"
#include "logging.hpp"
#include "platform/android_haptics.hpp"
#include "platform/android_runtime.hpp"
#include "platform/android_ui.hpp"
#include "platform/android_ui_sink.hpp"
#include "platform/debug_switch.hpp"
#include "platform/display_options.hpp"

#include "storm_ui/backend_bgfx.h"
#include "storm_ui/interface_model.h"
#include "storm_ui/ui.h"

#include <algorithm>
#include <storm/render/frame_owner.hpp>

#include <SDL2/SDL.h>
#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace storm::android::ui
{

constexpr const char *kTag = "StormUi";
// Frame owner client order: after the renderer's own clients.
constexpr int kClientOrder = 1000;
// Host::Prewarm, all of it, after a presented frame behind the loading screen of a scene load that has lasted
// kPrewarmAfterMs: the first load after the title (or of a new game) pays it once.
constexpr double kPrewarmAfterMs = 400.0;

// The host's own lines: ui.log (its errors in error.log too) and the platform's log under kTag.
spdlog::logger &Log();
// The debug traces switched on by their names (platform/debug_switch.hpp): the platform's log only.
spdlog::logger &TraceStates();
spdlog::logger &TraceInputs();
double MonotonicMs();

// What the touch interface asks of the platform it runs on: platform/android.cpp (the Java host) or
// platform/desktop.cpp (the desktop's development build). Game thread.
namespace ui_platform
{
// The platform can host the UI now (Android: the game thread has its JNIEnv).
bool Ready();
// The UI pack, read in place (the APK's assets, a directory on the desktop); thrown when there is none.
std::unique_ptr<storm::ui::FileSource> OpenPack();
void ClosePack();
void ShowTextEditor(const storm::ui::TextRequest &request);
// The touch interface cannot start: the player is told `reason`.
void UiFailed(const std::string &reason);
// A HUD shortcut or presence change (verb app::hud_input::*): the platform's own controls follow it.
void HudInput(const std::string &verb, const std::string &value);
// An action that is neither the engine's nor an interface's (about, settings, exit to the menu ...).
void Perform(const storm::ui::Action &action);
// Where recorded interface models go (StormUiModels); empty: nowhere.
std::filesystem::path ModelsFolder();
// The platform's log for the UI's lines under `tag` (logcat); nullptr: the files only.
spdlog::sink_ptr LogSink(const char *tag);
} // namespace ui_platform

class EngineUi
{
  public:
    static EngineUi &Get()
    {
        static EngineUi instance;
        return instance;
    }

    // ---- game thread -------------------------------------------------------------

    void State(std::string_view channel, std::string json)
    {
        if (trace_states_)
            TraceStates().debug("{} {}", channel, json);
        std::string name(channel);
        states_[name] = json; // replayed when a new host starts
        // A consumer may build a script screen (channel gameOver opens the death screen): a state published from
        // inside the game's script waits for the next Tick, between scripts, as a script screen's model does.
        if (const auto *program = scripts_.Running(); program && program->Executing())
        {
            if (std::find(held_states_.begin(), held_states_.end(), name) == held_states_.end())
                held_states_.push_back(std::move(name));
            return;
        }
        held_states_.erase(std::remove(held_states_.begin(), held_states_.end(), name), held_states_.end());
        if (host_)
            host_->SetState(name, json);
    }

    // A HUD channel's state: only a change reaches the host; the newest of each channel is replayed to a new one.
    void State(storm::ui::HudState state)
    {
        if (!hud_states_.keep(state))
            return;
        if (trace_states_)
            TraceStates().debug("{} changed", storm::ui::HudChannel(state));
        if (host_)
            host_->SetState(state);
    }

    void DialogueState(storm::ui::Dialogue dialogue)
    {
        if (trace_states_)
            TraceStates().debug("dialogue generation {}{}", dialogue.generation,
                                dialogue.refusal ? " refusal " + dialogue.refusal->error : std::string());
        dialogue_.Send(host_.get(), std::move(dialogue));
    }

    void Screen(const std::string &screen, uint64_t revision, double release_ms, double load_ms)
    {
        if (screen.empty())
        {
            if (!screen_.empty()) // not left already (Unloaded)
                Log().info("interface {} closed (revision {})", screen_, revision);
        }
        else // the game's part of a screen switch ("UI switch" follows with the UI's)
            Log().info("interface {} opened (revision {}) {} engine frames after the last tap, release {:.0f} ms, load "
                       "{:.0f} ms",
                       screen, revision, loop_ - tap_loop_, release_ms, load_ms);
        screen_ = screen;
        if (!screen.empty())
        {
            // Sent from the interface's own script (its init function): the segment that has to stay loaded for it.
            const auto *program = scripts_.Running();
            segment_ = program ? program->RunningSegment() : std::string();
            closing_ = false;
            presentation_.screen = screen;
            PublishPresentation();
            // Its model comes once the interface has loaded, seconds later at times: the UI
            // owns the touches from now and holds them for the screen (Host::ExpectInterface).
            if (host_)
                host_->ExpectInterface();
            return;
        }
        // Interface screens close one menu some frames before they open the next: the closed one stays until the
        // game says nothing follows it (InterfaceGone), so the game does not flash between them.
        segment_.clear();
        closing_ = true;
    }

    // The open interface's script unloaded the segment that opened it: the interface has ended for the game (its event
    // handlers are gone; the next one, if any, opens some frames later). Its models and actions are a closed
    // interface's from now, and its screen stays as drawn without being built again (Host::InterfaceLeft).
    void Unloaded(const std::string &segment)
    {
        if (segment_.empty() || segment != segment_)
            return;
        Log().info("interface {} left: its script unloaded {}", screen_, segment_);
        segment_.clear();
        screen_.clear();
        closing_ = true;
        if (host_)
            host_->InterfaceLeft();
    }

    void Language(std::string game_language)
    {
        language_ = std::move(game_language);
        if (host_)
            host_->SetLanguage(language_);
    }

    void InterfaceGone()
    {
        if (closing_)
            CloseNow();
    }

    void Interface(storm::ui::InterfaceModel &&model)
    {
        if (screen_.empty())
            return; // a late model of a screen that closed
        // A script screen is built by a script: a model published from inside the game's script waits for the next
        // Tick, between scripts.
        if (const auto *program = scripts_.Running(); program && program->Executing() && Scripted(model.screen))
        {
            deferred_ = std::move(model);
            return;
        }
        Record(model);
        TraceInput("model", screen_ + " revision " + std::to_string(model.revision) + ", " +
                                std::to_string(model.widgets.size()) + " widgets");
        if (host_)
            host_->SetInterface(model);
        interface_ = std::move(model);
        PublishInterfaceActive();
    }

    void GameMessage(std::string text, std::string target)
    {
        if (host_)
            host_->GameMessage(text, target);
        else
            Log().error("no UI host yet, game message not shown: {}", text);
    }

    // The engine's answer to a widget action. One the engine gives while the UI is still handling the
    // event that sent the action (OnAction) waits for the next Tick: the press has no busy mark yet.
    void Answer(uint64_t request, bool applied, std::string reason)
    {
        if (!request)
            return;
        TraceInput("answer", std::to_string(request) + (applied ? " applied" : " refused: " + reason));
        if (in_action_ || !host_)
        {
            answers_.push_back({request, applied, std::move(reason)});
            return;
        }
        host_->Answer(request, applied, reason);
    }

    void Loading(bool active)
    {
        // A load starts (the loading picture comes next, then the game thread blocks in it): no menu follows the one
        // that just closed, so it goes now, not when the blocked loop comes round again after the load's first
        // frames (the title over the loading picture after «Продолжить»).
        if (active && closing_)
        {
            Log().info("interface closed at the start of a load");
            CloseNow();
        }
        Set(presentation_.loading, active);
    }
    void Cinematic(bool active)
    {
        Set(presentation_.cinematic, active);
    }
    void Dialogue(bool active)
    {
        Set(presentation_.dialogue, active);
    }
    void LocationLoad(bool blocks)
    {
        Set(presentation_.location_load, blocks);
    }

    // Nothing follows the closed screen (InterfaceGone) or a load starts (Loading): the interface goes.
    void CloseNow()
    {
        closing_ = false;
        interface_.reset();
        if (host_)
            host_->CloseInterface();
        PublishInterfaceActive();
        presentation_.screen.clear(); // only now: a menu that follows the closed one keeps it
        PublishPresentation();
    }

    void AlertString(const std::string &key)
    {
        if (host_)
            host_->AlertString(key);
        else
            Log().error("no UI host yet, alert not shown: {}", key);
    }

    void Tick(uint64_t input_epoch);

    // switch StormUiInput (platform/debug_switch.hpp): when touches and
    // the commands they produce reach the engine, in engine loop iterations (one per frame).
    void TraceInput(const char *what, const std::string &detail) const
    {
        if (trace_input_)
            TraceInputs().debug("loop {}: {} {}", loop_, what, detail);
    }

    std::vector<storm::android::ui::Command> TakeCommands()
    {
        return commands_.take();
    }

    uint64_t Generation() const
    {
        return generation_;
    }

    void Shutdown();
    // The touch interface's scripts are attached to every program the game runs from now on.
    void AttachScripts()
    {
        storm::script::SetAttacher(&scripts_);
    }

    // ---- platform thread (EngineUi natives) ---------------------------------------

    void Post(std::function<void(storm::ui::Host &)> call)
    {
        const std::lock_guard lock(inbox_mutex_);
        inbox_.push({std::move(call)});
    }

    // A pointer sample with its time (MotionEvent's, milliseconds of CLOCK_MONOTONIC). Every
    // sample is kept, the historical ones of a batched move too: drags and flings measure the
    // finger's speed from them (storm::ui TouchScroll).
    void Touch(int phase, int pointer, float x, float y, double time_ms)
    {
        const std::lock_guard lock(inbox_mutex_);
        inbox_.push({{}, phase, pointer, x, y, time_ms});
    }

    // Answered from the layout of the last frame (Host is thread-safe here).
    bool WantsTouch(float x, float y)
    {
        const std::lock_guard lock(host_mutex_);
        return host_ && host_->WantsTouch(x, y);
    }

    bool WantsBack()
    {
        const std::lock_guard lock(host_mutex_);
        return host_ && host_->WantsBack();
    }

    bool InterfaceActive() const
    {
        return interface_active_.load();
    }

    // A channel Android writes ("presentation", "appSettings"), from the activity's thread. It is
    // kept as well as posted: the activity writes some before the engine has a host (the settings
    // at start), and what is posted to no host is dropped; a new host gets the latest of each.
    void PlatformState(std::string channel, std::string json);

    // «Размер интерфейса» and «Прозрачность кнопок» of the app's settings (platform/display_options.hpp), to the host.
    void DisplayOptionsFrom(const std::string &app_settings);

    static void ApplyDisplayOptions(storm::ui::Host &host, const storm::android::DisplayOptions &options)
    {
        host.SetDisplayOptions(static_cast<float>(options.ui_scale) / 100.0f,
                               1.0f - static_cast<float>(options.hud_transparency) / 100.0f, options.left_handed != 0);
    }

    // Safe-area insets in pixels; the host takes dp.
    void SafeArea(float left, float top, float right, float bottom)
    {
        const std::lock_guard lock(inbox_mutex_);
        safe_area_ = {left, top, right, bottom};
        has_safe_area_ = true;
        inbox_.push(Step{SafeAreaStep()});
    }

    // Applies safe_area_ (guarded by inbox_mutex_) in dp.
    std::function<void(storm::ui::Host &)> SafeAreaStep() const
    {
        return [area = safe_area_](storm::ui::Host &host) {
            const auto *owner = storm::render::FrameOwner::Get();
            const float density = owner && owner->Surface().density > 0.0f ? owner->Surface().density : 1.0f;
            host.SetSafeArea(area[0] / density, area[1] / density, area[2] / density, area[3] / density);
        };
    }

  private:
    void Start();
    // The UI pack in the activity's assets, opened once (the scripts' list is read before the host starts).
    storm::ui::FileSource &Pack();
    std::string ReadScriptList();
    void ScriptsAttached(const std::vector<storm::ui::ScriptScreen> &screens);
    void ScriptsReleased();
    bool Scripted(const std::string &interface) const;
    storm::ui::Config MakeConfig(const storm::render::SurfaceMetrics &surface);
    void ReportStartFailure(const std::string &error);
    void AttachToFrames(storm::render::FrameOwner &owner);
    void ReplayToHost();

    // What the UI asks for: engine commands stay in this process (read at the top of the next
    // loop iteration, or this one for input applied at its top); what is Android's goes to
    // the activity.
    void OnAction(const storm::ui::Action &action);

    // State channel "presentation" (hud::Presentation), on a change, once Android has said whether the
    // game is live.
    void Set(bool &field, bool value)
    {
        if (field == value)
            return;
        field = value;
        PublishPresentation();
    }

    void PublishPresentation()
    {
        if (!presentation_.live_known)
            return;
        State(storm::ui::hud::Presentation{true, presentation_.screen,
                                           presentation_.loading || presentation_.location_load,
                                           presentation_.cinematic, presentation_.dialogue, presentation_.live});
    }

    void PublishInterfaceActive()
    {
        interface_active_ = host_ && host_->InterfaceActive();
    }

    // switch StormUiModels: the newest model of each screen goes to
    // <external files>/ui-models/<screen>.json, to record real models for the screen tests.
    void Record(const storm::ui::InterfaceModel &model) const;

    std::unique_ptr<storm::ui::FileSource> files_;
    storm::ui_scripts::Attacher scripts_{
        {[this] { return ReadScriptList(); },
         [](const std::string &path) { return fio->_FileOrDirectoryExists(path.c_str()); },
         [this](const auto &screens) { ScriptsAttached(screens); }, [this] { ScriptsReleased(); },
         [this](const std::string &segment) { Unloaded(segment); }}};
    storm::ui_scripts::VmPort port_{scripts_};
    std::vector<storm::ui::ScriptScreen> script_screens_; // of the running program (replayed to a new host)
    std::optional<storm::ui::InterfaceModel> deferred_;   // a script screen's model published inside a script
    std::vector<std::string> held_states_;                // channels whose state was published inside a script
    std::unique_ptr<storm::ui::BgfxBackend> backend_;
    std::unique_ptr<storm::ui::Host> host_;
    std::mutex host_mutex_; // the host's lifetime against the platform thread's queries
    bool failed_ = false;
    uint64_t generation_ = 0;
    uint64_t loop_ = 0;     // engine loop iterations (Tick)
    uint64_t tap_loop_ = 0; // the iteration that applied the newest touch end
    std::string segment_;   // the script segment that opened the interface open now ("" unknown)
    // The debug log tags (Loggable), read once.
    const bool trace_input_ = storm::platform::debugSwitch("StormUiInput");
    const bool trace_states_ = storm::platform::debugSwitch("StormUiState");
    const bool record_models_ = storm::platform::debugSwitch("StormUiModels");
    // With the input trace: which loop iteration each presented UI frame belongs to, and every
    // 5 s what the UI frames cost on the CPU (GPU work is the renderer's, in RenderMetrics).
    struct FrameTrace final : storm::render::FrameClient
    {
        void OnUiRender(const storm::render::FrameContext &frame) override;
        uint32_t since = 0, frames = 0, made = 0;
        uint64_t counted = 0; // FrameStats::number of the last frame summed
        double made_ms = 0.0; // when the last made frame was traced
        double update_ms = 0.0, render_ms = 0.0;
        long draws = 0, layers = 0, layouts = 0, touched = 0, boxes = 0, meshes = 0;
    } frame_trace_;
    // What the first opening of a screen would make, made behind a loading screen (Host::Prewarm, kPrewarmAfterMs).
    struct Prewarmer final : storm::render::FrameClient
    {
        void OnFrameEnd(const storm::render::FrameContext &) override;
        std::optional<std::chrono::steady_clock::time_point> load_started; // of the scene load under way
    } prewarm_;
    std::map<std::string, std::string> states_;
    DialogueRelay dialogue_;
    std::map<std::string, std::string> platform_states_; // guarded by inbox_mutex_
    std::string screen_;
    std::string language_; // the game's, once it is set (a new host takes it)
    // Channel "presentation": the engine's facts as they change, `live` from the activity.
    struct
    {
        std::string screen;
        bool loading = true, cinematic = false, dialogue = false, live = false;
        bool location_load = false; // a location load blocks the game: loading for the UI
        bool live_known = false;
    } presentation_;
    storm::android::HudStates hud_states_;
    std::optional<bool> live_;                           // from the platform thread, guarded by inbox_mutex_
    std::optional<storm::ui::InterfaceModel> interface_; // the open interface's newest model (replayed to a new host)
    bool closing_ = false; // the game closed its interface; the UI still draws it (InterfaceGone)
    std::atomic<bool> interface_active_{false};
    storm::android::CommandQueue<storm::android::ui::Command> commands_;
    bool in_action_ = false; // inside OnAction: answers wait for the next Tick
    struct PendingAnswer
    {
        uint64_t request;
        bool applied;
        std::string reason;
    };
    std::vector<PendingAnswer> answers_;
    // What the platform thread sent: a call, or (no call) a pointer step.
    struct Step
    {
        std::function<void(storm::ui::Host &)> call;
        int phase = 0, pointer = 0;
        float x = 0.0f, y = 0.0f;
        double time_ms = 0.0;                              // CLOCK_MONOTONIC (SystemClock.uptimeMillis)
        uint64_t epoch = storm::android::inputEpoch.now(); // the input epoch it was sent in
        bool touch() const
        {
            return !call;
        }
    };
    std::mutex inbox_mutex_;
    storm::android::UiInbox<Step> inbox_;
    std::array<float, 4> safe_area_{};
    bool has_safe_area_ = false;
    int shown_ui_scale_ = -1, shown_hud_transparency_ = -1, shown_left_handed_ = -1,
        shown_haptics_ = -1; // the display options last logged
};

} // namespace storm::android::ui
