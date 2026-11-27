#include "storm/render/pipelines.hpp"
#include "storm/render/loading_schedule.hpp"
#include <chrono>
#include <exception>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include <SDL2/SDL.h>
// AddressSanitizer must see every C++ allocation: an ASan build keeps the default new/delete.
#if defined(__SANITIZE_ADDRESS__)
#define STORM_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define STORM_ASAN 1
#endif
#endif
#if !defined(__ANDROID__) && !defined(STORM_ASAN)
#include <mimalloc-new-delete.h>
#endif
#ifndef __ANDROID__ // Android allocates with Bionic and libc++ (cmake/android.cmake): no mimalloc there
#include <mimalloc.h>
#endif
#include <spdlog/spdlog.h>

#include "core_private.h"
#include "lifecycle_diagnostics_service.hpp"
#include "logging.hpp"
#include "log_rotation.hpp"
#include "stall_watchdog.hpp"
#include <spdlog/details/os.h>
#include "os_window.hpp"
#include "app_events.hpp"
#include "storm/render/effects.hpp"
#include "v_sound_service.h"
#include "animation.h"
#include "fs.h"
#include "platform/frame_work.hpp"
#include "platform/stall_watch.hpp"
#include "platform/host_hooks.hpp"
#include "platform/script_patches.hpp"
#include "profiler.hpp"
#ifdef STORM_TRACY
#include <common/TracyVersion.hpp>
#endif
#include "watermark.hpp"
#include "engine_run.hpp"
#ifdef __ANDROID__
#include <android/log.h>
#include <unistd.h>
#include "android_game.hpp"
#include "platform/android_ui.hpp"
#else
#include "platform/desktop_settings.hpp"
#endif
#include <cstdio>
#include <functional>

using namespace storm::engine;
#ifndef __ANDROID__
namespace storm::engine
{
class AndroidGame;
}
#endif

#ifdef __ANDROID__
// GameActivity's bounded exit wait polls this from the UI thread; see host_hooks.hpp.
extern "C" JNIEXPORT jboolean JNICALL Java_org_stormengine_port_GameActivity_nativeSaveInProgress(JNIEnv *, jclass)
{
    return storm::platform::saveInProgress() ? JNI_TRUE : JNI_FALSE;
}
#endif

namespace
{

constexpr char defaultLoggerName[] = "system";

storm::diag::LifecycleDiagnosticsService lifecycleDiagnostics;


// The running session's marker (log_rotation.hpp SessionMarker): from the start to a clean exit. The game thread
// beats it every frame. On Android the activity marks the background and the foreground from its UI thread
// (GameActivity.onPause/onResume -> nativeLifecycle): SDL raises SDL_APP_WILLENTERBACKGROUND only inside
// Android_PumpEvents on the game thread, which a stuck game never reaches, so a regular stop behind a busy game
// thread would count as a hang. The next start tells a hang the player swiped away. Created once, never freed: the
// UI thread may call it until the process ends.
std::atomic<storm::logging::SessionMarker *> sessionMarker{nullptr};

void SessionMarkerResult(const std::string &failure)
{
    if (!failure.empty())
        spdlog::error("Session marker: {}", failure);
}

void RunFrame()
{
    if (!core_private->Run())
    {
        isRunning = false;
    }
    SessionMarkerResult(sessionMarker.load()->Beat(std::time(nullptr)));
}

// Every dialog of the engine is logged first (an error one at error level: error.log too), so what the player saw
// is in the logs.
void ShowEngineDialog(Uint32 flags, const char *title, const char *text)
{
    spdlog::log(flags & SDL_MESSAGEBOX_ERROR ? spdlog::level::err : spdlog::level::warn, "dialog \"{}\": {}", title,
                text);
    if (SDL_ShowSimpleMessageBox(flags, title, text, nullptr) != 0)
        spdlog::error("cannot show the dialog \"{}\": {}", title, SDL_GetError());
}

// Last line of defence for exceptions thrown out of a frame: log, flush, tell the player, and let main() shut the
// engine down in order.
bool fatalError = false;

void ReportFatalError(const char *what)
{
    fatalError = true;
    isRunning = false;
    spdlog::critical("Unhandled exception in the frame loop: {}", what);
    if (auto logger = spdlog::default_logger())
        logger->flush();
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Engine stopped: %s", what);
    // SDL's Android bridge shows a Java dialog and waits for dismissal
    // before SDL_main returns and the game Activity is finished.
    // SDL's Android default TextView colors can be dark on a dark
    // Activity theme, so explicitly style this fatal error message.
    const SDL_MessageBoxColorScheme colors{{
        {24, 24, 24}, {255, 255, 255}, {128, 128, 128}, {64, 64, 64}, {96, 96, 96}}};
    const SDL_MessageBoxButtonData button{
        SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "OK"};
    const SDL_MessageBoxData dialog{SDL_MESSAGEBOX_ERROR, nullptr, "Game stopped", what, 1, &button, &colors};
    SDL_ShowMessageBox(&dialog, nullptr);
}

// Event watches (window, lifecycle) run inside SDL's C functions, some with SDL's watcher lock
// held: an exception must not unwind through them, and the fatal-error dialog must not be shown
// from there. A handler called from SDL runs its body through InsideSdlCallback, which keeps
// the first exception; the main loop rethrows it right after the SDL call returned, into the
// loop's catch (ReportFatalError).
std::mutex callbackErrorMutex;
std::exception_ptr callbackError;

std::string Describe(const std::exception_ptr &error)
{
    try
    {
        std::rethrow_exception(error);
    }
    catch (const std::exception &exception)
    {
        return exception.what();
    }
    catch (...)
    {
        return "unknown error";
    }
}

template <typename Body> void InsideSdlCallback(const char *what, Body &&body)
{
    try
    {
        body();
    }
    catch (...)
    {
        const auto error = std::current_exception();
        spdlog::critical("Exception in the SDL callback '{}': {}; the game stops after the SDL call returns", what,
                         Describe(error));
        std::lock_guard lock(callbackErrorMutex);
        if (!callbackError)
            callbackError = error;
    }
}

void RethrowCallbackError()
{
    std::exception_ptr error;
    {
        std::lock_guard lock(callbackErrorMutex);
        error = std::exchange(callbackError, nullptr);
    }
    if (error)
        std::rethrow_exception(error);
}

// Logs what the script patch layer did during the resource scan and, in Android debug builds, once per distinct set
// of unapplied fixes, shows which fixes the game files did not allow.
void AnnounceScriptPatches()
{
    const auto &report = storm::script_patches::lastReport();
    for (const auto &line : report.info)
        spdlog::info("{}", line);
    for (const auto &line : report.errors)
        spdlog::error("{}", line);
#ifdef __ANDROID__
    const auto key = report.noticeKey();
    if (key.empty() || !storm::android::debugBuild()) // release: the unapplied fixes are in the log above
        return;
    const auto stamp = std::filesystem::path(SDL_AndroidGetInternalStoragePath()) / "script-patches/notice-shown";
    std::string shown;
    if (std::ifstream file(stamp); file)
        std::getline(file, shown);
    if (shown == key)
        return;
    bool russian = false;
    if (SDL_Locale *locales = SDL_GetPreferredLocales())
    {
        russian = locales[0].language && std::string_view(locales[0].language) == "ru";
        SDL_free(locales);
    }
    const auto text = storm::script_patches::noticeText(report, russian);
    const SDL_MessageBoxButtonData button{
        SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "OK"};
    const SDL_MessageBoxData dialog{SDL_MESSAGEBOX_WARNING, nullptr,
                                    russian ? "Исправления не применены" : "Fixes not applied", text.c_str(), 1,
                                    &button, nullptr};
    spdlog::warn("dialog \"{}\": {}", dialog.title, dialog.message);
    if (SDL_ShowMessageBox(&dialog, nullptr) != 0)
    {
        spdlog::error("Cannot show the script patch notice: {}", SDL_GetError());
        return; // shown again next start
    }
    std::ofstream file(stamp, std::ios::trunc);
    file << key << '\n';
    if (!file)
        spdlog::error("Cannot record the script patch notice in {}", stamp.string());
#endif
}

#ifndef __ANDROID__
void mimalloc_fun(const char *msg, void *arg)
{
    static std::filesystem::path mimalloc_log_path;
    if (mimalloc_log_path.empty())
    {
        mimalloc_log_path = fs::GetLogsPath() / "mimalloc.log";
        std::error_code ec;
        remove(mimalloc_log_path, ec);
    }

    FILE *mimalloc_log =
        fopen(mimalloc_log_path.c_str(), "a+b");
    if (mimalloc_log != nullptr)
    {
        fputs(msg, mimalloc_log);
        fclose(mimalloc_log);
    }
}
#endif

} // namespace

#ifdef __ANDROID__
// GameActivity.onPause (true) / onResume (false), on the UI thread: the session marker's background state.
extern "C" JNIEXPORT void JNICALL Java_org_stormengine_port_GameActivity_nativeLifecycle(JNIEnv *, jclass, jboolean background)
{
    storm::platform::stall_watch::Background(background); // SDL pauses the game thread there: no stall
    auto *marker = sessionMarker.load();
    if (!marker)
        return; // before the engine started its logs: nothing to mark
    const auto failure = background ? marker->Background(std::time(nullptr)) : marker->Foreground(std::time(nullptr));
    if (!failure.empty())
        spdlog::error("Session marker: {}", failure);
}
#endif

// Engine-side reactions to application events. Other subsystems subscribe to
// storm::AppEvents themselves; see src/libs/window/README.md.
void SubscribeEngineToAppEvents()
{
    auto &events = storm::AppEvents::instance();
    // Early: pause audio and tell scripts inside the SDL call that announces the
    // pause, before anything else can run.
    events.subscribe(
        [](const storm::AppEvent &event) { InsideSdlCallback("early application event", [&] {
            if (event.type == storm::AppEventType::WillEnterBackground)
            {
                audioGate.foreground = false;
                audioGate.apply();
                if (event.immediate && core_private && core_private->initialized() &&
                    !storm::platform::LoadingPlatformTransactionActive())
                {
                    // Hook for scripts (the app's background autosave). Runs between frames on
                    // the game thread. This handler runs before the renderer's (early handlers are
                    // called in subscription order), so the scripts, and the save thumbnail, still
                    // see a live surface; the renderer stops presenting right after. The Android
                    // UI thread waits in surfaceDestroyed only for a running bgfx frame.
                    const auto started = std::chrono::steady_clock::now();
                    core_private->Event("AppWillEnterBackground");
                    spdlog::info("AppWillEnterBackground handlers took {} ms",
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - started).count());
                }
            }
            if (event.type == storm::AppEventType::WillEnterBackground ||
                event.type == storm::AppEventType::Terminating)
            {
                // A backgrounded process can be killed without notice: do not lose buffered log lines.
                spdlog::apply_all([](const std::shared_ptr<spdlog::logger> &logger) { logger->flush(); });
            }
        }); },
        true);
    events.subscribe([](const storm::AppEvent &event) {
        const bool ready = core_private && core_private->initialized();
        const bool scripts = ready && !storm::platform::LoadingPlatformTransactionActive();
        switch (event.type)
        {
        case storm::AppEventType::DidEnterForeground:
            audioGate.foreground = true;
            audioGate.apply();
            if (scripts)
                core_private->Event("AppDidEnterForeground");
            break;
        case storm::AppEventType::LowMemory:
            if (ready)
            {
                uint64_t released = 0;
                if (auto *soundService = static_cast<VSoundService *>(core.GetService("SoundService")))
                    released = soundService->TrimCaches();
                spdlog::warn("Low memory: released {} KiB of cached audio; the relief models go after their texture",
                             released / 1024);
                if (auto *animationService = static_cast<AnimationService *>(core.GetService("AnimationServiceImp")))
                    spdlog::warn("Low memory: released {} unused animations", animationService->TrimCaches());
                storm::render::TrimRelief();
                storm::render::ReleaseIdleRenderTargets();
                if (scripts) core_private->Event("AppLowMemory");
            }
            break;
        case storm::AppEventType::WindowResized:
            spdlog::info("Window resized to {}x{}", event.width, event.height);
            if (scripts)
                core_private->Event("AppWindowResized", "ll", event.width, event.height);
            break;
        default:
            break;
        }
    });
}

void HandleWindowEventInSdl(const storm::OSWindow::Event &event)
{
    if (event == storm::OSWindow::Closed)
    {
        isRunning = false;
        if (core_private->initialized() && !storm::platform::LoadingPlatformTransactionActive())
        {
            core_private->Event("DestroyWindow");
        }
    }
    else if (event == storm::OSWindow::FocusGained)
    {
        bActive = true;
        audioGate.windowFocused = true;
        if (core_private->initialized())
        {
            core_private->AppState(bActive);
            audioGate.apply();
        }
    }
    else if (event == storm::OSWindow::FocusLost)
    {
        bActive = false;
        audioGate.windowFocused = false;
        if (core_private->initialized())
        {
            core_private->AppState(bActive);
            audioGate.apply();
        }
    }
}

// Called from SDL's window event watch (sdl_window.cpp).
void HandleWindowEvent(const storm::OSWindow::Event &event)
{
    InsideSdlCallback("window event", [&] { HandleWindowEventInSdl(event); });
}

namespace
{
#ifdef __ANDROID__
// The engine runs once per process: its globals and statics (services, script state, ids of renderer objects) hold
// one run. The app ends the game process when main returns (GameActivity.onDestroy), so the next start is a fresh
// process; entering it again in the same process would run on the previous run's state.
void EnterOnce()
{
    static bool entered = false;
    if (entered)
        __android_log_assert(nullptr, "StormEngine",
                             "SDL_main entered a second time in one process: the engine runs once per process");
    entered = true;
}
#endif

// The logs and the session marker, first, so that every later failure (the game folder, damaged fixes) is in the
// logs. Before anything opens a log (mimalloc's output, the loggers truncate their files): the previous process's
// logs are the evidence of the hang or crash this start may follow. False (shown to the player): no logs.
bool StartLogs()
{
    try
    {
        const auto logs = fs::GetLogsPath();
        std::vector<std::string> logFailures;
        const auto uncleanEnd = storm::logging::takeUncleanEnd(logs, logFailures);
        for (const auto &failure : storm::logging::rotatePreviousSessionLogs(logs))
            logFailures.push_back(failure);
        spdlog::set_default_logger(storm::logging::getOrCreateLogger(defaultLoggerName));
        storm::logging::startPeriodicFlush();
        // The diagnostics export labels each session's logs with this line's time and pid (app DiagnosticsReport).
        spdlog::info("Logging system initialized. Running on {}, pid {}", storm::kBuildWatermark,
                     spdlog::details::os::pid());
        for (const auto &failure : logFailures)
            spdlog::error("Previous session logs: {}", failure);
        // The export shows this line with the session it is about (app DiagnosticsReport).
        if (!uncleanEnd.empty())
            spdlog::error("Previous session ended without a clean exit: {}", uncleanEnd);
        auto *marker = new storm::logging::SessionMarker(logs, storm::logging::logTime(std::time(nullptr)),
                                                         static_cast<long>(spdlog::details::os::pid()));
        SessionMarkerResult(marker->Start(std::time(nullptr)));
        sessionMarker = marker;
        return true;
    }
    catch (const std::exception &error)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Logs unavailable: %s", error.what()); // no log file to write
        ShowEngineDialog(SDL_MESSAGEBOX_ERROR, "Logs unavailable", error.what());
        return false;
    }
}

// The command line: on desktop `--game <folder>` names the game folder (remembered for the next start) and `--folders`
// prints the folders the engine uses for it and exits. Android starts the engine without arguments.
struct CommandLine
{
    std::filesystem::path game;
    bool printFolders = false;
};

#ifndef __ANDROID__
bool ParseCommandLine(int argc, char *argv[], CommandLine &command)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view argument = argv[i];
        if (argument == "--game" && i + 1 < argc)
            command.game = storm::storage::utf8Path(argv[++i]);
        else if (argument == "--folders")
            command.printFolders = true;
        else
        {
            std::fprintf(stderr, "Unknown argument '%s'. Usage: %s [--game <game folder>] [--folders]\n", argv[i],
                         argv[0]);
            return false;
        }
    }
    return true;
}

// The folder the engine is installed in: the layers it lays over the game are there.
std::filesystem::path EngineFolder()
{
    char *base = SDL_GetBasePath();
    if (!base)
        throw std::runtime_error(std::string("Cannot find the engine's folder: ") + SDL_GetError());
    const auto folder = storm::storage::utf8Path(base);
    SDL_free(base);
    return folder;
}

// The game folder named on the command line, else the one named last, and the player's folders for it. `remember`: a
// game folder named on the command line is the one the next start runs.
storm::storage::Folders DesktopFolders(const CommandLine &command, bool remember)
{
    const auto base = storm::desktop::baseFolders([](const char *name) -> const char * { return std::getenv(name); });
    const auto settings = storm::desktop::settingsFile(base);
    auto game = command.game.empty() ? storm::desktop::rememberedGame(settings) : command.game;
    if (game.empty())
        throw std::runtime_error("No game folder: start the engine once with --game <the folder of your game>; it is "
                                 "remembered in " +
                                 settings.string());
    game = std::filesystem::canonical(game);
    if (!std::filesystem::is_regular_file(storm::storage::child(game, fs::ENGINE_INI_FILE_NAME)))
        throw std::runtime_error(game.string() + " is not a game folder: it has no engine.ini");
    auto folders = storm::desktop::gameFolders(base, game, EngineFolder());
    if (remember && !command.game.empty())
        storm::desktop::rememberGame(settings, game);
    return folders;
}

// --folders: one JSON object on stdout.
int PrintFolders(const CommandLine &command)
{
    try
    {
        const auto folders = DesktopFolders(command, false);
        const auto text = [](const std::filesystem::path &path) {
            const auto utf8 = path.u8string();
            return std::string(utf8.begin(), utf8.end());
        };
        const nlohmann::json json{{"game", text(folders.game)},         {"saves", text(folders.saves)},
                                  {"settings", text(folders.settings)}, {"cache", text(folders.cache)},
                                  {"logs", text(fs::GetLogsPath())}};
        std::printf("%s\n", json.dump(2).c_str());
        return EXIT_SUCCESS;
    }
    catch (const std::exception &error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return EXIT_FAILURE;
    }
}
#endif

// The game folder, the player's folders and the resource index. False (shown): none.
bool PrepareGame([[maybe_unused]] const CommandLine &command)
{
    try
    {
#ifdef __ANDROID__
        storm::android::prepareGameDirectory();
        storm::storage::select(storm::android::folders());
#else
        auto folders = DesktopFolders(command, true);
        std::filesystem::current_path(folders.game);
        spdlog::info("Game folder {}; saves {}, settings {}, cache {}", folders.game.string(), folders.saves.string(),
                     folders.settings.string(), folders.cache.string());
        if (storm::desktop::gameKey(folders.game).title.empty())
            spdlog::warn("The game's scripts name no title (VERSION_NUMBER1 in PROGRAM/_LSC_on_off.h): its saves and "
                         "settings are kept for this exact build and are not found by another version");
        storm::storage::select(std::move(folders));
#endif
        fio->ScanResourcePaths();
        return true;
    }
    catch (const storm::script_patches::LayerError &error)
    {
        ShowEngineDialog(SDL_MESSAGEBOX_ERROR, "Damaged script fixes", error.what());
    }
    catch (const std::exception &error)
    {
        ShowEngineDialog(SDL_MESSAGEBOX_ERROR, "Game data required", error.what());
    }
    return false;
}

#ifndef __ANDROID__
void ConfigureMimalloc()
{
    mi_register_output(mimalloc_fun, nullptr);
    mi_option_set(mi_option_show_errors, 1);
    mi_option_set(mi_option_show_stats, 0);
    mi_option_set(mi_option_eager_commit, 1);
    mi_option_set(mi_option_eager_region_commit, 1);
    mi_option_set(mi_option_large_os_pages, 1);
    mi_option_set(mi_option_page_reset, 0);
    mi_option_set(mi_option_segment_reset, 0);
    mi_option_set(mi_option_reserve_huge_os_pages, 1);
    mi_option_set(mi_option_segment_cache, 16);
#ifdef _DEBUG
    mi_option_set(mi_option_verbose, 4);
#endif
}
#endif

void StartSdl()
{
#ifdef __ANDROID__
    // The manifest pins the activities to sensorLandscape, but SDL recomputes the
    // requested orientation from the window it creates; without this hint a
    // window that is not wider than tall (or a resizable one) unlocks portrait.
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
#endif
    SDL_InitSubSystem(SDL_INIT_EVENTS | SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);

    // Window/lifecycle event hub; must be installed on the game thread.
    storm::AppEvents::instance().install();
    SubscribeEngineToAppEvents();
}

void AnnounceStart()
{
    // Init stash
    create_directories(fs::GetSaveDataPath());

#ifdef STORM_TRACY
    // Development build (cmake/tracy.cmake). The application's release packaging refuses a library with this line.
    STORM_PROFILE_THREAD("game");
    spdlog::info("storm-profiler: Tracy {}.{}.{} client, on demand, listening on 127.0.0.1:8086 (device: adb forward "
                 "tcp:8086 tcp:8086; tools/android/README.md)",
                 tracy::Version::Major, tracy::Version::Minor, tracy::Version::Patch);
#endif
#ifndef __ANDROID__
    spdlog::info("mimalloc-redirect status: {}", mi_is_redirected());
#endif
    AnnounceScriptPatches();
#ifdef __ANDROID__
    storm::android::readStartAtMainMenu();
#endif
}

// engine.ini's window and loop settings.
struct EngineIni
{
    int width = 1024, height = 768;
    int preferred_display = 0;
    bool fullscreen = false;
    bool show_borders = false;
    bool run_in_background = false;
    // The stall watchdog's threshold (diagnostics stall_watchdog.hpp), seconds; 0 turns it off (logged). The scene
    // sweep raises it: its sanitizer builds run several times slower.
    double stall_seconds = 5.0;
};

// Also applies its logging switch and whether sound plays in the background.
EngineIni ReadEngineIni()
{
    EngineIni settings;
    auto ini = fio->OpenIniFile(fs::ENGINE_INI_FILE_NAME);
    if (!ini)
        return settings;
    settings.stall_seconds = ini->GetFloat(nullptr, "stall_watchdog_seconds", 5.0f);
    if (ini->GetInt(nullptr, "logs", 1) == 0) // disable logging
    {
        spdlog::set_level(spdlog::level::off);
    }
    settings.width = ini->GetInt(nullptr, "screen_x", 1024);
    settings.height = ini->GetInt(nullptr, "screen_y", 768);
    settings.preferred_display = ini->GetInt(nullptr, "display", 0);
    settings.fullscreen = ini->GetInt(nullptr, "full_screen", false);
    settings.show_borders = ini->GetInt(nullptr, "window_borders", false);
    settings.run_in_background = ini->GetInt(nullptr, "run_in_background", false);
    if (settings.run_in_background) {
        bSoundInBackground = ini->GetInt(nullptr, "sound_in_background", true);
    }
    else {
        bSoundInBackground = false;
    }
    return settings;
}

std::shared_ptr<storm::OSWindow> OpenWindow(const EngineIni &settings)
{
    std::shared_ptr<storm::OSWindow> window = storm::OSWindow::Create(
        settings.width, settings.height, settings.preferred_display, settings.fullscreen, settings.show_borders);
    window->SetTitle("Sea Dogs");
    window->Subscribe(HandleWindowEvent);
    window->Show();
    core_private->SetWindow(window);
    return window;
}

using LoadingSchedule = std::function<unsigned(uint64_t, storm::platform::LoadingSchedulePhase)>;

// The platform's side of a scene load's synchronous transaction: it pumps the OS and lifecycle queues, not
// ui::tick/takeCommands, Controls or Core::Run.
class LoadingPlatform
{
  public:
    LoadingPlatform(storm::OSWindow &window, AndroidGame *android) : window_(window), android_(android)
    {
    }
    storm::platform::LoadingPlatformState operator()(uint64_t generation);

  private:
    storm::OSWindow &window_;
    [[maybe_unused]] AndroidGame *android_;
    uint64_t cancelGeneration_ = 0;
    bool cancelPending_ = false;
};

storm::platform::LoadingPlatformState LoadingPlatform::operator()(uint64_t generation)
{
    using State = storm::platform::LoadingPlatformState;
    if (cancelGeneration_ != generation) {
        cancelGeneration_ = generation;
        cancelPending_ = false;
        storm::platform::CancelLoadingPlatformInput();
#ifdef __ANDROID__
        android_->CancelLoadingInput();
#endif
    }
    SDL_PumpEvents();
    window_.PumpEvents();
    RethrowCallbackError();
    // Renderer early subscribers still detach/rebind the surface. The main
    // subscribers suppress script callbacks while the transaction is active.
    storm::AppEvents::instance().dispatch();
    RethrowCallbackError();
    audioGate.apply();
#ifdef __ANDROID__
    android_->ConsumeLoadingEvents();
#else
    if (SDL_HasEvent(SDL_QUIT)) isRunning = false;
#endif
    // No loading-time mouse/key event may be replayed by the outer loop.
    // SDLInput's separate event-watch queue is gated/cancelled independently.
    SDL_FlushEvents(0, SDL_LASTEVENT);
    if (!isRunning) return State::Quit;
#ifdef __ANDROID__
    if (const auto state = android_->LoadingState(generation, cancelPending_))
        return *state;
#endif
    return bActive ? State::Continue : State::Suspended;
}

// From here on the game thread is watched for stalls (diagnostics stall_watchdog.hpp): a report with what it and the
// render thread do, and their backtraces, when it has not come back here nor presented a frame for `seconds`.
void StartStallWatchdog(storm::diagnostics::StallWatchdog &watchdog, double seconds)
{
    storm::platform::stall_watch::RegisterGameThread();
#ifdef __ANDROID__
    storm::platform::frameWorkGameThread(static_cast<int>(gettid()));
#endif
    if (seconds > 0.0)
    {
        spdlog::info("stall watchdog: on, reports after {:.1f} s without progress of the game or the render thread", seconds);
        watchdog.Start();
    }
    else
        spdlog::warn("stall watchdog: off (engine.ini stall_watchdog_seconds = 0): a hang leaves no report");
}

void LogStall(storm::diagnostics::StallWatchdog::Level level, const std::string &line)
{
    spdlog::log(level == storm::diagnostics::StallWatchdog::Level::Error ? spdlog::level::err : spdlog::level::warn, "{}", line);
}

// What one iteration of the main loop works with.
struct Loop
{
    storm::OSWindow &window;
    storm::platform::FrameWorkClock &frameWork;
    const LoadingSchedule &loadingSchedule;
    bool runInBackground;
    [[maybe_unused]] AndroidGame *android;
};

// One iteration: input, the system's events, the loading pace, and a game frame when one runs. False: the loop ends.
bool RunIteration(Loop &loop)
{
    STORM_PROFILE_ZONE("main loop");
    storm::platform::stall_watch::Progress();
    if (const auto work = loop.frameWork.lap(storm::platform::FrameWorkClock::Clock::now()))
        storm::platform::reportFrameWork(std::chrono::duration_cast<std::chrono::nanoseconds>(*work));
#ifdef __ANDROID__
    const auto inputEpoch = loop.android->TakeInput();
#endif
    SDL_PumpEvents();
    loop.window.PumpEvents();
    RethrowCallbackError(); // from a handler SDL called during the pump
    // Queued window/lifecycle events, delivered between frames on this thread.
    storm::AppEvents::instance().dispatch();
    audioGate.apply();
#ifdef __ANDROID__
    loop.android->AfterEvents(inputEpoch);
#else
    // SDL turns SIGINT/SIGTERM into SDL_QUIT: the host shuts down in order, like closing the window.
    if (SDL_HasEvent(SDL_QUIT))
    {
        spdlog::info("Quit requested (SIGINT/SIGTERM)");
        isRunning = false;
    }
#endif
    SDL_FlushEvents(0, SDL_LASTEVENT);
    if (!isRunning) {
        storm::render::SetSceneLoadingWindow(0, 0);
        return false;
    }
    // All SDL/lifecycle and load actions were serviced above. Only real frames run the
    // adapters below; waiting never fabricates frame/stability observations.
    bool canPaceLoading = bActive;
#ifdef __ANDROID__
    canPaceLoading = canPaceLoading && loop.android->CanRun();
#endif
    // The synchronous native stock loop invokes this same controller
    // itself. The outer loop never supplies a second heartbeat for it.
    if (!storm::platform::LoadingPlatformTransactionActive()) {
        const auto loading = storm::render::GetSceneLoadStatus();
        const auto wait = loop.loadingSchedule(loading.generation, canPaceLoading
            ? storm::platform::LoadingSchedulePhase::Poll : storm::platform::LoadingSchedulePhase::Suspended);
        if (wait) {
            std::this_thread::sleep_for(std::chrono::milliseconds(wait));
            return true;
        }
    }

    if ((bActive || loop.runInBackground)
#ifdef __ANDROID__
        && loop.android->CanRun()
#endif
    )
    {
        loop.frameWork.frame();
#ifdef __ANDROID__
        loop.android->BeforeFrame();
#endif
        {
            STORM_PROFILE_ZONE("frame: script, entities, render");
            loop.loadingSchedule(storm::render::GetSceneLoadStatus().generation,
                                 storm::platform::LoadingSchedulePhase::FrameStarted);
            RunFrame();
        }
        RethrowCallbackError(); // from a handler SDL called during the frame
#ifdef __ANDROID__
        loop.android->AfterFrame();
#endif
    }
    else
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

#ifndef __ANDROID__
    if (core.Controls && core.Controls->GetDebugAsyncKeyState(VK_F1) && core.Controls->GetDebugAsyncKeyState(VK_SHIFT))
    {
        mi_stats_print_out(mimalloc_fun, nullptr);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
#endif
    return true;
}

// Exceptions escaping an iteration (the frame, script events raised by HUD input, lifecycle and window handlers, the
// Android adapters) are reported and end the session in order instead of reaching std::terminate; see
// ReportFatalError.
void RunLoop(Loop &loop)
{
    try
    {
        while (isRunning && RunIteration(loop))
        {
        }
    }
    catch (const std::exception &error)
    {
        ReportFatalError(error.what());
    }
    catch (...)
    {
        ReportFatalError("unknown error");
    }
}

// After a fatal error the engine state is unknown: still try to shut down in order (services flush files), but
// never let a second exception abort.
int ReleaseCore(int exitCode)
{
    try
    {
        if (exitCode == EXIT_SUCCESS)
            core_private->Event("ExitApplication");
        core_private->CleanUp();
        core_private->ReleaseBase();
    }
    catch (const std::exception &error)
    {
        spdlog::critical("Exception during shutdown: {}", error.what());
        exitCode = EXIT_FAILURE;
    }
    catch (...)
    {
        spdlog::critical("Unknown exception during shutdown");
        exitCode = EXIT_FAILURE;
    }
    if (auto logger = spdlog::default_logger())
        logger->flush();
    return exitCode;
}
} // namespace

int main(int argc, char *argv[])
{
    CommandLine command;
#ifdef __ANDROID__
    EnterOnce();
#else
    if (!ParseCommandLine(argc, argv, command))
        return 2;
    if (command.printFolders)
        return PrintFolders(command);
#endif
    // Every return from here on is a clean exit: the running marker goes (a crash or a kill leaves it).
    struct RunningMarker
    {
        ~RunningMarker()
        {
            if (auto *marker = sessionMarker.load())
                SessionMarkerResult(marker->Clear());
        }
    } runningMarker;
    if (!StartLogs())
        return EXIT_FAILURE;
#ifndef __ANDROID__
    ConfigureMimalloc();
#endif
    if (!PrepareGame(command))
        return EXIT_FAILURE;
    StartSdl();
    const auto lifecycleDiagnosticsGuard = lifecycleDiagnostics.initialize();
    AnnounceStart();

    core_private = static_cast<CorePrivate *>(&core);
    core_private->Init();
    const EngineIni settings = ReadEngineIni();
    const std::shared_ptr<storm::OSWindow> window = OpenWindow(settings);
    core_private->InitBase();

    // Message loop. Frames are paced by the renderer's present (Swappy on Android, vsync elsewhere).
#ifdef __ANDROID__
    AndroidGame androidGame;
    AndroidGame *android = &androidGame;
#else
    AndroidGame *android = nullptr;
#endif
    int exitCode = EXIT_SUCCESS;
    storm::platform::FrameWorkClock frameWork;
    storm::render::LoadingSchedule loadingSchedule;
    const LoadingSchedule schedule = [&loadingSchedule](uint64_t generation,
                                                         storm::platform::LoadingSchedulePhase phase) {
        return loadingSchedule(generation, phase,
                               static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                         std::chrono::steady_clock::now().time_since_epoch())
                                                         .count()));
    };
    storm::platform::LoadingScheduleRegistration loadingScheduleRegistration(schedule);
    isRunning = true;
    storm::platform::LoadingPlatformRegistration loadingPlatform(LoadingPlatform(*window, android));
    storm::diagnostics::StallWatchdog::Config stallConfig;
    stallConfig.stall_seconds = settings.stall_seconds;
    storm::diagnostics::StallWatchdog stallWatchdog(stallConfig, LogStall);
    StartStallWatchdog(stallWatchdog, settings.stall_seconds);
    Loop loop{*window, frameWork, schedule, settings.run_in_background, android};
    RunLoop(loop);
    stallWatchdog.Stop(); // the shutdown below is not a frame loop
    if (fatalError)
        exitCode = EXIT_FAILURE;

    loadingPlatform.reset(); // No callback may retain stack captures during service teardown.
#ifdef __ANDROID__
    androidGame.Shutdown();
#endif
    storm::AppEvents::instance().uninstall();
    exitCode = ReleaseCore(exitCode);
#if _DEBUG && !defined(__ANDROID__)
    mi_option_set(mi_option_verbose, 0); // disable statistics writing in Linux
#endif
    SDL_Quit();

    return exitCode;
}
