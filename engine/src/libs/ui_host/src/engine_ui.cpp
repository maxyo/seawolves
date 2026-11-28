#include "engine_ui.hpp"
#include "app_ui_names.hpp"
#include "platform/android_land_commands.hpp"
#include "platform/android_land_diagnostics.hpp"

namespace storm::android::ui
{
// The host's own lines: ui.log (its errors in error.log too) and the platform's log under kTag, like the UI library's
// lines (Config::on_log writes those to the same files; on Android the library puts them in logcat itself).
spdlog::logger &Log()
{
    static const std::shared_ptr<spdlog::logger> logger = [] {
        std::vector<spdlog::sink_ptr> sinks = storm::logging::fileSinks("ui");
        if (spdlog::sink_ptr platform = ui_platform::LogSink(kTag))
            sinks.push_back(std::move(platform));
        auto made = std::make_shared<spdlog::logger>("ui", sinks.begin(), sinks.end());
        made->flush_on(spdlog::level::warn);
        return made;
    }();
    return *logger;
}

// A debug trace switched on by its own name (platform/debug_switch.hpp): the platform's log (the desktop: ui.log).
std::shared_ptr<spdlog::logger> Trace(const char *tag)
{
    spdlog::sink_ptr platform = ui_platform::LogSink(tag);
    auto trace = platform ? std::make_shared<spdlog::logger>(tag, std::move(platform))
                          : std::make_shared<spdlog::logger>(tag, Log().sinks().begin(), Log().sinks().end());
    trace->set_level(spdlog::level::debug);
    return trace;
}
spdlog::logger &TraceStates()
{
    static const std::shared_ptr<spdlog::logger> trace = Trace("StormUiState");
    return *trace;
}
spdlog::logger &TraceInputs()
{
    static const std::shared_ptr<spdlog::logger> trace = Trace("StormUiInput");
    return *trace;
}

double MonotonicMs()
{
    // CLOCK_MONOTONIC on both platforms: the clock of Android's MotionEvent times (SystemClock.uptimeMillis).
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void EngineUi::Tick(uint64_t input_epoch)
{
    ++loop_;
    if (!host_ && !failed_)
        Start();
    if (deferred_)
        Interface(*std::exchange(deferred_, std::nullopt));
    std::vector<Step> work;
    size_t dropped = 0;
    std::optional<bool> live;
    {
        const std::lock_guard lock(inbox_mutex_);
        auto taken = inbox_.take(input_epoch);
        work = std::move(taken.steps);
        dropped = taken.dropped;
        live = std::exchange(live_, std::nullopt);
    }
    if (dropped)
        Log().info("{} touch samples dropped: sent before the input went off", dropped);
    if (live)
    {
        presentation_.live = *live;
        presentation_.live_known = true;
        PublishPresentation();
    }
    // The HUD's requests the engine answered since the last iteration (their numbers are this host's).
    const auto hud_answers = storm::android::hudAnswers.take();
    if (!host_)
        return; // nothing of it applies to a host that is not there (touches of no UI)
    for (auto &[request, applied, reason] : std::exchange(answers_, {}))
        host_->Answer(request, applied, reason);
    for (const auto &answer : hud_answers)
    {
        TraceInput("answer", "HUD " + std::to_string(answer.request) +
                                 (answer.applied ? " applied at " + std::to_string(answer.revision)
                                                 : " refused: " + answer.reason));
        host_->Answer(answer.request, answer.applied, answer.reason, answer.revision);
    }
    const double now_ms = MonotonicMs();
    for (Step &step : work)
    {
        if (step.call)
        {
            step.call(*host_);
            continue;
        }
        // With the trace every sample: where, and how long ago it happened (input latency).
        if (trace_input_)
            TraceInput("touch", std::to_string(step.phase) + " " + std::to_string(int(step.x)) + "," +
                                    std::to_string(int(step.y)) + " sampled at " +
                                    std::to_string(int64_t(step.time_ms)) + " ms, age " +
                                    std::to_string(int(now_ms - step.time_ms)) + " ms");
        if (step.phase == static_cast<int>(storm::ui::TouchPhase::Up))
            tap_loop_ = loop_;
        // The sample's own time on the host's clock: how long ago it happened. The moves of
        // this iteration are samples: each pointer is placed once, below, for this frame.
        const auto phase = static_cast<storm::ui::TouchPhase>(step.phase);
        host_->Touch(phase == storm::ui::TouchPhase::Move ? storm::ui::TouchPhase::Sample : phase, step.pointer, step.x,
                     step.y, host_->Now() - (now_ms - step.time_ms) / 1000.0);
    }
    host_->PlaceTouches(host_->Now());
    PublishInterfaceActive();
}

std::string EngineUi::ReadScriptList()
{
    std::vector<uint8_t> bytes;
    if (!Pack().Read("scripts.json", bytes))
        throw std::runtime_error("The UI pack has no scripts.json. Reinstall the app.");
    return {bytes.begin(), bytes.end()};
}

void EngineUi::ScriptsAttached(const std::vector<storm::ui::ScriptScreen> &screens)
{
    script_screens_ = screens;
    if (host_)
        host_->SetScriptScreens(script_screens_, &port_);
}

// The program's tables and static events are gone (a save loaded, the game ends): its screens go, and the touch
// menu's taps made for the old tables are revoked (missing presentation support must not close the stock menu).
void EngineUi::ScriptsReleased()
{
    script_screens_.clear();
    deferred_.reset();
    if (host_)
        host_->SetScriptScreens(script_screens_, &port_);
    storm::android::landCommands.releaseUnavailableSource(storm::android::LandCommandSource::Missing);
    storm::android::traceLandAdapter(0, "script-reset");
}

bool EngineUi::Scripted(const std::string &interface) const
{
    return std::ranges::any_of(script_screens_, [&](const auto &screen) { return screen.interface == interface; });
}

void EngineUi::Shutdown()
{
    storm::script::SetAttacher(nullptr);
    if (auto *owner = storm::render::FrameOwner::Get(); owner && backend_)
    {
        owner->RemoveClient(backend_.get());
        owner->RemoveClient(&prewarm_);
        if (trace_input_)
            owner->RemoveClient(&frame_trace_);
    }
    {
        const std::lock_guard lock(host_mutex_);
        host_.reset();
    }
    backend_.reset();
    files_.reset();
    ui_platform::ClosePack();
    failed_ = true; // no new host after this
}

void EngineUi::OnAction(const storm::ui::Action &action)
{
    using storm::ui::hud::Presence;
    using storm::ui::hud::Shortcut;
    if (action.command &&
        (std::holds_alternative<Shortcut>(*action.command) || std::holds_alternative<Presence>(*action.command)))
    {
        const auto *shortcut = std::get_if<Shortcut>(&*action.command);
        const std::string verb = shortcut ? app::hud_input::shortcut : app::hud_input::presence;
        const std::string value = shortcut ? shortcut->name : std::get<Presence>(*action.command).state;
        ui_platform::HudInput(verb, value);
        return;
    }
    if (storm::ui::AnsweredAction(action))
    {
        // A widget of the open interface: the action itself goes to the interface (XINTERFACE::DoControl
        // applies and answers it).
        in_action_ = true;
        TraceInput("action", action.screen + " " + action.node + " " + action.verb + " request " +
                                 std::to_string(action.request));
        if (action.screen == "dialogue") // the game's dialogue, not an interface: libs/dialog takes the reply
            storm::android::dialogueActions.push_back(action);
        else if (action.screen != screen_)
            Answer(action.request, false, "the interface " + action.screen + " is not open");
        else
            storm::android::uiWidgetActions.push_back(action);
        in_action_ = false;
        return;
    }
    if (trace_input_)
        TraceInput("action",
                   action.screen + " " + action.node + " " + action.verb + " -> " + storm::ui::to_string(action));
    if (action.command)
    {
        commands_.push(*action.command, action.node); // a node names a latest-value slot
        return;
    }
    if (std::string command = storm::ui::EngineCommand(action); !command.empty())
    {
        commands_.push(std::move(command), {});
        return;
    }
    ui_platform::Perform(action);
}

void EngineUi::Record(const storm::ui::InterfaceModel &model) const
{
    if (screen_.empty() || !record_models_)
        return;
    const std::string json = storm::ui::InterfaceModelJson(model);
    const std::filesystem::path folder = ui_platform::ModelsFolder();
    if (folder.empty())
        return;
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    std::ofstream out(folder / (screen_ + ".json"), std::ios::binary);
    out.write(json.data(), std::streamsize(json.size()));
    if (!out)
        Log().error("Cannot record the interface {} in {}", screen_, folder.string());
}

void state(std::string_view channel, std::string json)
{
    EngineUi::Get().State(channel, std::move(json));
}

void state(storm::ui::HudState state)
{
    EngineUi::Get().State(std::move(state));
}

void dialogueState(storm::ui::Dialogue dialogue)
{
    EngineUi::Get().DialogueState(std::move(dialogue));
}

void screen(const std::string &screen, uint64_t revision, double release_ms, double load_ms)
{
    EngineUi::Get().Screen(screen, revision, release_ms, load_ms);
}

void language(std::string game_language)
{
    EngineUi::Get().Language(std::move(game_language));
}

void interfaceGone()
{
    EngineUi::Get().InterfaceGone();
}

void interfaceModel(storm::ui::InterfaceModel &&model)
{
    EngineUi::Get().Interface(std::move(model));
}

void toast(std::string text, std::string target)
{
    EngineUi::Get().GameMessage(std::move(text), std::move(target));
}

void answer(uint64_t request, bool applied, std::string reason)
{
    EngineUi::Get().Answer(request, applied, std::move(reason));
}

void loading(bool active)
{
    EngineUi::Get().Loading(active);
}

void cinematic(bool active)
{
    EngineUi::Get().Cinematic(active);
}

void dialogue(bool active)
{
    EngineUi::Get().Dialogue(active);
}

void locationLoad(bool blocks)
{
    EngineUi::Get().LocationLoad(blocks);
}

void alert(std::string string_key)
{
    EngineUi::Get().AlertString(string_key);
}

void attachScripts()
{
    EngineUi::Get().AttachScripts();
}

void tick(uint64_t input_epoch)
{
    EngineUi::Get().Tick(input_epoch);
}

std::vector<Command> takeCommands()
{
    return EngineUi::Get().TakeCommands();
}

uint64_t generation()
{
    return EngineUi::Get().Generation();
}

void shutdown()
{
    EngineUi::Get().Shutdown();
}

} // namespace storm::android::ui
