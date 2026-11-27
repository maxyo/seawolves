// Host checks of script screens (src/screens/script_screen.h) behind the router: a fake VM side (ScriptPort) does what
// a screen's script does through the natives; the view's conversion to typed props by the spec's shapes.
#include "fakes.h"
#include "../src/screens/script_props.h"
#include "../src/screens/script_screen.h"
#include "storm_ui/script_screens.h"

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
        "free": {"type": "json"},
        "extras": {"type": "json"}}})"));
}

ViewTree Leaf(std::string name, std::string value)
{
    return {std::move(name), std::move(value), {}};
}

// What the script of the demo screen does: binds the tabs and a modal, shows a view; its events send widget actions.
struct DemoPort final : ScriptPort
{
    std::vector<std::string> events;
    std::vector<std::string> nodes; // the widgets of the last build
    ViewTree view{"view",
                  "",
                  {Leaf("title", "Отдых"),
                   Leaf("count", "3"),
                   Leaf("open", "1"),
                   {"items", "", {{"x", "", {Leaf("id", "a"), Leaf("ON", "0"), Leaf("n", "1.5")}}}}}};
    bool show_in_event = false;

    void Build(const ScriptScreen &, const std::vector<const Widget *> &widgets, ScriptCall &call) override
    {
        nodes.clear();
        for (const Widget *widget : widgets)
            nodes.push_back(widget->node);
        call.On("tabs", "select");
        call.On("go", "");
        call.Modal("dlg", "dlg-close");
        call.Show(view);
    }
    void Event(const ScriptScreen &screen, const std::string &id, const std::string &verb, const std::string &value,
               ScriptCall &call) override
    {
        events.push_back(screen.capability + " " + id + " " + verb + " " + value);
        if (show_in_event)
            call.Show(view);
        if (id == "tabs")
            call.Send("SLIDER", "set", value);
        else if (id == "go" && call.Offers("OK", "press"))
            call.Send("OK", "press", "");
        else if (id == "dlg-close")
            call.Back("CANCEL");
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
    model.widgets = {slider, ok, cancel};
    return model;
}

const ScriptScreen kDemo{
    "demo", "salary", "s-demo", {{"SLIDER", "slider", true}, {"OK", "button", true}, {"CANCEL", "button"}}};

void Routing()
{
    const auto spec = DemoSpec();
    DemoPort port;
    FakeSurface surface;
    Router router(surface);
    router.SetScriptScreens({{kDemo.interface, [&] { return CreateScriptScreen(kDemo, *spec, port); }}});
    router.SetInterface(Model("salary"));
    ExpectEqual(surface.tag, "s-demo", "a script screen replaces the ported screen of its interface");
    Expect(port.nodes == std::vector<std::string>{"SLIDER", "OK", "CANCEL"}, "the script gets the admitted widgets");
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
    ExpectEqual(convert({"v", "", {Leaf("free", "x")}}), "view free: the spec of s-demo gives this json prop no shape",
                "a json prop needs a shape");
    ExpectEqual(convert({"v", "", {{"items", "", {{"0", "", {Leaf("other", "")}}}}}}),
                "view items/0/other: is not a field of the spec's shape", "an item field outside the shape");
    ExpectEqual(convert({"v", "", {{"title", "", {Leaf("x", "")}}}}), "view title: has children, the spec says text",
                "a text prop has no children");
}
} // namespace

int main()
{
    Routing();
    Failures();
    if (failures)
        std::fprintf(stderr, "%d check(s) failed\n", failures);
    else
        std::printf("script screen checks passed\n");
    return failures ? 1 : 0;
}
