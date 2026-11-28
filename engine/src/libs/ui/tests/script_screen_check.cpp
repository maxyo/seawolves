// Host checks of script screens (src/screens/script_screen.h) behind the router: a fake VM side (ScriptPort) does what
// a screen's script does through the natives; the view's conversion to typed props by the spec's shapes.
#include "fakes.h"
#include "../src/screens/script_props.h"
#include "../src/screens/script_screen.h"
#include "storm_ui/script_screens.h"

#include <optional>
#include <stdexcept>

using namespace storm::ui;
using namespace storm::ui::screens;

namespace
{
std::unique_ptr<ComponentSpec> DemoSpec()
{
    return ParseComponentSpec(Json::parse(R"({"tag": "s-demo", "props": {
        "title": {"type": "text"}, "count": {"type": "int"}, "open": {"type": "bool"},
        "look": {"type": "enum", "options": ["a", "b"]},
        "items": {"type": "json", "shape": [{"id": "text", "on": "bool", "n": "number"}]},
        "deal": {"type": "json", "shape": {"$null": {"value": "int"}}},
        "note": {"type": "json", "shape": {"$null": "text"}},
        "cells": {"type": "json", "shape": [{"$either": ["text", {"text": "text", "bold": "bool"}]}]},
        "extras": {"type": "json", "shape": {}}}})"));
}

ViewTree Leaf(std::string name, std::string value)
{
    return {std::move(name), std::move(value), {}};
}

// What the script of the demo screen does: binds the tabs and a modal, shows a view; its events send widget actions.
struct DemoPort final : ScriptPort
{
    std::vector<std::string> events;
    std::vector<std::string> nodes; // the widgets of the last build: node and label
    ViewTree view{"view",
                  "",
                  {Leaf("title", "Отдых"),
                   Leaf("count", "3"),
                   Leaf("open", "1"),
                   {"items", "", {{"x", "", {Leaf("id", "a"), Leaf("ON", "0"), Leaf("n", "1.5")}}}}}};
    bool show_in_event = false;
    std::optional<ViewTree> predict; // what a tap on the tabs predicts
    bool hold = false;               // "go" waits for the game's answer
    bool awaiting = false;           // the builds say the game's answer needs another frame
    bool rebuild = false;            // the builds ask to be built again (a change of the game no model shows)
    std::vector<std::string> answers;
    std::optional<DealSpec> deal;
    DealView dealt;

    void Build(const ScriptScreen &, const std::vector<const Widget *> &widgets, ScriptCall &call) override
    {
        nodes.clear();
        for (const Widget *widget : widgets)
            nodes.push_back(widget->node + " " + widget->label);
        call.On("tabs", "select");
        call.On("go", "");
        call.Modal("dlg", "dlg-close");
        answers.push_back(call.Answer());
        if (awaiting)
            call.Await();
        if (rebuild)
            call.Rebuild();
        if (deal)
            dealt = call.Deal(*deal);
        call.Show(view);
    }
    void Event(const ScriptScreen &screen, const std::string &id, const std::string &verb, const std::string &value,
               ScriptCall &call) override
    {
        events.push_back(screen.capability + " " + id + " " + verb + " " + value);
        if (show_in_event)
            call.Show(view);
        if (id == "tabs" && predict)
            call.Predict(*predict);
        if (id == "tabs")
            call.Send("SLIDER", -1, "set", value, "", "");
        else if (id == "go" && call.Offers("OK", "press"))
        {
            call.Send("OK", -1, "press", "", "", "");
            if (hold)
                call.Hold();
        }
        else if (id == "dlg-close")
            call.Back("CANCEL");
        else if (id == "deal-offer")
            call.Send("SLIDER", -1, "set", value, "", "");
    }
};

InterfaceModel Model(const std::string &screen)
{
    InterfaceModel model;
    model.screen = screen;
    model.language = "russian";
    model.opening = 5;
    model.revision = 1;
    Widget slider;
    slider.node = "SLIDER";
    slider.type = "slider";
    slider.max = 100;
    slider.actions = {"set"};
    Widget ok;
    ok.node = "OK";
    ok.type = "button";
    Widget cancel = ok;
    cancel.node = "CANCEL";
    Widget line;
    line.node = "INFO";
    line.type = "text";
    line.index = 0;
    line.label = "first";
    Widget next = line;
    next.index = 1;
    next.label = "second";
    model.widgets = {slider, ok, cancel, line, next};
    return model;
}

const ScriptScreen kDemo{"demo",
                         "salary",
                         "s-demo",
                         {{"SLIDER", "slider", true}, {"OK", "button", true}, {"CANCEL", "button"}, {"INFO", "text"}}};

void Routing()
{
    const auto spec = DemoSpec();
    DemoPort port;
    FakeSurface surface;
    Router router(surface);
    router.SetScriptScreens({{kDemo.interface, [&] { return CreateScriptScreen(kDemo, *spec, port); }}});
    router.SetInterface(Model("salary"));
    ExpectEqual(surface.tag, "s-demo", "a script screen replaces the ported screen of its interface");
    Expect(port.nodes == std::vector<std::string>{"SLIDER ", "OK ", "CANCEL ", "INFO first", "INFO second"},
           "the script gets every admitted widget (a text node's lines are rows)");
    ExpectEqual(surface.props["items"].dump(), R"([{"id":"a","n":1.5,"on":false}])",
                "list items converted by the shape, fields named as the spec names them");
    Expect(surface.props["count"] == 3 && surface.props["open"] == true && surface.props["title"] == "Отдых",
           "scalar props by their types");
    Expect(surface.props["extras"]["rows"].empty(), "the router adds the extras as for every screen");

    router.OnEvent({"tabs", "select", "6"});
    ExpectEqual(port.events.back(), "demo tabs select 6", "a bound event goes to the script with its value");
    ExpectEqual(surface.Last(), "widget:salary\tSLIDER\t-1\tset@5:6", "the script's send is a widget action");
    router.OnEvent({"go", "click", ""});
    ExpectEqual(surface.Last(), "widget:salary\tOK\t-1\tpress", "an event bound with no verb takes every verb");
    Expect(!router.OnEvent({"unbound", "click", ""}), "an event the script did not bind is not handled");
    Expect(router.WantsBack(), "the modal the script declared takes the back key");
    router.Back();
    ExpectEqual(port.events.back(), "demo dlg-close  ", "the modal's cancel is the event the script named");
    ExpectEqual(surface.Last(), "widget:salary\tCANCEL\t-1\tpress", "and its Back presses the exit");

    router.SetScriptScreens({});
    Expect(surface.tag != "s-demo", "removing the script screens rebuilds the open interface without them");
}

// The game's answer to every widget action sent so far, and its next model.
void Answered(Router &router, FakeSurface &surface, InterfaceModel &model)
{
    for (; surface.answered < surface.actions.size(); ++surface.answered)
        router.Answer(surface.actions[surface.answered].request, true, "");
    ++model.revision;
    router.SetInterface(model);
}

void Prediction()
{
    const auto spec = DemoSpec();
    DemoPort port;
    FakeSurface surface;
    Router router(surface);
    router.SetScriptScreens({{kDemo.interface, [&] { return CreateScriptScreen(kDemo, *spec, port); }}});
    InterfaceModel model = Model("salary");
    router.SetInterface(model);
    port.predict = port.view;
    port.predict->children[1].value = "6";
    router.OnEvent({"tabs", "select", "6"});
    Expect(surface.props["count"] == 6 && router.Predicted() == 1, "the predicted view is drawn in the tap's build");
    Expect(surface.props["title"] == "Отдых" && surface.props["extras"].is_object(),
           "laid over the build: what the prediction does not change stays the build's");
    ++model.revision;
    router.SetInterface(model);
    Expect(surface.props["count"] == 6, "kept over the game's steps until it answers");
    port.view.children[1].value = "6";
    Answered(router, surface, model);
    Expect(surface.props["count"] == 6 && surface.errors.empty(), "the game's answer confirms it, quietly");

    port.predict->children[1].value = "7";
    router.OnEvent({"tabs", "select", "7"});
    Expect(surface.props["count"] == 7, "predicted again");
    Answered(router, surface, model);
    Expect(surface.props["count"] == 6 && surface.errors.size() == 1 &&
               surface.errors[0].find("UI prediction corrected: screen ") == 0 &&
               surface.errors[0].find("; count: predicted 7, the game has 6") != std::string::npos,
           "a different answer is shown and logged: " + (surface.errors.empty() ? std::string() : surface.errors[0]));

    surface.errors.clear();
    port.predict.reset();
    router.OnEvent({"tabs", "select", "8"});
    Expect(router.Predicted() == 2, "a tap the script does not predict is not predicted");
}

ViewTree Row(const std::string &id, bool on)
{
    return {"r" + id, "", {Leaf("id", id), Leaf("on", on ? "1" : "0"), Leaf("n", "1")}};
}

// The view's items as "<id><on>" in their order: "a0b1".
std::string Marks(const Json &items)
{
    std::string marks;
    for (const Json &item : items)
        marks += item["id"].get<std::string>() + (item["on"].get<bool>() ? "1" : "0");
    return marks;
}

// A prediction marks a row by its id, whatever the builds before the answer do to the list, and ends with the
// request it anticipates: shown (compared) or refused (at once), never by time.
void PredictionByIdentity()
{
    const auto spec = DemoSpec();
    DemoPort port;
    FakeSurface surface;
    Router router(surface);
    router.SetScriptScreens({{kDemo.interface, [&] { return CreateScriptScreen(kDemo, *spec, port); }}});
    InterfaceModel model = Model("salary");
    port.view.children[3].children = {Row("a", false), Row("b", false), Row("c", false)};
    router.SetInterface(model);
    port.predict = port.view;
    port.predict->children[3].children[1] = Row("b", true);
    router.OnEvent({"tabs", "select", "b"});
    ExpectEqual(Marks(surface.props["items"]), "a0b1c0", "the tapped row is marked in the tap's build");
    // the game's step before its answer: the row above is gone, the tapped one has moved up
    port.view.children[3].children = {Row("b", false), Row("c", false)};
    ++model.revision;
    router.SetInterface(model);
    surface.now += 60.0;
    router.Tick();
    ExpectEqual(Marks(surface.props["items"]), "b1c0", "the mark stays on the row tapped, not on its old place");
    port.view.children[3].children = {Row("b", true), Row("c", false)};
    Answered(router, surface, model);
    Expect(surface.errors.empty() && Marks(surface.props["items"]) == "b1c0", "the answer that shows it ends it");
    port.view.children[3].children = {Row("b", false), Row("c", false)};
    ++model.revision;
    router.SetInterface(model);
    ExpectEqual(Marks(surface.props["items"]), "b0c0", "and nothing of it is laid over the builds after");

    // the row tapped disappears before the answer
    port.predict = port.view;
    port.predict->children[3].children[1] = Row("c", true);
    router.OnEvent({"tabs", "select", "c"});
    port.view.children[3].children = {Row("b", false)};
    ++model.revision;
    router.SetInterface(model);
    ExpectEqual(Marks(surface.props["items"]), "b0", "a row gone is not marked, and nothing else is");
    const size_t logged = surface.logs.size();
    Answered(router, surface, model);
    Expect(surface.errors.empty() && surface.logs.size() == logged + 1 &&
               surface.logs.back().find("items[c].on was predicted, but its row is gone") != std::string::npos,
           "the end of its request says once that the row is gone");

    // refused: the game's state at once
    port.predict = port.view;
    port.predict->children[3].children[0] = Row("b", true);
    router.OnEvent({"tabs", "select", "b"});
    ExpectEqual(Marks(surface.props["items"]), "b1", "marked");
    for (; surface.answered < surface.actions.size(); ++surface.answered)
        router.Answer(surface.actions[surface.answered].request, false, "not now");
    Expect(Marks(surface.props["items"]) == "b0" && surface.errors.empty(), "a refusal ends it at once, quietly");
}

// The count the view shows, as the script builds it next.
void Count(DemoPort &port, int count)
{
    port.view.children[1].value = std::to_string(count);
}

void Waiting()
{
    const auto spec = DemoSpec();
    DemoPort port;
    port.hold = true;
    FakeSurface surface;
    Router router(surface);
    router.SetScriptScreens({{kDemo.interface, [&] { return CreateScriptScreen(kDemo, *spec, port); }}});
    InterfaceModel model = Model("salary");
    router.SetInterface(model);
    Count(port, 4);
    router.OnEvent({"go", "click", ""});
    router.Tick();
    Expect(surface.props["count"] == 4 && port.answers.back() == "waiting",
           "before the game answers, the view is drawn as built");
    port.awaiting = true;
    Count(port, 5);
    Answered(router, surface, model);
    Expect(surface.props["count"] == 4 && port.answers.back() == "applied",
           "an answer the game completes in a later frame is not drawn");
    port.awaiting = false;
    Count(port, 6);
    ++model.revision;
    router.SetInterface(model);
    Expect(surface.props["count"] == 6 && surface.errors.empty(), "the complete answer is drawn");
    router.SetInterface(model);
    Expect(port.answers.back().empty(), "and the wait is over");

    port.awaiting = true;
    Count(port, 7);
    router.OnEvent({"go", "click", ""});
    for (; surface.answered < surface.actions.size(); ++surface.answered)
        router.Answer(surface.actions[surface.answered].request, false, "not now");
    Expect(surface.props["count"] == 7 && port.answers.back() == "refused" && surface.errors.empty(),
           "a refusal is the answer: drawn at once");

    port.awaiting = false;
    Count(port, 8);
    router.OnEvent({"go", "click", ""});
    surface.now += 4.0; // the router's own bound on an engine that never answers is kAnswerSeconds
    router.Tick();
    Expect(port.answers.back() == "waiting" && surface.errors.empty(), "the hold has no time of its own");
    port.awaiting = true;
    Answered(router, surface, model);
    Count(port, 9);
    surface.now += 3.1;
    router.Tick();
    Expect(surface.props["count"] == 9 && port.answers.back() == "late" && surface.errors.size() == 1 &&
               surface.errors[0].find("did not complete its answer") != std::string::npos,
           "an awaited rest that does not come is drawn after the bound, and said: " + Json(surface.errors).dump());
}

// A build that awaits a change of the game no model shows (a script variable it sets in its next frame) asks to be
// built again: that build runs at the next tick, not at the awaited bound.
void AwaitedRebuild()
{
    const auto spec = DemoSpec();
    DemoPort port;
    port.hold = true;
    FakeSurface surface;
    Router router(surface);
    router.SetScriptScreens({{kDemo.interface, [&] { return CreateScriptScreen(kDemo, *spec, port); }}});
    InterfaceModel model = Model("salary");
    router.SetInterface(model);
    router.OnEvent({"go", "click", ""});
    port.awaiting = port.rebuild = true;
    Count(port, 5);
    Answered(router, surface, model);
    Expect(surface.props["count"] == 3, "the awaiting build is not drawn");
    port.awaiting = port.rebuild = false;
    surface.now += 0.016;
    router.Tick();
    Expect(surface.props["count"] == 5 && surface.errors.empty(),
           "the build asked for in it runs at the next tick: " + Json(surface.errors).dump());
}

void Deals()
{
    const auto spec = DemoSpec();
    DemoPort port;
    port.deal = DealSpec{.key = "r1",
                         .element = "goods",
                         .origin = 5,
                         .other = 10,
                         .low = 0,
                         .high = 15,
                         .answer = 5,
                         .cut_reason = "limit",
                         .more = "trade.buy",
                         .less = "trade.sell",
                         .reason = "",
                         .commit = "deal-commit",
                         .offer = "deal-offer"};
    FakeSurface surface;
    Router router(surface);
    router.SetScriptScreens({{kDemo.interface, [&] { return CreateScriptScreen(kDemo, *spec, port); }}});
    InterfaceModel model = Model("salary");
    router.SetInterface(model);
    Expect(port.dealt.value == 5 && port.dealt.change == 0 && port.dealt.button.children.empty(),
           "a deal opens at what the game's window holds, with no button");
    router.OnEvent({"goods", "change", "7"});
    Expect(port.events.back() == "demo deal-offer  2" && port.dealt.value == 7 && surface.commands.size() == 1,
           "a move offers its change to the game, and shows what was asked while the game answers");
    port.deal->answer = 7;
    Answered(router, surface, model);
    Expect(port.dealt.value == 7 && surface.toasts.empty(), "the game took all that was asked: nothing to say");
    router.OnEvent({"goods", "change", "0"});
    port.deal->answer = 3; // the game lets two be sold, not five
    Answered(router, surface, model);
    const auto label =
        std::ranges::find_if(port.dealt.button.children, [](const ViewTree &f) { return f.name == "label"; });
    Expect(port.dealt.value == 3 && port.dealt.change == -2 && surface.toasts == std::vector<std::string>{"limit"} &&
               label != port.dealt.button.children.end() && label->value == "<trade.sell> 2",
           "a sale past the game's limit shows the amount the game clamped it to, and says so");
    router.OnEvent({"primary", "click", ""});
    ExpectEqual(port.events.back(), "demo deal-commit  -2", "its button raises the commit with the game's change");
}

void Failures()
{
    const auto spec = DemoSpec();
    {
        DemoPort port;
        port.view.children.push_back(Leaf("nope", "1"));
        FakeSurface surface;
        Router router(surface);
        router.SetScriptScreens({{kDemo.interface, [&] { return CreateScriptScreen(kDemo, *spec, port); }}});
        router.SetInterface(Model("salary"));
        Expect(!surface.errors.empty() &&
                   surface.errors[0].find("view nope: is not a prop of s-demo") != std::string::npos,
               "a view the spec does not describe fails the screen, named");
        Expect(!surface.commands.empty() && surface.commands.back() == "back", "and the interface closes");
    }
    {
        DemoPort port;
        port.show_in_event = true;
        FakeSurface surface;
        Router router(surface);
        router.SetScriptScreens({{kDemo.interface, [&] { return CreateScriptScreen(kDemo, *spec, port); }}});
        router.SetInterface(Model("salary"));
        router.OnEvent({"tabs", "select", "6"});
        Expect(!surface.errors.empty() &&
                   surface.errors.back().find("only while the screen builds") != std::string::npos,
               "a view is shown only in a build");
    }
    const auto convert = [&](ViewTree view) {
        try
        {
            ScriptProps(view, *spec);
            return std::string();
        }
        catch (const std::runtime_error &error)
        {
            return std::string(error.what());
        }
    };
    ExpectEqual(convert({"v", "", {Leaf("open", "2")}}), "view open: '2' is not a bool (0 or 1)", "bool is 0 or 1");
    ExpectEqual(convert({"v", "", {Leaf("count", "3x")}}), "view count: '3x' is not a number", "int reads whole");
    bool unshaped = false;
    try
    {
        ParseComponentSpec(Json::parse(R"({"tag": "s-x", "props": {"free": {"type": "json"}}})"));
    }
    catch (const std::runtime_error &)
    {
        unshaped = true;
    }
    Expect(unshaped, "a json prop needs a shape");
    ExpectEqual(convert({"v", "", {{"items", "", {{"0", "", {Leaf("other", "")}}}}}}),
                "view items/0/other: is not a field of the spec's shape", "an item field outside the shape");
    ExpectEqual(convert({"v", "", {{"title", "", {Leaf("x", "")}}}}), "view title: has children, the spec says text",
                "a text prop has no children");
    const auto props = [&](ViewTree view) { return ScriptProps(view, *spec).dump(); };
    ExpectEqual(props({"v", "", {Leaf("deal", "")}}), R"({"deal":null})", "a nullable part given nothing is null");
    ExpectEqual(props({"v", "", {{"deal", "", {Leaf("value", "4")}}}}), R"({"deal":{"value":4}})",
                "a nullable part given fields is its shape");
    ExpectEqual(props({"v", "", {Leaf("note", "a")}}), R"({"note":"a"})", "a nullable text given a value is the text");
    ExpectEqual(props({"v", "", {{"cells", "", {Leaf("a", "12"), {"b", "", {Leaf("text", "5"), Leaf("bold", "1")}}}}}}),
                R"({"cells":["12",{"bold":true,"text":"5"}]})",
                "an either is the scalar without children, else the structure");
    ExpectEqual(convert({"v", "", {{"cells", "", {{"a", "", {Leaf("bold", "x")}}}}}}),
                "view cells/0/bold: 'x' is not a bool (0 or 1)", "the structure of an either is checked as such");
}
} // namespace

int main()
{
    Routing();
    Prediction();
    PredictionByIdentity();
    Waiting();
    AwaitedRebuild();
    Deals();
    Failures();
    if (failures)
        std::fprintf(stderr, "%d check(s) failed\n", failures);
    else
        std::printf("script screen checks passed\n");
    return failures ? 1 : 0;
}
