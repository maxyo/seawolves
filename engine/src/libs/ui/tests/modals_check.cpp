// Host checks for the modals of every screen (Bindings::Modal): a tap beside a modal (its scrim
// reports `dismiss`) and the back key go one way for all of them.
//   - A modal with a cancel closes as its cancel button would: the same engine actions, the same
//     view afterwards.
//   - A modal that must be answered by its buttons (a mandatory choice of the game, a password in
//     the middle of an import) stays: nothing goes to the engine, the modal is told to show the
//     refusal (Surface::Refuse: C-Dialog shakes) and a log line says why. Never silence.
//   - The back key does exactly what the tap beside does.
// The walk covers every modal the screens declare (the cases below open each one) and every model
// in tests/models that opens with a modal of its own. The runtime part (whole host, test pack)
// checks that a tap on the scrim never reaches what lies under it, and the refusal's shake.
// Synthetic models only; no GL context, no game data.
#include "fakes.h"
#include "../tools/script_game.h"
#include "../src/screens/screen.h"
#include "storm_ui/ui.h"

#include <RmlUi/Core.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <unistd.h>

using namespace storm::ui;
using namespace storm::ui::screens;
namespace fs = std::filesystem;

static std::string models;

static std::string Topmost(const Router &router)
{
    const auto modals = router.Modals();
    return modals.empty() ? std::string() : modals.back().first + (modals.back().second ? "" : " (must be answered)");
}

// One modal: how to open it, its id, and the event of its own cancel (none: it must be answered).
struct Case
{
    const char *name;
    std::function<void(Router &)> open;
    const char *modal;
    Event cancel; // id empty: the modal must be answered by its buttons
};

static void Interface(Router &router, const char *file)
{
    router.SetInterface(Load(models, file));
}

static Json LauncherState(Json data)
{
    return Json{{"screen", "launcher"}, {"language", "english"}, {"launcher", std::move(data)}};
}

static Json DownloadSets()
{
    return Json::array({Json{{"id", "1"},
                             {"kind", "torrent"},
                             {"name", "Full"},
                             {"state", "downloading"},
                             {"downloaded", 50.0},
                             {"wanted", 100.0},
                             {"rate", 10.0},
                             {"main", "pause"},
                             {"menu", {"details", "delete"}}}});
}

static std::vector<Case> Cases()
{
    const auto file = [](const char *name) { return [name](Router &router) { Interface(router, name); }; };
    std::vector<Case> cases = {
        {"delete-save question", file("saves-confirm.json"), "confirm", {"confirm-cancel", "click", ""}},
        {"leave-the-game question", file("game-menu-exit.json"), "confirm", {"confirm-cancel", "click", ""}},
        {"leave-the-battle card", file("reading-leavebattle.json"), "card", {"cancel", "click", ""}},
        // The model's encounter cannot be avoided: the game does not offer «Плыть дальше».
        {"forced world map encounter", file("reading-map.json"), "card", {}},
        {"world map encounter card",
         [](Router &router) {
             InterfaceModel map = Load(models, "reading-map.json");
             for (Widget &widget : map.widgets)
                 widget.enabled = true;
             router.SetInterface(map);
         },
         "card",
         {"cancel", "click", ""}},
        {"encounter card without a way past", file("reading-dirsailenc.json"), "card", {}},
        {"crew wages card", file("reading-salary.json"), "card", {}},
        {"notice card", file("reading-colonyinfo.json"), "card", {"card-close", "click", ""}},
        {"rest (a script screen)", file("rest.json"), "rest", {"rest-close", "click", ""}},
        {"shipyard buy question", file("shipyard-confirm-buy.json"), "sheet", {"sheet-cancel", "click", ""}},
        {"defectors (quantity)", file("transfer-hire.json"), "amount", {}},
        {"prisoners' fate", file("transfer-crew-capture.json"), "modal", {}},
        {"ship without a captain",
         [](Router &router) {
             ScriptGame::Get().Set("sMessageMode", "ShipDeadAsk"); // the window asks about the ship we leave
             Interface(router, "transfer-fate.json");
         },
         "modal",
         {"answer-1", "click", ""}},
        {"hero subject list",
         [](Router &router) {
             Interface(router, "hero.json");
             router.OnEvent({"header", "subject-list", ""});
         },
         "header-subjects",
         {"header-subjects-close", "click", ""}},
        {"reading sheet",
         [](Router &router) {
             Interface(router, "hero.json");
             router.OnEvent({"card", "portrait", ""});
         },
         "extras-reading",
         {"extras-reading-close", "click", ""}},
        {"«Прочее» sheet",
         [](Router &router) {
             InterfaceModel many;
             many.screen = "mod_many";
             many.opening = 3;
             many.widgets = {Make("T", "text", "Choose"), Make("B_A", "button", "One", {"press"}),
                             Make("B_B", "button", "Two", {"press"}), Make("B_C", "button", "Three", {"press"}),
                             Make("B_D", "button", "Four", {"press"})};
             router.SetInterface(many);
             router.OnEvent({"extras-more", "click", ""});
         },
         "extras",
         {"extras-more", "click", ""}},
        {"archive password during an import",
         [](Router &router) {
             router.SetState(
                 "launcher",
                 LauncherState(
                     {{"phase", "busy"},
                      {"hasGame", false},
                      {"import", {{"stage", "reading"}, {"canCancel", true}, {"password", {{"for", "x.zip"}}}}}}));
         },
         "password",
         {}},
        {"downloads «⋯» sheet",
         [](Router &router) {
             router.SetState("launcher", Json{{"screen", "downloads"},
                                              {"language", "english"},
                                              {"downloads", {{"loaded", true}, {"sets", DownloadSets()}}}});
             router.OnEvent({"set-1", "menu", ""});
         },
         "sheet",
         {"sheet-close", "click", ""}},
        {"downloads files question",
         [](Router &router) {
             router.SetState(
                 "launcher",
                 Json{{"screen", "downloads"},
                      {"language", "english"},
                      {"downloads",
                       {{"loaded", true},
                        {"sets", DownloadSets()},
                        {"dialog",
                         {{"kind", "candidates"},
                          {"id", "1"},
                          {"candidates",
                           {{{"label", "Game"}, {"kind", "loose-game-candidate"}, {"files", 3}, {"bytes", 10}}}},
                          {"all", {{"files", 9}, {"bytes", 40}}}}}}}});
         },
         "dialog",
         {"dialog-cancel", "click", ""}},
    };
    return cases;
}

// The tap beside the modal and the back key, each on a router of its own opened the same way.
static void Walk(const Case &c)
{
    const bool dismissible = !c.cancel.id.empty();
    const std::string expected = std::string(c.modal) + (dismissible ? "" : " (must be answered)");
    FakeSurface tapped_surface, back_surface;
    Router tapped(tapped_surface), back(back_surface);
    Scripted(tapped);
    Scripted(back);
    c.open(tapped);
    c.open(back);
    Expect(Topmost(tapped) == expected && tapped.WantsBack(),
           std::string(c.name) + ": the modal " + expected + " is declared, got «" + Topmost(tapped) + "»");
    const Json before = tapped_surface.props;
    tapped_surface.commands.clear();
    back_surface.commands.clear();
    tapped.OnEvent({c.modal, "dismiss", ""});
    back.Back();
    if (dismissible)
    {
        FakeSurface cancelled_surface;
        Router cancelled(cancelled_surface);
        Scripted(cancelled);
        c.open(cancelled);
        cancelled_surface.commands.clear();
        cancelled.OnEvent(c.cancel);
        Expect(tapped_surface.commands == cancelled_surface.commands,
               std::string(c.name) + ": a tap beside it is its cancel: " + List(tapped_surface.commands) + " vs " +
                   List(cancelled_surface.commands));
        // A local close leaves the same view; what went to the game marks a different element busy
        // until the game answers with a new model, so there only the modals left are compared.
        Expect((!tapped_surface.commands.empty() || tapped_surface.props == cancelled_surface.props) &&
                   tapped.Modals() == cancelled.Modals(),
               std::string(c.name) + ": the view after the tap is the view after the cancel");
        Expect(!tapped_surface.commands.empty() || Topmost(tapped) != expected,
               std::string(c.name) + ": the tap closes it (or asks the game to)");
        Expect(tapped_surface.refused.empty(), std::string(c.name) + ": nothing refused");
    }
    else
    {
        Expect(tapped_surface.commands.empty() && tapped_surface.props == before && Topmost(tapped) == expected,
               std::string(c.name) + ": a tap beside it answers nothing: " + List(tapped_surface.commands));
        Expect(tapped_surface.refused == std::vector<std::string>{c.modal},
               std::string(c.name) + ": the modal shows the refusal (shakes): " + List(tapped_surface.refused));
        bool logged = false;
        for (const std::string &line : tapped_surface.logs)
            logged = logged || line.find(std::string("modal ") + c.modal + " must be answered") != std::string::npos;
        Expect(logged, std::string(c.name) + ": the refusal is logged");
    }
    Expect(back_surface.commands == tapped_surface.commands && back_surface.props == tapped_surface.props &&
               back_surface.refused == tapped_surface.refused,
           std::string(c.name) + ": the back key does what the tap beside does: " + List(back_surface.commands) +
               " vs " + List(tapped_surface.commands));
    Expect(tapped_surface.errors.empty() && back_surface.errors.empty(),
           std::string(c.name) + ": no errors: " + List(tapped_surface.errors) + List(back_surface.errors));
}

// Every model that opens with a modal of its own: the tap beside it and the back key agree, and
// the modal either goes (or asks the game) or is refused visibly.
static void EveryModel()
{
    int seen = 0;
    for (const auto &entry : fs::directory_iterator(models))
    {
        const std::string text = Read(entry.path());
        if (text.find("\"widgets\"") == std::string::npos)
            continue; // a state channel's model
        const std::string name = entry.path().filename().string();
        FakeSurface tapped_surface, back_surface;
        Router tapped(tapped_surface), back(back_surface);
        Scripted(tapped);
        Scripted(back);
        tapped.SetInterface(Load(models, name));
        back.SetInterface(Load(models, name));
        const auto modals = tapped.Modals();
        if (modals.empty())
            continue;
        ++seen;
        const auto [id, dismissible] = modals.back();
        tapped_surface.commands.clear();
        back_surface.commands.clear();
        tapped.OnEvent({id, "dismiss", ""});
        back.Back();
        Expect(back_surface.commands == tapped_surface.commands && back_surface.props == tapped_surface.props &&
                   back_surface.refused == tapped_surface.refused,
               name + ": the back key does what a tap beside " + id + " does");
        if (dismissible)
            Expect(tapped_surface.refused.empty() && (!tapped_surface.commands.empty() || Topmost(tapped) != id),
                   name + ": a tap beside " + id + " closes it");
        else
            Expect(tapped_surface.commands.empty() && tapped_surface.refused == std::vector<std::string>{id},
                   name + ": " + id + " refuses visibly");
        Expect(tapped_surface.errors.empty(), name + ": no errors: " + List(tapped_surface.errors));
    }
    Expect(seen >= 10, "the models open modals of their own: " + std::to_string(seen));
}

// A tap beside a modal is the modal's alone: a `dismiss` of a modal the screen did not declare is an
// error, never a quiet drop, and binding a scrim by hand (Bindings::On … "dismiss") is an error too.
static void Undeclared()
{
    FakeSurface surface;
    Router router(surface);
    router.SetInterface(Load(models, "store.json"));
    Expect(router.Modals().empty() && !router.OnEvent({"confirm", "dismiss", ""}) && !surface.errors.empty(),
           "a tap beside a modal the screen does not show is reported");
}

// ---- the runtime: what a tap on the scrim reaches ----

// The test pack plus the saves screen and the reading card reduced to a C-Dialog (the pack's
// structure: the scrim first, data-action="dismiss", the box over it) and a row of the screen
// under the scrim, where the finger lands.
static fs::path MakePack(const fs::path &base, const fs::path &font)
{
    const fs::path pack = CopyPack(base, "modals", font);
    Json components = Json::parse(Read(pack / "components.json"));
    components["components"].push_back(Json::parse(R"({
        "tag": "c-dialog", "behaviors": ["press"], "constants": {"pressRoot": false},
        "props": {"title": {"type": "text", "default": ""}},
        "template": "<div class=\"scrim\" data-action=\"dismiss\" style=\"position: absolute; left: 0; top: 0; right: 0; bottom: 0;\"></div><div class=\"box\" style=\"position: relative; width: 200dp; height: 100dp;\">{{title}}</div>"})"));
    components["components"].push_back(Json::parse(R"({
        "tag": "c-line", "behaviors": ["press"], "props": {"title": {"type": "text", "default": ""}},
        "template": "<div style=\"display: block; width: 100%; height: 100%;\">{{title}}</div>"})"));
    // The reading card is the application's script screen: its props as the pack's spec shapes them.
    Json reading_actions, saves_props;
    const Json app_components = Json::parse(Read(application_pack / "components.json"));
    for (const Json &component : app_components.at("components"))
        if (component.at("tag") == "s-reading")
            reading_actions = component["props"]["actions"];
        else if (component.at("tag") == "s-saves")
            saves_props = component["props"];
    const char *screen_style = "position: absolute; left: 0; top: 0; right: 0; bottom: 0; font-family: LatoLatin;";
    const char *dialog_style = "display: flex; position: absolute; left: 0; top: 0; right: 0; bottom: 0; z-index: 20; "
                               "justify-content: center; align-items: center;";
    for (const auto &[tag, dialog, test] :
         {std::tuple{"s-saves", "confirm", "confirm"}, std::tuple{"s-reading", "card", "mode"}})
    {
        Json screen = {{"tag", tag},
                       {"props",
                        {{"records", {{"type", "json"}, {"default", Json::array()}, {"shape", Json::array({{}})}}},
                         {"confirm", {{"type", "json"}, {"default", nullptr}, {"shape", {{"$null", Json::object()}}}}},
                         {"mode", {{"type", "text"}, {"default", ""}}},
                         {"title", {{"type", "text"}, {"default", ""}}},
                         {"icon", {{"type", "text"}, {"default", ""}}},
                         {"text", {{"type", "text"}, {"default", ""}}},
                         {"actions", reading_actions},
                         {"extras", {{"type", "json"}, {"default", Json::object()}, {"shape", Json::object()}}}}},
                       {"template", std::string("<div style=\"") + screen_style +
                                        "\"><c-line id=\"under\" title=\"under\" style=\"display: block; position: "
                                        "absolute; left: 0; top: 0; width: 800dp; height: 60dp;\"/><t-if test=\"" +
                                        test + "\"><c-dialog id=\"" + dialog + "\" title=\"modal\" style=\"" +
                                        dialog_style + "\"/></t-if></div>"}};
        // The saves list is the application's script screen too: every prop of the pack's spec.
        if (std::string(tag) == "s-saves")
            screen["props"] = saves_props;
        components["components"].push_back(screen);
    }
    std::ofstream(pack / "components.json") << components.dump();
    return pack;
}

static void Runtime(const fs::path &base, const fs::path &font)
{
    const fs::path pack = MakePack(base, font);
    std::unique_ptr<FileSource> files = DirectoryFiles(pack.string());
    NoBackend backend;
    std::vector<std::string> actions;
    double clock = 0.0;
    Config config = CheckConfig(*files, backend, clock);
    config.on_action = [&actions](const Action &action) {
        if (action.screen != "sound")
            actions.push_back(action.node + " " + action.verb);
    };
    std::string error;
    std::unique_ptr<Host> host = Host::Create(config, &error);
    Expect(host != nullptr, "the runtime starts: " + error);
    if (!host)
        return;
    host->SetScriptScreens(ScriptGame::Get().Screens(), &ScriptGame::Get().Port());
    const auto frames = [&](double seconds) {
        for (double t = 0.0; t < seconds; t += 1.0 / 60.0)
        {
            clock += 1.0 / 60.0;
            host->Frame();
        }
    };
    const auto tap = [&](float x, float y) {
        host->Touch(TouchPhase::Down, 1, x, y, clock);
        frames(1.0 / 60.0);
        host->Touch(TouchPhase::Up, 1, x, y, clock);
        frames(0.05);
    };
    const auto open = [&](const char *file) {
        actions.clear();
        host->ExpectInterface();
        host->SetInterface(Load(models, file));
        frames(0.1);
        actions.clear();
    };

    // The delete question over the saves list: a tap on the scrim right over a row of the list is the
    // question's «нет», and the row gets nothing.
    open("saves-confirm.json");
    Expect(host->WantsBack(), "the question owns the back key");
    tap(100, 30);
    Expect(actions.size() == 1 && actions[0] == "CONFIRM_NO press",
           "a tap beside the question is its «нет», nothing under the scrim: " + List(actions));
    // A view is the screen's whole state: the question the game closed leaves with it, in the same opening and when
    // the interface ends under the question and comes back (a save that failed brings the list back).
    const auto dialog = [] {
        return Rml::GetContext(0) && Rml::GetContext(0)->GetRootElement()->QuerySelector("#confirm") != nullptr;
    };
    InterfaceModel answered = Load(models, "saves-load.json");
    answered.revision = 3;
    host->SetInterface(answered);
    frames(0.1);
    Expect(!dialog() && !host->WantsBack(), "the question the game closed is gone from the list");
    open("saves-confirm.json");
    host->InterfaceLeft();
    frames(0.05);
    InterfaceModel back = Load(models, "saves-load.json");
    back.opening += 1;
    host->ExpectInterface();
    host->SetInterface(back);
    frames(0.1);
    Expect(!dialog() && !host->WantsBack(), "the list that comes back after its interface ended has no question");
    host->CloseInterface();
    frames(0.05);

    // The wages card must be answered: the tap over the row presses nothing, the card shakes for a
    // moment (class "refused" for refuseMs), and a second tap during the shake changes nothing.
    open("reading-salary.json");
    tap(100, 30);
    Rml::Element *card = Rml::GetContext(0) ? Rml::GetContext(0)->GetRootElement()->QuerySelector("#card") : nullptr;
    Expect(card != nullptr, "the card is on the screen");
    Expect(actions.empty(), "a tap beside the wages card reaches nothing: " + List(actions));
    Expect(card && card->IsClassSet("refused"), "the card shows the refusal (it shakes)");
    tap(100, 30);
    Expect(actions.empty() && card && card->IsClassSet("refused"), "a second tap during the shake: still nothing");
    frames(0.8);
    Expect(card && !card->IsClassSet("refused"), "the shake ends after refuseMs");
    tap(100, 30);
    Expect(actions.empty() && card && card->IsClassSet("refused"), "and starts again on the next tap");
    host->CloseInterface();
    frames(0.05);
    host.reset();
    fs::remove_all(pack);
}

int main(int argc, char **argv)
{
    if (argc < 5)
    {
        std::fprintf(stderr, "usage: storm_ui_modals_unit <tests/models> <tests/pack> <font.ttf> <application pack>\n");
        return 2;
    }
    models = argv[1];
    application_pack = argv[4];
    for (const Case &c : Cases())
        Walk(c);
    EveryModel();
    Undeclared();
    Runtime(argv[2], argv[3]);
    if (failures)
    {
        std::fprintf(stderr, "%d modal check(s) failed\n", failures);
        return 1;
    }
    std::printf("modals: every modal answers a tap beside it and the back key as its cancel, or refuses visibly; the "
                "scrim takes the tap\n");
    return 0;
}
