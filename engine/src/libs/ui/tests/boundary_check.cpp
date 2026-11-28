// Host checks of the UI exception boundary: a screen, a screen's handler, a state consumer
// or a HUD scene that throws must not end the game session. Each throw is an error line naming
// the screen or component and the exception, and a logged recovery: a failed screen gives its opening to the generic
// layout of the same window with a note for the player, and the next opening tries it again; the generic layout
// failing, or the screen of an engine-made model, closes the interface through the engine's back; a screen that
// cannot draw this game's window (a required node of another type) is not used for it; a HUD scene goes off until
// the game mode changes and comes back.
// Fake screens and scenes only; no GL context, no pack, no game data.
#include "fakes.h"
#include "../src/hud/hud.h"

#include <cstdio>
#include <functional>
#include <stdexcept>

using namespace storm::ui;
using namespace storm::ui::screens;
using namespace storm::ui::hud;

// What the fake screens do, set by each case.
static bool throw_in_build = false;
static bool throw_in_handler = false;
static bool throw_in_back = false;
static bool generic_throws = false;
static int generic_created = 0;

class ThrowingScreen final : public Screen
{
  public:
    const char *Component() const override
    {
        return "s-throwing";
    }
    std::span<const NodeRule> Nodes() const override
    {
        static const NodeRule rules[] = {{"OK", "button", true}};
        return rules;
    }
    Json Build(const Admission &admitted, Bindings &bindings) override
    {
        if (throw_in_build)
            throw std::runtime_error("bad widget in Build");
        bindings.On("ok", "", [](const Event &) {
            if (throw_in_handler)
                throw std::out_of_range("bad row in a handler");
        });
        bindings.On("edit", "click", [&bindings](const Event &event) {
            bindings.AskText(event, "", "Probe", false,
                             [](const std::string &) { throw std::runtime_error("bad text answer"); });
        });
        return Json{{"label", admitted.Label("OK")}};
    }
    bool OwnsBack() const override
    {
        return throw_in_back;
    }
    void Back(Bindings &) override
    {
        throw std::runtime_error("bad Back");
    }
};

class Generic final : public Screen
{
  public:
    const char *Component() const override
    {
        return "s-generic";
    }
    std::span<const NodeRule> Nodes() const override
    {
        return {};
    }
    Json Build(const Admission &admitted, Bindings &) override
    {
        if (generic_throws)
            throw std::logic_error("generic failed");
        return Json{{"widgets", admitted.known.size()}};
    }
};

static std::unique_ptr<Screen> CountingGeneric()
{
    ++generic_created;
    return std::make_unique<Generic>();
}

static const ScreenEntry kScreens[] = {
    {"throwing", [] { return std::unique_ptr<Screen>(std::make_unique<ThrowingScreen>()); }}};

// A router whose generic layout is CountingGeneric, as the touch interface's scripts attach theirs.
struct Bench
{
    FakeSurface surface;
    Router router{surface, kScreens};
    Bench()
    {
        surface.now = 5.0;
        generic_created = 0;
        generic_throws = false;
        throw_in_build = throw_in_handler = throw_in_back = false;
        router.SetScriptScreens({{kAnyInterface, CountingGeneric}});
    }
};

static InterfaceModel Model(uint64_t opening, uint64_t revision, const char *type = "button")
{
    InterfaceModel model;
    model.screen = "throwing";
    model.language = "english";
    model.opening = opening;
    model.revision = revision;
    Widget ok;
    ok.node = "OK";
    ok.type = type;
    ok.label = "Fine";
    ok.actions = {"press"};
    model.widgets.push_back(ok);
    return model;
}

static void ExpectClosed(const Router &router, const FakeSurface &surface, uint64_t opening, const char *screen)
{
    Expect(!router.Active() && router.Using().empty() && surface.tag.empty() && surface.hides > 0,
           "the failed interface is hidden and has no active screen");
    Expect(router.Model().screen.empty() && router.Model().opening == 0 && router.Rejection().empty(),
           "the failed interface model and rejection are cleared");
    Expect(surface.commands.size() == 1 && surface.commands[0] == "back" && surface.actions[0].screen == screen &&
               surface.actions[0].opening == opening,
           "exactly one normal engine back targets the failed opening");
    Expect(surface.alerts.empty(), "a closed interface has no note of its own (Config::show_errors shows errors)");
}

// The failed screen's opening is the generic layout's, with the note; the game's window stays open.
static void ExpectFellBack(const Router &router, const FakeSurface &surface, const std::string &error)
{
    Expect(router.Active() && router.Using() == "generic" && surface.tag == "s-generic",
           "the generic layout draws the window of the failed screen");
    Expect(surface.commands.empty(), "the interface is not closed: no engine back");
    Expect(surface.errors.size() == 1 && Has(surface.errors, error + "; the generic layout draws this opening"),
           "one error line names the screen, the interface, the call and the exception");
    Expect(surface.alerts.size() == 1 && surface.alerts[0] == "<common.screen_failed>",
           "the player is told the screen failed and the window is shown as it is");
}

static void CheckScreenBuild()
{
    Bench bench;
    throw_in_build = true;
    bench.router.SetInterface(Model(1, 1));
    ExpectFellBack(bench.router, bench.surface,
                   "screen throwing (interface throwing #1 revision 1) threw in Build: bad widget in Build");
    Expect(!bench.router.OnEvent({"ok", "click", ""}), "the failed screen leaves no event bindings");
    bench.router.SetInterface(Model(1, 2));
    Expect(bench.router.Using() == "generic" && CountHaving(bench.surface.errors, "threw") == 1,
           "the rest of the opening stays the generic layout's");
    // A new opening tries the screen again.
    throw_in_build = false;
    bench.router.SetInterface(Model(2, 1));
    Expect(bench.router.Using() == "throwing" && bench.surface.tag == "s-throwing",
           "a new opening draws the screen again");
}

// The game's script ended the interface (its segment unloaded, the next interface frames later): the screen stays as
// drawn and is not built again, though its build would now fail on the script state that went with it.
static void CheckLeft()
{
    Bench bench;
    throw_in_handler = true;
    bench.router.SetInterface(Model(1, 1));
    bench.router.LeaveInterface();
    throw_in_build = true;
    bench.router.RebuildActive();
    bench.surface.now = 50.0;
    bench.router.Tick();
    Expect(bench.surface.errors.empty() && bench.surface.commands.empty() && bench.surface.tag == "s-throwing",
           "a left interface's screen stays drawn and is not built again");
    Expect(bench.router.OnEvent({"ok", "click", ""}) && bench.surface.errors.empty(),
           "a tap on a left interface is dropped");
    throw_in_build = false;
    throw_in_handler = false;
    bench.router.SetInterface(Model(2, 1));
    Expect(bench.router.Using() == "throwing" && bench.surface.errors.empty(), "the next opening is built");
}

static void CheckHandler()
{
    Bench bench;
    throw_in_handler = true;
    bench.router.SetInterface(Model(1, 1));
    Expect(bench.router.Using() == "throwing", "the screen is in use");
    Expect(bench.router.OnEvent({"ok", "click", ""}), "the event was handled (by the boundary)");
    ExpectFellBack(bench.router, bench.surface, "threw in the handler of ok click: bad row in a handler");
    Expect(!bench.router.OnEvent({"ok", "click", ""}), "a failed handler leaves no event bindings");
}

static void CheckBack()
{
    Bench bench;
    throw_in_back = true;
    bench.router.SetInterface(Model(5, 1));
    Expect(bench.router.WantsBack(), "the screen owns back before failure");
    bench.router.Back();
    ExpectFellBack(bench.router, bench.surface, "threw in Back: bad Back");
    Expect(!bench.router.WantsBack() && bench.router.Modals().empty(),
           "the failed screen no longer owns back or modals");
}

static void CheckTextAnswer()
{
    Bench bench;
    bench.router.SetInterface(Model(6, 1));
    Expect(bench.router.OnEvent({"edit", "click", ""}) && !bench.surface.text.token.empty(),
           "the screen requested a text editor");
    bench.router.SubmitText(bench.surface.text.token, "answer", true);
    ExpectFellBack(bench.router, bench.surface, "threw in the text editor's answer: bad text answer");
    bench.router.SubmitText(bench.surface.text.token, "late answer", true);
    Expect(bench.surface.commands.empty() && bench.surface.errors.size() == 1 && bench.surface.alerts.size() == 1,
           "a late editor answer cannot revive the failed screen or send anything");
}

// The generic layout for an interface no screen draws; its own failure closes it.
static void CheckGenericFails()
{
    Bench bench;
    generic_throws = true;
    InterfaceModel other = Model(4, 1);
    other.screen = "unported";
    bench.router.SetInterface(other);
    ExpectClosed(bench.router, bench.surface, 4, "unported");
    Expect(Has(bench.surface.errors, "screen generic (interface unported #4 revision 1) threw in Build: generic "
                                     "failed; the interface is closed"),
           "the generic error names the failure and closure");
}

// A failed screen whose generic layout fails too: closed, once (no loop between the two).
static void CheckBothFail()
{
    Bench bench;
    generic_throws = true;
    throw_in_build = true;
    bench.router.SetInterface(Model(3, 1));
    ExpectClosed(bench.router, bench.surface, 3, "throwing");
    Expect(bench.surface.errors.size() == 2 && generic_created == 1,
           "the screen's error, then the generic layout's, which closes the interface");
}

// A model the engine makes for a screen of its own has no game window to show as it is: its failed screen closes it.
static void CheckEngineMade()
{
    Bench bench;
    throw_in_build = true;
    InterfaceModel model = Model(7, 1);
    model.engine = true;
    bench.router.SetInterface(model);
    ExpectClosed(bench.router, bench.surface, 7, "throwing");
    Expect(generic_created == 0, "no generic layout stands in for an engine-made screen");
}

// A screen whose required node this game's window has with another type: the generic layout draws the window, one
// error line names the screen and the node with both types, and the window is not closed; later openings do not
// check or log again.
static void CheckUnfit()
{
    Bench bench;
    bench.router.SetInterface(Model(1, 1, "image"));
    Expect(!bench.router.Active() && bench.surface.commands.empty(),
           "a rejected first model waits for the model to settle");
    bench.surface.now = 7.0;
    bench.router.Tick();
    Expect(bench.router.Using() == "generic" && bench.surface.tag == "s-generic" && bench.surface.commands.empty(),
           "the generic layout draws the window and it stays open");
    Expect(bench.surface.errors.size() == 1 &&
               Has(bench.surface.errors, "screen throwing cannot draw this game's window: required nodes missing or "
                                         "mistyped: OK(button; found image); the generic layout draws it"),
           "one error line names the screen and the node with the type expected and the type found");
    Expect(bench.router.Rejection() == "OK(button; found image)", "the rejection says why");
    bench.router.SetInterface(Model(2, 1, "image"));
    Expect(bench.router.Using() == "generic" && bench.surface.errors.size() == 1,
           "the next opening goes to the generic layout at once, without another line");
    // A new list of screens (another program) checks again.
    bench.router.SetScriptScreens({{kAnyInterface, CountingGeneric}});
    bench.router.SetInterface(Model(3, 1));
    Expect(bench.router.Using() == "throwing", "a window of the type it needs is drawn by the screen");
    // Once a screen has drawn its window, a required node gone later fails that opening only.
    bench.router.SetInterface(Model(4, 1));
    bench.surface.now = 9.0;
    bench.router.SetInterface(Model(4, 2, "image"));
    Expect(bench.router.Using() == "generic" && bench.surface.alerts.size() == 1 &&
               CountHaving(bench.surface.errors, "the generic layout draws this opening") == 1,
           "a required node gone in a later revision gives the rest of the opening to the generic layout");
    bench.router.SetInterface(Model(5, 1));
    Expect(bench.router.Using() == "throwing", "and the next opening tries the screen again");
}

// Bad states of real channels: whatever a consumer does with them, the screen in use stays.
static void CheckConsumers()
{
    FakeSurface surface;
    surface.now = 5.0;
    Router router(surface, kScreens);
    throw_in_build = false;
    router.SetInterface(Model(1, 1));
    for (const char *channel : {"gameOver", "dialogue", "launcher", "titleIntent", "loadFailure", "saveFailure",
                                "appSettings", "gameplay", "ship", "worldMap"})
    {
        router.SetState(channel, Json::array({1, "two", nullptr}));
        router.SetState(channel, Json("not an object"));
    }
    Expect(router.Using() == "throwing" && surface.tag == "s-throwing",
           "a channel's bad state leaves the screen in use alone");
}

// --- HUD -------------------------------------------------------------------------------

static bool scene_throws_in_build = false, scene_throws_in_tick = false, scene_throws_in_handler = false;
static int scenes_created = 0;

class ThrowingScene final : public Scene
{
  public:
    ThrowingScene()
    {
        ++scenes_created;
    }
    void Build(const States &, View &view, Input &input) override
    {
        if (scene_throws_in_build)
            throw std::runtime_error("scene build failed");
        view.layout = "probe";
        view.elements["button"] = Json::object();
        input.On("button", "click", [](const Event &) {
            if (scene_throws_in_handler)
                throw std::runtime_error("scene handler failed");
        });
    }
    double Tick(const States &, double) override
    {
        if (scene_throws_in_tick)
            throw std::runtime_error("scene tick failed");
        return 3600.0;
    }
};

static const char *const kModes[] = {"explore"};
static const SceneEntry kScenes[] = {
    {"probe", kModes, [](Hud &) -> std::unique_ptr<Scene> { return std::make_unique<ThrowingScene>(); }}};

struct HudBench
{
    FakeMount mount;
    Hud hud{mount, kScenes};
    HudBench()
    {
        hud.Edit().gameplay.mode = "explore";
        hud.Edit().gameplay.revision = 1;
        Presentation &shown = hud.Edit().presentation;
        shown.known = true;
        shown.live = true;
        hud.Tick();
    }
};

// Throws `flag` once (a build, a tick or an event), then clears it.
static void ThrowOnce(HudBench &bench, bool &flag, bool event)
{
    flag = true;
    if (event)
        bench.hud.OnEvent({"button", "click", ""});
    else
        bench.hud.Rebuild();
    bench.hud.Tick();
    flag = false;
}

static void CheckHudComponent(const char *where, bool &flag, bool event)
{
    HudBench bench;
    Expect(bench.hud.Visible() && !bench.hud.MenuOnly() && bench.mount.shown.layout == "probe",
           std::string(where) + ": the scene draws the HUD");
    ThrowOnce(bench, flag, event);
    Expect(Has(bench.mount.errors, std::string("HUD probe threw in ")) &&
               Has(bench.mount.errors, "off until the game mode changes"),
           std::string(where) + ": the error line names the component, the call and the exception");
    Expect(bench.mount.alerts.empty(),
           std::string(where) + ": an error line, no alert of its own (Config::show_errors shows errors)");
    Expect(bench.hud.MenuOnly() && bench.mount.shown.layout == "menu",
           std::string(where) + ": the game-menu button stays");
    // Off in that mode: later states of it, however late, do not make the scene again.
    const int created = scenes_created;
    bench.hud.Edit().gameplay.revision = 2;
    bench.mount.now += 600.0;
    bench.hud.Tick();
    Expect(scenes_created == created && bench.hud.MenuOnly(), std::string(where) + ": the scene stays off in its mode");
    // The touch HUD is the only HUD: in the next mode with the scene it comes back by itself, logged.
    bench.hud.Edit().gameplay.mode = "sailing";
    bench.hud.Edit().gameplay.revision = 3;
    bench.hud.Tick();
    bench.hud.Edit().gameplay.mode = "explore";
    bench.hud.Edit().gameplay.revision = 4;
    bench.hud.Tick();
    Expect(scenes_created == created + 1 && bench.hud.Visible() && !bench.hud.MenuOnly() &&
               bench.mount.shown.layout == "probe",
           std::string(where) + ": the scene comes back after a mode change");
    Expect(Has(bench.mount.logs, "HUD probe: on again"), std::string(where) + ": its return is logged");
    // Throwing again: off again, and back after the next change.
    ThrowOnce(bench, flag, event);
    Expect(bench.mount.alerts.empty() && bench.hud.MenuOnly(), std::string(where) + ": off again after a new throw");
}

int main()
{
    CheckScreenBuild();
    CheckLeft();
    CheckHandler();
    CheckBack();
    CheckTextAnswer();
    CheckGenericFails();
    CheckBothFail();
    CheckEngineMade();
    CheckUnfit();
    CheckConsumers();
    CheckHudComponent("Build", scene_throws_in_build, false);
    CheckHudComponent("Tick", scene_throws_in_tick, false);
    CheckHudComponent("event handler", scene_throws_in_handler, true);
    if (failures)
    {
        std::fprintf(stderr, "%d UI boundary check(s) failed\n", failures);
        return 1;
    }
    std::printf("UI boundary checks passed\n");
    return 0;
}
