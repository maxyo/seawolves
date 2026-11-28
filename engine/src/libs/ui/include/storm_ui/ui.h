// Portable game UI runtime. The platform host owns the window, the bgfx device and
// the frame loop; this library owns documents, components, input routing and drawing.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "storm_ui/action.h"
#include "storm_ui/hud_command.h"
#include "storm_ui/hud_state_fwd.h"

namespace storm::ui
{

// Read access to the UI pack (documents, styles, component specs, icons, fonts).
class FileSource
{
  public:
    virtual ~FileSource() = default;
    virtual bool Read(const std::string &path, std::vector<uint8_t> &out) = 0;
};
std::unique_ptr<FileSource> DirectoryFiles(std::string root);

struct Dialogue;       // dialogue_state.h
struct InterfaceModel; // interface_model.h
struct ScriptScreen;   // script_screens.h
class ScriptPort;

// A text field asks for the platform editor (IME bridge). The host shows a native editor
// over `x, y, width, height` (physical pixels) and answers with Host::SubmitText.
struct TextRequest
{
    std::string token;
    std::string text;
    std::string title;
    bool numeric = false;
    bool secret = false; // a password: the editor masks what is typed
    float x = 0, y = 0, width = 0, height = 0;
};

class RenderBackend; // render_backend.h

enum class LogLevel
{
    Info,
    Warning,
    Error,
};

struct Config
{
    FileSource *files = nullptr; // must outlive the host
    // Renderer seam, required: BgfxBackend::Create() (backend_bgfx.h). There is no default.
    RenderBackend *backend = nullptr; // must outlive the host
    int width = 0;                    // physical pixels
    int height = 0;
    float density = 1.0f; // physical pixels per dp
    // Directory with the player's game files (the one holding RESOURCE/): pictures.ini (atlas
    // references) is read from there. The pictures themselves come from the renderer
    // (RenderBackend::AcquireGamePicture); nothing of them ships with the application.
    std::string game_root;
    std::function<void(const Action &)> on_action;
    std::function<void(const TextRequest &)> on_text_request;
    // Debug builds: every UI error is also shown as a toast and broken components draw their inline
    // marks (root class ui-debug). Release builds only log them.
    bool show_errors = false;
    // Only the engine host presents gameplay HUDs and sends their ownership declarations.
    // Standalone application screens (launcher, downloads, gallery) disable this.
    bool game_hud = true;
    // Checks every prediction of Prepare: a frame it called unchanged that draws other
    // commands than the frame shown is an error (host checks and captures turn this on).
    bool verify_unchanged = false;
    // Template strict mode (host checks and captures): a template path that names nothing (an
    // unknown name, a missing key, an index past the end) is an error of the component's render,
    // not null; paths that may be missing say so with opt() or or() (template.h).
    bool strict_templates = false;
    // Tap map for device crawls (tools/ui_crawl): whenever what can be tapped changed, one "taps" block in the
    // log lists every touch region of every drawn component, in physical pixels.
    bool log_taps = false;
    // The flicker check (src/action_trace.h): records the frames after each player's action until they settle and
    // reports transient ones ("UI flicker: …"). The traces are kept for Host::TakeActionTraces.
    bool trace_actions = false;
    // Optional clock in seconds, for deterministic captures.
    std::function<double()> clock;
    // Also gets every line the host logs: its errors, RmlUi's messages and every text it puts on the screen
    // (toast, alert, game log line; errors at Error). The engine process writes them to its log files. The lines
    // go to logcat (tag StormUi) or stderr either way.
    std::function<void(LogLevel level, const std::string &line)> on_log;
};

// What one Host::Frame did. A steady frame (nothing changed) rebuilds, lays out, compiles
// and uploads nothing; the host checks hold that (tests/frame_cost_check.cpp) and the
// platform hosts log these numbers to measure the UI on a device.
struct FrameStats
{
    double update_ms = 0.0; // CPU: timers, routing, Context::Update (styles, rebuilds, layout)
    double render_ms = 0.0; // CPU: Context::Render and the backend's frame (command submission, not GPU time)
    // Parts of update_ms (and, for textures, of render_ms), for the screen switch line (Host log
    // "UI switch"): what a frame's CPU went to.
    double apply_ms = 0.0;   // host calls between frames: interface models and state channels applied
    double rebuild_ms = 0.0; // components built or updated from their compiled templates
    double style_ms = 0.0;   // RmlUi's element update without the rebuilds: styles, definitions
    double layout_ms = 0.0;  // RmlUi's document layout and positioning
    double picture_ms = 0.0; // game pictures acquired from the renderer (.tx read, decoded, uploaded)
    int pictures = 0;        // game pictures acquired
    double texture_ms = 0.0; // textures made through the render interface (pack images decoded; font atlases)
    int rebuilds = 0;        // components built or updated (an update runs only what reads a changed prop)
    int touched = 0;         // elements those updates changed: attributes, text or props set, elements made,
                             // moved or removed
    int laid_out = 0;        // component boxes formatted (0 = no layout ran)
    int layout_passes = 0;   // documents formatted (RmlUi lays out a whole document at a time)
    int geometry = 0;        // meshes compiled
    int draws = 0;           // draw calls (geometry, shaders)
    int masks = 0;           // clip-mask (stencil) draws: rounded or transformed clipping
    int layers = 0;          // offscreen layers composited (filters, masks), as RmlUi asked for them
    int cached_layers = 0;   // of those, filtered layers drawn from the runtime's cache (no backend layer)
    int textures = 0;        // textures created (uploads)
    size_t texture_bytes = 0;
    // With Config::verify_unchanged: this frame was predicted unchanged by Prepare but drew
    // other commands than the frame shown (also an error in the log).
    bool mispredicted = false;
    uint64_t number = 0; // which Frame this was (1, 2, …): a frame composited unchanged keeps the last one's
    // What the frame showed, for the frame timeline of a platform host: the mounted screen with the
    // interface model it was built from ("s-store store#12.3" = pack screen, interface id, opening,
    // model revision), "document", "hud" or "nothing", and " +opening" while an interface the game
    // announced has no frame yet.
    std::string shown;
};

// What the screen showed between one player's action and the state it settled in (Config::trace_actions).
struct ActionTrace
{
    // "<where> <element id> <verb>" of the first event the action caused; "tap <x>,<y>" / "back" without one
    std::string action;
    std::string before;        // FrameStats::shown when it started
    std::string after;         // and of the settled frame
    int frames = 0;            // frames presented after the action until it settled
    int transient = 0;         // of them, frames whose content differs from both the start and the settled state
    double transient_ms = 0.0; // how long those were on the screen
    double area = 0.0;         // the largest share of the screen (0..1) one transient frame changed
    double settle_ms = 0.0;    // from the action to the settled frame
    // The first presented frame that showed something else than the start: which frame after the action (1 = the
    // action's own frame, 0 = none changed anything) and when.
    int feedback_frame = 0;
    double feedback_ms = 0.0;
    // The screen predicted the action's result (screens::Bindings::Predict): it must be drawn in the action's own
    // frame. `late`: the first change came in a later frame (feedback_frame > 1).
    bool predicted = false;
    bool late = false;
    bool interrupted = false; // another action began before it settled (its last frame is taken as the settled one)
    // The game stopped being live (the app left the screen) before the action settled: what it led to was not drawn,
    // and its last frame before that is taken as the settled one.
    bool cut_off = false;
    bool unsettled = false; // still changing after ActionTracer::kMaxSeconds
    // One line per transient frame: when, for how long, how much, what it showed ("+40 ms 33 ms 52%: s-saves
    // save_load#4.1: c-header#header 'Загрузка' …").
    std::vector<std::string> details;
};

enum class TouchPhase
{
    Down,
    Move,
    Up,
    Cancel,
    // A move sample of a platform that gives the UI its touches once per frame: it counts for
    // drag velocities at once, and what the pointer drags moves at the next Host::PlaceTouches.
    Sample
};

class Host
{
  public:
    // Landscape only: width is the long side.
    // One host per process: RmlUi's state is process-global, so Create fails while another host
    // lives. Platforms put every UI-hosting screen that can coexist into its own process.
    static std::unique_ptr<Host> Create(const Config &config, std::string *error = nullptr);
    virtual ~Host() = default;

    virtual bool LoadDocument(const std::string &path) = 0;
    virtual void Resize(int width, int height, float density) = 0;
    // The game's language ("russian", "english", …): the strings of the pack in it (English for one the pack has no
    // strings for, said once). An interface model names it too; this is for the time before the first one.
    virtual void SetLanguage(const std::string &game_language) = 0;
    // Safe-area insets in dp (the system bars; a display cutout is ignored: the UI runs under it). HUD scenes and menu
    // screens keep their content inside; templates read them as `safe.left` … `safe.bottom`.
    virtual void SetSafeArea(float left, float top, float right, float bottom) = 0;
    // The player's display options («Размер интерфейса», «Прозрачность кнопок», «Под левую руку»): the layout's dp
    // grow by `ui_scale` (1 = the display's density; Resize and SetSafeArea keep taking the display's own density and
    // dp), the HUD's buttons are drawn at `hud_button_opacity` (1 = as designed), and `left_handed` mounts the HUD
    // mirrored left to right (the pack's rcssMirrored / propsMirrored).
    virtual void SetDisplayOptions(float ui_scale, float hud_button_opacity, bool left_handed) = 0;
    // One sample of a pointer, in physical pixels. `time` is when it happened, in seconds of
    // the host's clock (Now): platforms pass every sample they have (a move event's historical
    // ones too) with its own time, which drag velocities and flings are measured from.
    virtual void Touch(TouchPhase phase, int pointer, float x, float y, double time) = 0;
    // A sample that happens now (tests, tools).
    void Touch(TouchPhase phase, int pointer, float x, float y)
    {
        Touch(phase, pointer, x, y, Now());
    }
    // Places every pointer with samples given as TouchPhase::Sample where the finger is at `time`
    // (the frame's time on the host's clock), resampled between its samples as Android does, so a
    // dragged slider or list moves evenly. One move per pointer and frame reaches the behaviours.
    virtual void PlaceTouches(double time) = 0;
    // The host's clock in seconds (Config::clock, else a monotonic clock from its start).
    virtual double Now() const = 0;
    // Settles timers, routing and the documents without drawing. True when the frame on
    // the screen is out of date: the host then asks for a Frame. False when it still shows
    // everything (a state that changed nothing visible): the host draws nothing, and
    // acknowledgements that wait for a drawn frame are given for the frame shown. `delay`
    // is the time in seconds until Prepare should run again (0 while something animates).
    virtual bool Prepare(double &delay) = 0;
    // Updates and draws one frame over a transparent background (Prepare included). Returns
    // the time in seconds until another frame is needed: 0 while something animates.
    virtual double Frame() = 0;
    // What the last Frame did (see FrameStats).
    virtual FrameStats LastFrameStats() const = 0;
    // Makes ahead what the first opening of a screen would make (src/prewarm.h): the font face handles of the pack's
    // font uses (pack.json "prewarm") with the glyph blocks of the game language and their layer textures, one use a
    // step, then its icons' textures. Steps run while the time spent stays within `budget_ms`, each predicted by the
    // slowest step so far; with `hidden` (nothing the player sees waits: a loading screen is up) the first step runs
    // whatever it costs. Call it between frames, on the thread of Frame. True while something is left to make; a new
    // game language or UI scale makes the uses again at the new size. Its time and textures are not those of a frame
    // (FrameStats) but its own line in the log when it is done.
    virtual bool Prewarm(double budget_ms, bool hidden) = 0;
    virtual void Toast(const std::string &text, const std::string &tone) = 0;
    // Something the player asked for failed, in the pack's words: a danger toast of the string
    // `key` (the pack's table in the game language) that stays several seconds.
    virtual void AlertString(const std::string &key) = 0;
    // A line of the game's log: an event line of the HUD's log over the game (a toast
    // elsewhere). `target` is where a tap on it leads, "" = nowhere: "character:<index>" (that
    // character's abilities), "journal:<quest id>" (that quest), "items" (the inventory).
    virtual void GameMessage(const std::string &text, const std::string &target) = 0;

    // --- game interfaces -----------------------------------------------------------
    // Routes a game interface to its screen: the ported screen registered for the id (a model
    // it cannot admit closes the interface as a defect), the generic layout for an id with no
    // ported screen. Call again for every model revision.
    virtual void SetInterface(const InterfaceModel &model) = 0;
    // The screens the attached scripts provide, run through `port` (script_screens.h); they replace a ported screen
    // of the same interface. A screen whose pack component is missing is an error line and is left out.
    virtual void SetScriptScreens(const std::vector<ScriptScreen> &screens, ScriptPort *port) = 0;
    virtual void CloseInterface() = 0;
    // The game's script ended the open interface (its next one, if any, opens some frames later): its screen stays as
    // drawn until SetInterface of the next or CloseInterface, and is no longer built or tapped.
    virtual void InterfaceLeft() = 0;
    // The game opens an interface whose model is still to come (it can take seconds: the
    // interface loads first). From now the UI covers the game: WantsTouch is true everywhere
    // and touches are held until the interface's first frame, then applied to it, so the
    // player's first tap is not lost. CloseInterface withdraws the opening (held touches are
    // dropped, logged).
    virtual void ExpectInterface() = 0;
    // A named state channel as JSON: the application's and the game's screens' channels, the HUD's and "dialogue"
    // in the fixture format of their structs ("worldInfo" and "contextAction": the engine's text as a JSON
    // string), plus "presentation": {"screen": "<interface id open
    // in the game, or empty>", "loading", "cinematic", "dialogue", "live": <the game is in front
    // and running>}, composed by the engine's host. The HUD scene of the gameplay mode is drawn
    // from these while nothing else covers the game.
    // False (and an error) for malformed JSON; a channel without a consumer and data that
    // does not fit its channel are errors too (logged, shown in debug hosts).
    virtual bool SetState(const std::string &channel, const std::string &json) = 0;
    // The new state of a HUD channel (hud_state.h), as the engine fills it.
    virtual void SetState(const HudState &state) = 0;
    // The game's dialogue (dialogue_state.h), as the engine fills it: shown over the game while active.
    virtual void SetDialogue(const Dialogue &dialogue) = 0;
    // The engine's answer to a widget action (Action::request): applied (its result is in the next
    // interface model) or refused, with the reason (logged). The screen's busy marks and amount
    // drafts follow it. For a HUD command (hud_command.h `request`) `revision` is the revision of its
    // state channel that shows it.
    virtual void Answer(uint64_t request, bool applied, const std::string &reason, uint64_t revision = 0) = 0;
    // Answer to Config::on_text_request; `accepted` false = the editor was dismissed.
    virtual void SubmitText(const std::string &token, const std::string &text, bool accepted) = 0;
    // True while a game interface or a plain document covers the game.
    virtual bool InterfaceActive() const = 0;

    // --- touch routing over the game ------------------------------------------------
    // Ask at pointer-down, per pointer: true delivers that pointer's whole gesture through Touch,
    // false gives it to the game (camera look). True on an interactive HUD element, while an
    // interface, sheet or document covers the game, and between scenes (HostImpl::Between).
    // Physical pixels. Thread-safe: answered from the layout of the last frame.
    virtual bool WantsTouch(float x, float y) const = 0;
    // The platform took the gestures away (focus lost, a menu opened): every pointer the
    // UI holds is cancelled and every held HUD control is released.
    virtual void CancelInput() = 0;
    // Thread-safe: the back key belongs to the UI now (an open HUD sheet).
    virtual bool WantsBack() const = 0;
    // The back key: an open HUD sheet when WantsBack said so; otherwise, while an interface
    // is mounted, its screen closes its sheet or open part first, as its ← does, and sends the
    // engine's own back when it has nothing to close.
    virtual void Back() = 0;

    // The actions finished since the last call (Config::trace_actions), oldest first. `finish_open`: the action still
    // open ends now, its last presented frame taken as its settled state (a crawl that moves on).
    virtual std::vector<ActionTrace> TakeActionTraces(bool finish_open = false) = 0;
};

} // namespace storm::ui
