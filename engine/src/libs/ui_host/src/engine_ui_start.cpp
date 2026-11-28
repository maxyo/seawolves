#include "engine_ui.hpp"

namespace storm::android::ui
{
// Once the renderer's device is ready: a host on the activity's assets, attached to the frames, given everything the
// engine and the platform said before it was there.
void EngineUi::Start()
{
    auto *owner = storm::render::FrameOwner::Get();
    if (!owner || !owner->DeviceReady())
        return; // not yet: the renderer starts first
    if (!ui_platform::Ready())
        return;
    std::string error;
    storm::ui::BgfxBackendOptions options;
    options.owner = owner;
    backend_ = storm::ui::BgfxBackend::Create(options, &error);
    const storm::render::SurfaceMetrics surface = owner->Surface();
    const storm::ui::Config config = MakeConfig(surface);
    std::unique_ptr<storm::ui::Host> host = backend_ ? storm::ui::Host::Create(config, &error) : nullptr;
    if (!host)
    {
        ReportStartFailure(error);
        return;
    }
    {
        const std::lock_guard lock(host_mutex_);
        host_ = std::move(host);
    }
    ++generation_;
    AttachToFrames(*owner);
    ReplayToHost();
    Log().info("UI host started in the engine frame ({}x{}, density {:.2f})", surface.width_px, surface.height_px,
               surface.density);
}

storm::ui::FileSource &EngineUi::Pack()
{
    if (!files_)
        files_ = ui_platform::OpenPack();
    return *files_;
}

storm::ui::Config EngineUi::MakeConfig(const storm::render::SurfaceMetrics &surface)
{
    storm::ui::Config config;
    config.files = &Pack();
    config.backend = backend_.get();
    config.width = int(surface.width_px);
    config.height = int(surface.height_px);
    config.density = surface.density;
    config.game_root = std::filesystem::current_path().string(); // prepareGameDirectory
    config.show_errors = storm::android::debugBuild();
    // Automated UI runs: the tap map in the log (Config::log_taps).
    config.log_taps = storm::platform::debugSwitch("StormUiTaps");
    // The flicker check (Config::trace_actions): debug builds and automated UI runs report every action that showed
    // a transient frame ("UI flicker" in logcat and Logs/ui.log).
    config.trace_actions = config.show_errors || config.log_taps;
    // The UI's lines (what it showed, its errors) in Logs/ui.log, its errors in error.log too; logcat has them
    // from the UI already (tag StormUi).
    config.on_log = [logger = storm::logging::getOrCreateLogger("ui", spdlog::level::trace, false)](
                        storm::ui::LogLevel level, const std::string &line) {
        logger->log(level == storm::ui::LogLevel::Error     ? spdlog::level::err
                    : level == storm::ui::LogLevel::Warning ? spdlog::level::warn
                                                            : spdlog::level::info,
                    "{}", line);
    };
    config.on_action = [this](const storm::ui::Action &action) { OnAction(action); };
    config.on_text_request = [](const storm::ui::TextRequest &request) { ui_platform::ShowTextEditor(request); };
    return config;
}

// No other renderer exists for the game's screens: the player is told, and no host is tried again.
void EngineUi::ReportStartFailure(const std::string &error)
{
    failed_ = true;
    Log().error("UI host failed: {}", error);
    ui_platform::UiFailed("The UI runtime failed to start: " + error);
    backend_.reset();
}

void EngineUi::AttachToFrames(storm::render::FrameOwner &owner)
{
    if (trace_input_)
        owner.AddClient(&frame_trace_, kClientOrder + 1);
    backend_->AttachHost(host_.get());
    owner.AddClient(backend_.get(),
                    kClientOrder); // OnSurfaceChanged: the host lays out at the surface's size and density
    owner.AddClient(&prewarm_, kClientOrder + 2);
}

// The language, the states of the engine and the platform, the open interface and the safe area, as the last host had
// them.
void EngineUi::ReplayToHost()
{
    if (!language_.empty())
        host_->SetLanguage(language_);
    for (const auto &[channel, json] : states_)
        host_->SetState(channel, json);
    dialogue_.Replay(*host_);
    hud_states_.replay([&](const storm::ui::HudState &state) { host_->SetState(state); });
    std::map<std::string, std::string> platform;
    {
        const std::lock_guard lock(inbox_mutex_);
        platform = platform_states_;
    }
    for (const auto &[channel, json] : platform)
        host_->SetState(channel, json);
    if (const auto settings = platform.find("appSettings"); settings != platform.end())
    {
        ApplyDisplayOptions(*host_, storm::android::ReadDisplayOptions(settings->second));
    }
    host_->SetScriptScreens(script_screens_, &port_);
    if (interface_)
        host_->SetInterface(*interface_);
    else if (!screen_.empty())
        host_->ExpectInterface();
    {
        const std::lock_guard lock(inbox_mutex_);
        if (has_safe_area_)
            inbox_.pushFront(Step{SafeAreaStep()});
    }
}
} // namespace storm::android::ui
