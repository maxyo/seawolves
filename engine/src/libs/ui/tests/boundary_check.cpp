// Host checks of the UI exception boundary: a screen, a screen's handler, a state consumer
// or a HUD scene that throws must not end the game session. Each throw is an error line naming
// the screen or component and the exception, a danger toast for the player, and a logged
// recovery: a failed screen closes the interface through the engine's back immediately,
// without constructing a generic replacement; a HUD scene goes off until the game mode changes and comes back.
// Fake screens and scenes only; no GL context, no pack, no game data.
#include "fakes.h"
#include "../src/hud/hud.h"

#include <cstdio>
#include <stdexcept>

using namespace storm::ui;
using namespace storm::ui::screens;
using namespace storm::ui::hud;

static bool Has(const std::vector<std::string> &lines, const std::string &part)
{
    for (const std::string &line : lines)
        if (line.find(part) != std::string::npos)
            return true;
    return false;
}

// What the fake screens do, set by each case.
static bool throw_in_build = false;
static bool throw_in_handler = false;
static bool throw_in_back = false;
static int generic_created = 0;

static std::unique_ptr<Screen> CountingGeneric()
{
    ++generic_created;
    return CreateGenericScreen();
}

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

static const ScreenEntry kScreens[] = {
    {"throwing", [] { return std::unique_ptr<Screen>(std::make_unique<ThrowingScreen>()); }}};

static InterfaceModel Model(uint64_t opening, uint64_t revision)
{
    InterfaceModel model;
    model.screen = "throwing";
    model.language = "english";
    model.opening = opening;
    model.revision = revision;
    Widget ok;
    ok.node = "OK";
    ok.type = "button";
    ok.label = "Fine";
    model.widgets.push_back(ok);
    return model;
}

static void ExpectClosed(const Router &router, const FakeSurface &surface, uint64_t opening)
{
    Expect(!router.Active() && router.Using().empty() && surface.tag.empty() && surface.hides > 0,
           "the failed interface is hidden and has no active screen");
    Expect(router.Model().screen.empty() && router.Model().opening == 0 && router.Rejection().empty(),
           "the failed interface model and rejection are cleared");
    Expect(surface.commands.size() == 1 && surface.commands[0] == "back" && surface.actions[0].screen == "throwing" &&
               surface.actions[0].opening == opening,
           "exactly one normal engine back targets the failed opening");
    Expect(surface.alerts.empty(),
           "the defect is an error line, no alert of its own (Config::show_errors shows errors)");
    Expect(generic_created == 0, "the boundary never constructs a generic replacement");
}

static void CheckScreenBuild()
{
    FakeSurface surface;
    surface.now = 5.0;
    generic_created = 0;
    Router router(surface, kScreens, CountingGeneric);
    throw_in_build = true;
    router.SetInterface(Model(1, 1));
    ExpectClosed(router, surface, 1);
    Expect(Has(surface.errors, "screen throwing (interface throwing #1 revision 1) threw in Build: bad widget in "
                               "Build; the interface is closed"),
           "the error line names the screen, the interface, the call, the exception and closure");
    Expect(!router.OnEvent({"ok", "click", ""}), "the failed build leaves no live event bindings");
    // A new opening tries the ported screen again.
    throw_in_build = false;
    router.SetInterface(Model(2, 1));
    Expect(router.Using() == "throwing" && surface.tag == "s-throwing", "a new opening draws the ported screen again");
}

static void CheckHandler()
{
    FakeSurface surface;
    surface.now = 5.0;
    generic_created = 0;
    Router router(surface, kScreens, CountingGeneric);
    throw_in_build = false;
    throw_in_handler = true;
    router.SetInterface(Model(1, 1));
    Expect(router.Using() == "throwing", "the ported screen is in use");
    Expect(router.OnEvent({"ok", "click", ""}), "the event was handled (by the boundary)");
    ExpectClosed(router, surface, 1);
    Expect(Has(surface.errors, "threw in the handler of ok click: bad row in a handler"),
           "the error line names the event");
    Expect(!router.OnEvent({"ok", "click", ""}), "a failed handler leaves no live event bindings");
    throw_in_handler = false;
}

static void CheckBack()
{
    FakeSurface surface;
    surface.now = 5.0;
    generic_created = 0;
    Router router(surface, kScreens, CountingGeneric);
    throw_in_back = true;
    router.SetInterface(Model(5, 1));
    Expect(router.WantsBack(), "the screen owns back before failure");
    router.Back();
    ExpectClosed(router, surface, 5);
    Expect(!router.WantsBack() && router.Modals().empty(), "the failed screen no longer owns back or modals");
    Expect(Has(surface.errors, "threw in Back: bad Back"), "the error line names Back");
    throw_in_back = false;
}

static void CheckTextAnswer()
{
    FakeSurface surface;
    surface.now = 5.0;
    generic_created = 0;
    Router router(surface, kScreens, CountingGeneric);
    router.SetInterface(Model(6, 1));
    Expect(router.OnEvent({"edit", "click", ""}) && !surface.text.token.empty(), "the screen requested a text editor");
    router.SubmitText(surface.text.token, "answer", true);
    ExpectClosed(router, surface, 6);
    Expect(Has(surface.errors, "threw in the text editor's answer: bad text answer"),
           "the error line names the text answer");
    router.SubmitText(surface.text.token, "late answer", true);
    Expect(surface.commands.size() == 1 && surface.errors.size() == 1 && surface.alerts.empty(),
           "a late editor answer cannot revive the failed screen or send another command");
}

class ThrowingGeneric final : public Screen
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
    Json Build(const Admission &, Bindings &) override
    {
        throw std::logic_error("generic failed");
    }
};

// A generic screen is still allowed for an unported interface; its own failure closes it.
static void CheckGenericFails()
{
    FakeSurface surface;
    surface.now = 5.0;
    Router router(surface, kScreens, [] { return std::unique_ptr<Screen>(std::make_unique<ThrowingGeneric>()); });
    InterfaceModel other = Model(4, 1);
    other.screen = "unported";
    router.SetInterface(other);
    Expect(!router.Active() && router.Using().empty() && surface.tag.empty() && surface.hides > 0,
           "the generic layout alone: hidden and closed at once");
    Expect(surface.commands.size() == 1 && surface.commands[0] == "back" && surface.actions[0].screen == "unported" &&
               surface.actions[0].opening == 4,
           "the generic failure sends exactly one back for its opening");
    Expect(Has(surface.errors, "screen generic (interface unported #4 revision 1) threw in Build: generic failed; the "
                               "interface is closed"),
           "the generic error names the failure and closure");
    Expect(surface.alerts.empty(), "the generic failure is an error line, no alert of its own");
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
    CheckHandler();
    CheckBack();
    CheckTextAnswer();
    CheckGenericFails();
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
