// What the host checks share: the expectations, recording fakes of screens::Surface and hud::Mount,
// and the model loader. No GL context, no game data.
#pragma once

#include "../src/hud/hud.h"
#include "../src/screens/screen.h"
#include "storm_ui/interface_model.h"

#include <cstdio>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

inline int failures = 0;

inline void Expect(bool ok, const std::string &what)
{
    if (!ok)
    {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++failures;
    }
}

inline void ExpectEqual(const std::string &actual, const std::string &expected, const std::string &what)
{
    if (actual != expected)
    {
        std::fprintf(stderr, "FAIL: %s\n  expected: %s\n  actual:   %s\n", what.c_str(), expected.c_str(),
                     actual.c_str());
        ++failures;
    }
}

inline storm::ui::InterfaceModel Load(const std::string &directory, const std::string &name)
{
    std::ifstream file(directory + "/" + name, std::ios::binary);
    const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    storm::ui::InterfaceModel model;
    std::string error;
    Expect(storm::ui::ParseInterfaceModel(json, model, &error), "parse " + name + ": " + error);
    return model;
}

// Records what a screen does. Host actions (no screen) are "host <node> <value>" in `commands`.
struct FakeSurface final : storm::ui::screens::Surface
{
    std::string tag;
    storm::ui::Json props;
    int hides = 0;
    std::vector<std::string> commands, logs, errors, toasts, alerts, refused;
    std::vector<storm::ui::Action> actions;
    uint64_t last_request = 0;              // of the last widget action
    size_t answered = 0;                    // actions[0, answered) have had the engine's answer
    mutable std::vector<std::string> shown; // "<tone> (<source>): <text>", as the host logs them
    std::string language;
    std::map<std::string, std::string> texts; // "<key>" for a key not in it
    storm::ui::TextRequest text;
    double now = 0.0;

    void Refuse(const std::string &id) override
    {
        refused.push_back(id);
    }
    void Show(const std::string &shown_tag, const storm::ui::Json &view) override
    {
        tag = shown_tag;
        props = view;
    }
    void Hide() override
    {
        tag.clear();
        ++hides;
    }
    void Send(const storm::ui::Action &action) override
    {
        commands.push_back(action.screen.empty() ? "host " + action.node + " " + action.value
                                                 : storm::ui::to_string(action));
        actions.push_back(action);
        last_request = action.request;
    }
    void RequestText(storm::ui::TextRequest request, const std::string &) override
    {
        text = std::move(request);
    }
    // An alert is a toast that is also counted on its own.
    void Alert(const std::string &line) override
    {
        alerts.push_back(line);
        Toast(line);
    }
    void Toast(const std::string &line) override
    {
        toasts.push_back(line);
    }
    void SetLanguage(const std::string &game_language) override
    {
        language = game_language;
    }
    std::string Text(const std::string &key) override
    {
        const auto found = texts.find(key);
        return found == texts.end() ? "<" + key + ">" : found->second;
    }
    void Shown(const std::string &source, const std::string &tone, const std::string &line) const override
    {
        shown.push_back(tone + " (" + source + "): " + line);
    }
    void Log(const std::string &line) override
    {
        logs.push_back(line);
    }
    void Error(const std::string &line) override
    {
        errors.push_back(line);
    }
    double Now() const override
    {
        return now;
    }
    std::string Last() const
    {
        return commands.empty() ? std::string() : commands.back();
    }
};

// A surface for a router that only carries the HUD's states.
struct QuietSurface final : storm::ui::screens::Surface
{
    std::vector<std::string> errors;
    void Refuse(const std::string &) override
    {
    }
    void Show(const std::string &, const storm::ui::Json &) override
    {
    }
    void Hide() override
    {
    }
    void Send(const storm::ui::Action &) override
    {
    }
    void RequestText(storm::ui::TextRequest, const std::string &) override
    {
    }
    void Alert(const std::string &) override
    {
    }
    void Toast(const std::string &) override
    {
    }
    void SetLanguage(const std::string &) override
    {
    }
    std::string Text(const std::string &key) override
    {
        return key;
    }
    void Shown(const std::string &, const std::string &, const std::string &) const override
    {
    }
    void Log(const std::string &line) override
    {
        logs.push_back(line);
    }
    void Error(const std::string &line) override
    {
        errors.push_back(line);
    }
    double Now() const override
    {
        return now;
    }
    std::vector<std::string> logs;
    double now = 0.0;
};

// The id the screen gave the drawn row k of its list `list` (a JSON pointer into the props, "/list/rows"): a row's
// tap carries it.
inline std::string Drawn(const FakeSurface &surface, const char *list, size_t k)
{
    return surface.props.at(storm::ui::Json::json_pointer(list)).at(k).at("id").get<std::string>();
}

// Field `name` of a note left for a screen (Router::Take), "" when it has none.
inline std::string NoteField(const std::optional<storm::ui::ViewTree> &note, const std::string &name)
{
    if (note)
        for (const storm::ui::ViewTree &field : note->children)
            if (field.name == name)
                return field.value;
    return "";
}

// Records what a HUD scene shows and sends.
struct FakeMount final : storm::ui::hud::Mount
{
    storm::ui::hud::View shown;
    bool visible = false;
    bool refuse = false;    // the pack cannot draw the view
    bool interface = false; // a game interface is open
    bool screen = false;    // a screen is mounted without an interface (the dialogue panel)
    bool mirrored = false;
    bool marked_texts = true; // Text gives "<key>"; false: the key itself
    int shows = 0;
    std::vector<std::string> commands, slots, logs, errors, alerts;
    std::string language;
    double now = 0.0;
    std::map<std::string, storm::ui::hud::Placement> boxes; // what is on the screen, by "<id> <selector>"
    // A problem of a shown view ("" = none), each one reported once.
    std::function<std::string(const storm::ui::hud::View &)> check;
    std::set<std::string> reported;

    bool ShowHud(const storm::ui::hud::View &view) override
    {
        ++shows;
        if (check)
        {
            const std::string problem = check(view);
            if (!problem.empty() && reported.insert(problem).second)
                Expect(false, problem);
        }
        if (refuse)
            return false;
        shown = view;
        visible = true;
        return true;
    }
    void HideHud() override
    {
        visible = false;
    }
    // The pack's components, for script scenes (hud_scripts.h); none without a pack.
    std::function<const storm::ui::ComponentSpec *(const std::string &)> components;
    const storm::ui::ComponentSpec *PackComponent(const std::string &tag) const override
    {
        return components ? components(tag) : nullptr;
    }
    // The layout elements, for script scenes (hud_scripts.h); none without a pack.
    std::function<const storm::ui::hud::LayoutElement *(const std::string &, const std::string &)> elements;
    const storm::ui::hud::LayoutElement *HudElement(const std::string &layout,
                                                    const std::string &element) const override
    {
        return elements ? elements(layout, element) : nullptr;
    }
    // The pack's times (tokens.json "time"), the ones the HUD reads.
    std::map<std::string, double> times{{"pickerFoldMs", 6000.0}, {"hintHoldMs", 500.0}, {"toastLeave", 0.22}};
    double HudSeconds(const std::string &token) const override
    {
        return times.at(token) / (token.ends_with("Ms") ? 1000.0 : 1.0);
    }
    void Send(const storm::ui::Action &action) override
    {
        commands.push_back(storm::ui::to_string(action));
        slots.push_back(action.node);
    }
    void SetLanguage(const std::string &game_language) override
    {
        language = game_language;
    }
    std::string Text(const std::string &key) override
    {
        return marked_texts ? "<" + key + ">" : key;
    }
    void Log(const std::string &line) override
    {
        logs.push_back(line);
    }
    void Error(const std::string &line) override
    {
        errors.push_back(line);
    }
    double Now() const override
    {
        return now;
    }
    bool InterfaceActive() const override
    {
        return interface;
    }
    bool Mirrored() const override
    {
        return mirrored;
    }
    bool ScreenMounted() const override
    {
        return interface || screen;
    }
    void Alert(const std::string &text) override
    {
        alerts.push_back(text);
    }
    bool Measure(const std::string &id, const std::string &selector, storm::ui::hud::Placement &out) const override
    {
        const auto found = boxes.find(id + " " + selector);
        if (found == boxes.end())
            return false;
        out = found->second;
        return true;
    }
    std::string Last() const
    {
        return commands.empty() ? std::string() : commands.back();
    }
    bool Has(const char *id) const
    {
        return shown.elements.contains(id);
    }
};
