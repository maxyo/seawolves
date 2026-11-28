// The in-game HUD: state channels of the game become the props of the HUD elements that
// hud-layout.json of the pack places in a scene, and component events become engine input.
// See README.md in ../screens ("The HUD") for how to port a scene.
#pragma once

#include "../screens/screen.h"
#include "../text_utf8.h"
#include "storm_ui/hud_command.h"
#include "storm_ui/hud_state.h"

#include <cstdint>
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace storm::ui::hud
{

// The icon of a fast-travel place by the game's picture of it ("landtarget_slb:<cell>", the
// landCommands item's `picture`); nullptr = a picture the land HUD does not know.
const char *LandPlaceIcon(const std::string &picture);

// The talk hint's icon by the role of the person spoken to, read from the game's own data: the
// target's dialog file (combat channel interactDialog, "common_store", "usurer_dialog",
// "enc_officer_dialog"). An ordinary person (a citizen, a quest character) keeps "talk".
const char *LandTalkIcon(const std::string &dialog);

// A message shown again while its toast is still up: one toast, "text ×N". Shared by the
// HUD's event log and the runtime's toast stack (every game log line comes through these).
inline std::string RepeatedText(const std::string &text, int count)
{
    return count > 1 ? text + " \u00D7" + std::to_string(count) : text;
}

// How long a toast or event line stays. A repeat («×N») starts it again; a finger on it stops it.
inline double ToastSeconds(const std::string &text)
{
    const size_t characters = Rml::StringUtilities::LengthUTF8(text);
    return std::clamp(2.5 + 0.06 * static_cast<double>(characters), 3.0, 7.0);
}

using screens::Event;

// --- states: the channels (storm_ui/hud_state.h) as the scenes read them ----------------

struct States
{
    Gameplay gameplay;
    Combat combat;
    Vitals vitals;
    std::string world_info;     // channel "worldInfo": lines of the location text
    std::string context_action; // channel "contextAction": the script's quick action, "" = none
    Door door;                  // channel "reloadPlace": the door the hero stands at
    LandCommands land_commands;
    Ship ship;
    SeaOrders sea_orders;
    SeaQuick sea_quick;
    WorldMap world_map;
    Presentation presentation;
    Spyglass spyglass;
};

// --- view -----------------------------------------------------------------------------

// What a scene shows. `elements` holds the live props of the layout elements by id; an
// element of the layout scene that is left out is not mounted at all.
struct View
{
    std::string layout; // scene of hud-layout.json
    Json elements = Json::object();
    std::vector<std::string> dimmed; // elements drawn subdued (under an open wheel)
    std::string sheet;               // pack part over the scene ("s-hud-sheet"), "" = none
    Json sheet_props = Json::object();
    bool modal = false;                  // the sheet takes every touch and the back key
    bool back = false;                   // the scene takes the back key without a sheet (the raised spyglass)
    bool log = false;                    // event lines are shown now (Hud::LogLines in the `log` element)
    std::string overlay;                 // pack part over the scene and under the sheet, takes no touches
    Json overlay_props = Json::object(); // (the spyglass target card, laid by the Hud over any scene)
    bool operator==(const View &) const = default;
};

// Connects element ids of the view to what happens on their events (Scene::Build).
class Input
{
  public:
    using Handler = std::function<void(const Event &)>;
    // An empty verb takes every verb of the element.
    void On(const std::string &id, const std::string &verb, Handler handler);
    // Expected and needs no action. An event that is neither bound nor ignored is an error.
    void Ignore(const std::string &id, const std::string &verb);

  private:
    friend class Hud;
    std::unordered_map<std::string, Handler> handlers_;
};

class Hud;

// A picker of the HUD (the sea ammunition fan, the abilities): a button that, tapped, lays its options
// out as ordinary HUD buttons in the layout's slots <button>-1…N (hud-layout.json), beside it. The
// slots take the place of the buttons under them (their "replaces"): the scene leaves those out
// while Open(). The button again, a pick, Back or kSeconds untouched fold it. An option without a
// pick is not tappable: greyed by its props ("disabled") when it cannot be used, its tap (or the press
// behaviour's "refused") does nothing.
class Fan
{
  public:
    static constexpr double kSeconds = 6.0; // an untouched picker folds by itself
    struct Option
    {
        Json props;                 // the c-hud-button props of the slot (icon, label, badge, fill, disabled)
        std::function<void()> pick; // empty: not tappable now
    };
    Fan(Hud &hud, std::string button) : hud_(hud), button_(std::move(button))
    {
    }
    // The button (`props`, the open look added) and, while open, the options in its slots. Without
    // options the button stays and opens nothing. `opened` runs when a tap opens it (a scene folds
    // its other picker there).
    void Build(Json props, const std::vector<Option> &options, View &view, Input &input,
               const std::function<void()> &opened = {});
    bool Open() const
    {
        return open_;
    }
    void Close()
    {
        open_ = false;
    }
    // The back key: folds an open picker (true), else leaves the key to the scene.
    bool Back();
    // Folds the picker once its time is up (a rebuild); lowers `next` to the seconds until then.
    void Expire(double now, double &next);

  private:
    Hud &hud_;
    std::string button_;
    bool open_ = false;
    double until_ = 0.0;
};

// The active abilities of a scene (sea: the ship abilities, land: Rush) behind one small button,
// the layout element "abilities" (a star; a dot while one is ready, the fill while one runs), also
// for one ability; `lead` are entries of the scene laid out before them (land: the potion), the
// button is drawn for them alone too. A tap lays them out as HUD buttons beside it (Fan, slots abilities-1…4): ready
// (a tap launches it through `launch`, the game's own command, and folds the row), running (its
// fill and seconds, not tappable), recovering and blocked (the disabled look; recovering keeps its
// fill and seconds). Nothing is drawn without abilities: the button is hidden then.
class AbilityControls
{
  public:
    explicit AbilityControls(Hud &hud) : hud_(hud), fan_(hud, "abilities")
    {
    }
    void Build(const std::vector<Ability> &abilities, View &view, Input &input,
               const std::function<void(const Ability &)> &launch, std::vector<Fan::Option> lead = {});
    bool Open() const
    {
        return fan_.Open();
    }
    void Close()
    {
        fan_.Close();
    }
    bool Back()
    {
        return fan_.Back();
    }
    void Expire(double now, double &next)
    {
        fan_.Expire(now, next);
    }

  private:
    Hud &hud_;
    Fan fan_;
};

// One ported gameplay zone (land, sea, world map). The instance lives while the gameplay
// mode stays in its zone, so members hold local state (an open wheel, a held stick).
class Scene
{
  public:
    virtual ~Scene() = default;
    // Called for every state change while the HUD is visible and after Hud::Rebuild().
    virtual void Build(const States &states, View &view, Input &input) = 0;
    // The states changed while the HUD cannot be seen (a menu, loading, a cutscene).
    virtual void Hidden(const States &)
    {
    }
    // Timed work (heartbeats); returns the seconds until it must be called again.
    virtual double Tick(const States &, double /*now*/)
    {
        return 3600.0;
    }
    // A frame with the current view was drawn (coverage acknowledgements).
    virtual void Drawn(const States &)
    {
    }
    // Every held control was let go (the platform cancelled input).
    virtual void Released(const States &)
    {
    }
    // The player cannot act any more (the game lost the screen): give up what the HUD owns.
    virtual void Suspended(const States &)
    {
    }
    // The engine reports no gameplay mode ("unknown") while something of this scene is
    // still open (the land menu blocks gameplay): true keeps the scene until it closes.
    virtual bool Holds(const States &) const
    {
        return false;
    }
    // The back key; true when the scene used it (an open sheet).
    virtual bool Back(const States &)
    {
        return false;
    }
};

struct SceneEntry
{
    const char *name;
    std::span<const char *const> modes; // gameplay modes it draws
    std::unique_ptr<Scene> (*create)(Hud &);
    // The engine's name for this HUD in "hudowner:<name>", declared at startup and after
    // a long gap between UI ticks so the engine knows which presenters are available.
    const char *presents = nullptr;
};
// The HUD scenes by gameplay mode (hud.cpp).
std::span<const SceneEntry> Scenes();

// Where a part of a mounted HUD element is on the screen, in physical pixels.
struct Placement
{
    float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f; // the box
    float layer_x = 0.0f, layer_y = 0.0f;                  // the origin of the HUD layer (the safe area)
    float screen_width = 0.0f, screen_height = 0.0f;
    float density = 1.0f;                          // pixels per dp
    float layer_width = 0.0f, layer_height = 0.0f; // the size of the HUD layer (the safe area)
};

// What the HUD needs from the host; unit tests give it a fake.
class Mount
{
  public:
    virtual ~Mount() = default;
    // The box of the mounted element `id`, or of the part inside it that `selector` names,
    // as the last frame laid it out. False while it is not on the screen.
    virtual bool Measure(const std::string &id, const std::string &selector, Placement &out) const = 0;
    // Mounts the layout scene with these element props, patching what is already there.
    // False when the pack cannot draw it (unknown scene or element); the reason is reported.
    virtual bool ShowHud(const View &view) = 0;
    virtual void HideHud() = 0;
    virtual void Send(const Action &action) = 0;
    // The game language of the HUD ("russian"): tr() in templates and Text() follow it.
    virtual void SetLanguage(const std::string &game_language) = 0;
    virtual std::string Text(const std::string &key) = 0;
    virtual void Log(const std::string &line) = 0;
    virtual void Error(const std::string &line) = 0;
    virtual double Now() const = 0;
    // A game interface is mounted over the game: the HUD stays out of the way.
    virtual bool InterfaceActive() const = 0;
    // A screen is mounted (a game interface, the dialogue panel), shown or held under a
    // full-screen video: what the player can still reach.
    virtual bool ScreenMounted() const = 0;
    // A notice for the player that survives the change of screen (the danger toast).
    virtual void Alert(const std::string &text) = 0;
    // «Под левую руку»: the layout is mounted mirrored (scenes that reason about which side an element is on
    // measure in the design's own orientation and mirror their result). Hosts without the option: never.
    virtual bool Mirrored() const
    {
        return false;
    }
};

using screens::SubjectRequest;

class Hud
{
  public:
    // `scenes`: the ported scenes; unit tests give their own.
    explicit Hud(Mount &mount, std::span<const SceneEntry> scenes = Scenes());
    ~Hud();

    // --- for consumers ---
    States &Edit()
    {
        changed_ = true;
        return states_;
    }
    // The new state of one channel (states.cpp).
    void Set(const HudState &state);
    // A fixture of a channel does not have its shape: logged once per distinct text, shown in debug hosts.
    void Reject(const std::string &channel, const std::string &why);

    // --- for scenes ---
    const States &Current() const
    {
        return states_;
    }
    // A command for the engine (storm_ui/hud_command.h). A `slot` marks a latest-value input (a stick position, a
    // geometry): only the newest one of a slot matters.
    void Send(Command command, const std::string &slot = {});
    // A command the engine answers (a menu step, a map command, the sails: their `request`), numbered in the
    // router's request ledger and sent. The scene holds what the tap shows while Answered() is Waiting or Applied.
    template <class T> uint64_t Request(T command);
    RequestLedger::Status Answered(uint64_t request) const
    {
        return router_ ? router_->Requests().Of(request) : RequestLedger::Status::None;
    }
    void Rebuild()
    {
        changed_ = true;
    }
    // «Под левую руку»: the layout is mounted mirrored (Mount::Mirrored).
    bool Mirrored() const
    {
        return mount_.Mirrored();
    }
    double Now() const
    {
        return mount_.Now();
    }
    bool Measure(const std::string &id, const std::string &selector, Placement &out) const
    {
        return mount_.Measure(id, selector, out);
    }
    // Logged once per distinct text.
    void Log(const std::string &line)
    {
        if (reported_.insert(line).second)
            mount_.Log(line);
    }
    // A string of the pack's table in the game language of the gameplay state. Every text
    // of the HUD that the game does not send comes from here.
    std::string Text(const std::string &key)
    {
        return mount_.Text(key);
    }
    // Reported once per distinct text.
    void Error(const std::string &line);
    // Event lines shown top-centre by the scene's `log` element; newest first.
    Json LogLines() const;

    // A tap on a party portrait or a squadron ship opens a game screen on that subject: the scene sends the opening
    // shortcut and passes the subject to that screen (Router::Pass), which takes it once (UiTake); one nobody takes
    // lapses after kSubjectSeconds (the game may refuse to open the screen).
    static constexpr double kSubjectSeconds = 5.0;
    void RequestSubject(const SubjectRequest &request);
    // The router the subjects go to (Router::AttachHud).
    void AttachRouter(screens::Router *router)
    {
        router_ = router;
    }

    // A scene sent what leads into a scene change (a fast-travel place chosen): the HUD stays
    // hidden from now until the game reports the transition (gameplay blocked "transition" or
    // "loading": its fader is up), so nothing of the HUD shows between the closing panel and the
    // fader. A change that does not come within kTransitionWait is logged and the HUD comes back.
    static constexpr double kTransitionWait = 1.5;
    void ExpectTransition(const std::string &what);
    void ExpectLandTransition(const std::string &what, const LandCommands &commands, const std::string &id);

    // --- for the host ---
    // An event line for the top-centre log; false while no scene shows one. `target`: where a
    // tap on it leads (Host::GameMessage), "" = nowhere.
    bool Notice(const std::string &text, const std::string &tone, const std::string &target = {});
    // False when the event belongs to nothing bound.
    bool OnEvent(const Event &event);
    // Applies pending state changes and timed work; returns seconds until the next call.
    double Tick();
    void Drawn();
    // Every pointer was released by the platform.
    void Released();
    bool Back();
    bool Visible() const
    {
        return visible_;
    }
    // What is shown is only the game-menu button (layout "menu"): no scene draws this mode.
    bool MenuOnly() const
    {
        return visible_ && menu_only_;
    }
    // The game is in front and running (presentation "live").
    bool GameLive() const
    {
        return states_.presentation.known && states_.presentation.live;
    }
    // The player can act: touches on HUD elements belong to the HUD.
    bool Interactive() const
    {
        return visible_ && states_.presentation.live;
    }
    const View &Shown() const
    {
        return view_;
    }
    // The game is in a gameplay mode, or its scene holds through an "unknown" one (an open
    // sheet that blocks gameplay): from the states as they are now, not the last refresh.
    bool InGameplay() const
    {
        return states_.gameplay.mode != "unknown" || (scene_ && scene_->Holds(states_));
    }

  private:
    struct Line
    {
        std::string text, tone;
        double until = 0.0;
        std::string base; // the message itself; `text` adds the repeat count
        int count = 1;
        unsigned key = 0;      // the line's own element in the log (data-key)
        double leaving = -1.0; // when it started to animate out (-1 = shown); removed kLeaveSeconds later
        double held = -1.0;    // the seconds it had left when a finger stopped it (-1 = running)
        std::string target;    // where a tap on it leads, "" = nowhere
    };
    void Refresh();
    void Conceal();
    // The exception boundary of HUD component code: a scene's calls and handlers, the
    // spyglass overlay. Runs `call`; a throw is reported by Disable and false returned.
    template <class Call> bool Guarded(const std::string &component, const char *where, Call &&call);
    // A HUD component threw: an error line naming it, the call and the exception (shown as a toast in
    // debug builds only: Config::show_errors), and the component is off until the game mode changes
    // (a scene: its modes show the game-menu button meanwhile). The touch HUD is the only HUD: it
    // comes back by itself, and a component that throws on every state of a mode is said once.
    void Disable(const std::string &component, const char *where, const std::string &what);
    // The component threw and waits for another game mode (Disable).
    bool Off(const std::string &component) const
    {
        return failures_.count(component) != 0;
    }
    // The scene in use, or nothing; "?" when none.
    std::string SceneName() const
    {
        return entry_ ? entry_->name : "?";
    }
    // The game is changing scenes now (its fader is up, a load), or a scene asked to wait for it.
    bool InTransition(double now);
    double WatchDialogue(double now); // seconds until it must look again
    // A tap on an event line that leads somewhere: opens that screen on its subject.
    void Open(const std::string &target);
    void Present(bool visible, bool menu_only);

    Mount &mount_;
    LandCommands::Activation transition_activation_;
    bool transition_accepted_ = false;
    double transition_until_ = -1.0; // ExpectTransition: hidden until then at most (-1 = none)
    std::string transition_what_;
    screens::Router *router_ = nullptr;
    std::span<const SceneEntry> scenes_;
    States states_;
    const SceneEntry *entry_ = nullptr;
    std::unique_ptr<Scene> scene_;
    Input input_;
    View view_;
    std::vector<Line> lines_;
    unsigned next_line_key_ = 0;
    std::set<std::string> reported_;
    // HUD components that threw (Disable), by scene name or "spyglass": the game mode they threw in.
    std::map<std::string, std::string> failures_;
    bool failing_ = false;
    bool disabling_ = false; // Disable is ending the held controls of the scene it turns off
    std::string language_;   // game language last given to the mount
    bool changed_ = true;
    bool visible_ = false;
    bool menu_only_ = false; // shown is only the layout "menu" (the game-menu button)
    bool live_ = false;
    bool interface_ = false;
    double dialogue_since_ = -1.0; // when the game's dialogue began (presentation), -1 = none
    double unseen_since_ = -1.0;   // the game in a dialogue with no screen mounted since, -1 = not
    bool unseen_ended_ = false;    // the watchdog ended that dialogue (once per episode)
    bool declared_ = false;        // the "hudowner" declarations were sent
    double last_tick_ = -1.0;      // mount clock of the previous UI tick; -1 = not started
};

// The props of the spyglass overlay's part (spyglass.cpp).
Json SpyglassProps(const Spyglass &spyglass, Hud &hud);

// The game's time acceleration in four steps, the same at sea and on the world
// map: the "time" button shows the scale the engine runs at (`scale`, x1 = 1; negative: unknown,
// no button) and a tap asks for the next of x1, x2, x4, x8. The engine applies it through the
// scripts, which clamp it to their limits (android_time_scale.hpp). Offered only when the
// gameplay binds all four steps.
void BuildTimeScale(Hud &hud, const Gameplay &gameplay, double scale, View &view, Input &input);

// The analog sticks (the helm at sea and on the world map, the deck walk stick): how hard a
// deflection of the stick (0…100 of its travel) pushes, 0…100. A small dead zone in the middle,
// then a response curve, gentle near the middle and all the way at the end of the travel.
constexpr double kStickDead = 8.0;
int StickStrength(double deflection);

// The controls a scene holds in the engine by name (android_held_controls.hpp), at sea and on
// the world map: a Hold command once per change (the engine holds it until its release), each in
// its latest-value slot.
class HoldSet
{
  public:
    explicit HoldSet(Hud &hud) : hud_(hud)
    {
    }
    // `strength` 1…100 is the analog part of a hold (how far the helm is pulled); 0 is a button.
    void Hold(const std::string &control, bool down, int strength = 0);
    // The helm slider's value (-100…100): `left` or `right` held as hard as it is pulled.
    void Helm(const std::string &value, const std::string &left, const std::string &right);
    // A hold whose gameplay is gone: the engine has let go already.
    void Forget(const States &states);
    // Cancelled input: every hold let go.
    void Release(const Gameplay &gameplay);

  private:
    struct Held
    {
        bool down = false; // held until the pointer's up or cancel: the engine keeps it till then
        int strength = 0;
        unsigned press = 0; // counts the presses: each one is its own latest-value slot
    };
    void Send(const std::string &control, const Held &held, const Gameplay &gameplay);

    Hud &hud_;
    std::map<std::string, Held> held_;
    int turn_ = 0;        // the helm as said: negative to the left, 1…100
    std::string turning_; // the side it holds
};

// The fleet token of a ship by a word of the engine's ship model name ("Lugger" → ship-lugger).
const char *ShipIcon(const std::string &type);

// One member of a party or squadron column (c-hud-plate variant officers / fleet): the game
// picture and its bars, drawn as arcs around it in this order from the outside in, {kind,
// share 0…1}; a share below 0 (unknown) is left out. `tap` makes the cell report its index (verb `member` / `ship`).
struct MemberBar
{
    const char *kind;
    double share;
};
Json MemberCell(const std::string &name, const Picture &picture, std::initializer_list<MemberBar> bars, bool tap);

// The fixture readers of the HUD channels, added to the router's list of state consumers (screens/registry.cpp).
void AddStateConsumers(std::vector<std::unique_ptr<screens::StateConsumer>> &consumers);

// One element of a layout scene as the pack holds it (hud-layout.json with the `rcss` the
// pack build adds).
struct LayoutElement
{
    std::string id, component, rcss, behavior;
    // «Под левую руку» (Host::SetDisplayOptions): the pack's mirrored placement and the props it changes
    // (mobile-ui/design build.py hud_mirrored), used instead of rcss and on top of props.
    std::string rcss_mirrored;
    Json props_mirrored = Json::object();
    Json props = Json::object();
    Json data = Json::object(); // data-* attributes: configuration of the behaviour
    // A top status the toasts must not cover (pack "toastClear": its bottom below the safe top, dp);
    // 0 = none. While it is shown the host's toast stack starts below it.
    float toast_clear = 0.0f;
    std::vector<LayoutElement> children;
};
using Layout = std::unordered_map<std::string, std::vector<LayoutElement>>;
// False with `error` set when the file is not a layout this runtime understands.
bool ParseLayout(const Json &file, Layout &out, std::string *error);
// Empty when `view` can be drawn with `layout`; otherwise what is wrong.
std::string CheckView(const Layout &layout, const View &view);

} // namespace storm::ui::hud
