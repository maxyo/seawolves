// What the host checks share: the expectations, recording fakes of screens::Surface and hud::Mount, the model and
// state loaders, a renderer that draws nothing and copies of the check packs. No GL context, no game data.
#pragma once

#include "../src/hud/hud.h"
#include "../src/screens/screen.h"
#include "../tools/script_game.h"
#include "storm_ui/interface_model.h"
#include "storm_ui/render_backend.h"
#include "storm_ui/ui.h"

#include <RmlUi/Core/RenderInterface.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <unistd.h>
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

// The check's result line and exit code: `what` names the check.
inline int Finish(const char *what)
{
    if (failures)
        std::fprintf(stderr, "%d %s check(s) failed\n", failures, what);
    else
        std::printf("%s checks passed\n", what);
    return failures ? 1 : 0;
}

inline std::string Read(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

inline void Write(const std::filesystem::path &path, const std::string &text)
{
    std::ofstream(path, std::ios::binary) << text;
}

inline storm::ui::InterfaceModel Load(const std::string &directory, const std::string &name)
{
    storm::ui::InterfaceModel model;
    std::string error;
    Expect(storm::ui::ParseInterfaceModel(Read(directory + "/" + name), model, &error), "parse " + name + ": " + error);
    return model;
}

// The synthetic HUD states (tests/states), a check's argument.
inline std::string states_directory;

inline std::string Text(const std::string &name)
{
    std::ifstream file(states_directory + "/" + name + ".json", std::ios::binary);
    Expect(static_cast<bool>(file), "state file " + name + ".json exists");
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

inline storm::ui::Json State(const std::string &name)
{
    const storm::ui::Json parsed = storm::ui::Json::parse(Text(name), nullptr, false);
    Expect(!parsed.is_discarded(), "state file " + name + ".json is JSON");
    return parsed;
}

inline storm::ui::Widget Make(const char *node, const char *type, const char *label = "",
                              std::vector<std::string> actions = {})
{
    storm::ui::Widget widget;
    widget.node = node;
    widget.type = type;
    widget.label = label;
    widget.actions = std::move(actions);
    return widget;
}

inline void Without(storm::ui::InterfaceModel &model, const char *node)
{
    std::erase_if(model.widgets, [node](const storm::ui::Widget &w) { return w.node == node; });
}

// The application's built pack (a check's argument): its script screens with its components on a router.
inline std::filesystem::path application_pack;

inline storm::ui::screens::Router &Scripted(storm::ui::screens::Router &router)
{
    router.SetScriptScreens(storm::ui::ScriptGame::Get().Entries(application_pack));
    return router;
}

// Lines that are `line` exactly / that contain `part`.
inline size_t Count(const std::vector<std::string> &lines, const std::string &line)
{
    return std::count(lines.begin(), lines.end(), line);
}
inline bool Sent(const std::vector<std::string> &lines, const std::string &line)
{
    return Count(lines, line) > 0;
}
inline size_t CountHaving(const std::vector<std::string> &lines, const std::string &part)
{
    return std::count_if(lines.begin(), lines.end(),
                         [&part](const std::string &line) { return line.find(part) != std::string::npos; });
}
inline bool Has(const std::vector<std::string> &lines, const std::string &part)
{
    return CountHaving(lines, part) > 0;
}

inline std::string List(const std::vector<std::string> &lines)
{
    std::string out;
    for (const std::string &line : lines)
        out += (out.empty() ? "" : " | ") + line;
    return "[" + out + "]";
}

// A copy of the check pack `base` in a temporary directory to add to, with `font` as its only font when given (text
// is measured).
inline std::filesystem::path CopyPack(const std::filesystem::path &base, const std::string &name,
                                      const std::filesystem::path &font = {})
{
    namespace fs = std::filesystem;
    const fs::path pack = fs::temp_directory_path() / ("storm-ui-" + name + "-" + std::to_string(::getpid()));
    fs::remove_all(pack);
    fs::copy(base, pack, fs::copy_options::recursive);
    if (!font.empty())
    {
        fs::copy_file(font, pack / "font.ttf");
        storm::ui::Json manifest = storm::ui::Json::parse(Read(pack / "pack.json"));
        manifest["fonts"] = storm::ui::Json::array({"font.ttf"});
        Write(pack / "pack.json", manifest.dump());
    }
    return pack;
}

// The runtime's start options of a check: the pack's files, the backend and the check's clock, 800x400 at density 1
// (pixels are dp).
inline storm::ui::Config CheckConfig(storm::ui::FileSource &files, storm::ui::RenderBackend &backend,
                                     const double &clock)
{
    storm::ui::Config config;
    config.files = &files;
    config.backend = &backend;
    config.width = 800;
    config.height = 400;
    config.clock = [&clock] { return clock; };
    return config;
}

// A renderer that draws nothing. Distinct handles, as a real renderer gives (the runtime identifies meshes by handle);
// it counts the meshes in the runtime's danger red (the crossed "missing icon" marker) and the textures drawn.
class NoRenderer final : public Rml::RenderInterface
{
  public:
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int>) override
    {
        if (std::any_of(vertices.begin(), vertices.end(), [](const Rml::Vertex &vertex) {
                return vertex.colour == Rml::ColourbPremultiplied(0xc4, 0x47, 0x3a, 0xff);
            }))
            ++marker_meshes;
        return ++next_;
    }
    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle texture) override
    {
        if (texture)
            drawn_textures.insert(texture);
    }
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override
    {
    }
    // Every pack image "loads" as a pixel: what is drawn does not matter, only that it exists and which it is.
    Rml::TextureHandle LoadTexture(Rml::Vector2i &dimensions, const Rml::String &source) override
    {
        dimensions = {1, 1};
        loaded.push_back(source);
        return ++next_;
    }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override
    {
        ++generated;
        return ++next_;
    }
    void ReleaseTexture(Rml::TextureHandle) override
    {
    }
    void EnableScissorRegion(bool) override
    {
    }
    void SetScissorRegion(Rml::Rectanglei) override
    {
    }
    // Layers, filters and shaders as every backend has them (the runtime's filtered-layer cache needs a saved
    // layer; the application's pack has gradients and blur): handles that are never drawn.
    Rml::LayerHandle PushLayer() override
    {
        return ++layers_;
    }
    void PopLayer() override
    {
        --layers_;
    }
    Rml::TextureHandle SaveLayerAsTexture() override
    {
        return ++next_;
    }
    Rml::CompiledFilterHandle CompileFilter(const Rml::String &, const Rml::Dictionary &) override
    {
        return ++next_;
    }
    Rml::CompiledShaderHandle CompileShader(const Rml::String &, const Rml::Dictionary &) override
    {
        return ++next_;
    }

    int marker_meshes = 0;
    int generated = 0;               // textures made from pixels (fonts, layers)
    std::vector<std::string> loaded; // texture sources loaded
    std::set<Rml::TextureHandle> drawn_textures;

  private:
    uintptr_t next_ = 0;
    Rml::LayerHandle layers_ = 0;
};

class NoBackend final : public storm::ui::RenderBackend
{
  public:
    Rml::RenderInterface &Interface() override
    {
        return renderer_;
    }
    void SetViewport(int, int) override
    {
    }
    void BeginFrame(bool) override
    {
    }
    void EndFrame() override
    {
    }
    PictureState AcquireGamePicture(const std::string &) override
    {
        return PictureState::Unavailable; // no game files in the checks
    }
    void Upload(Rml::TextureHandle) override
    {
        // No GPU copies in the checks.
    }
    std::string RefusedTexture(int width, int height, storm::ui::PackFormat) const override
    {
        return std::max(width, height) > max_texture
                   ? "larger than the device's texture limit of " + std::to_string(max_texture) + " px"
                   : std::string();
    }
    NoRenderer &Renderer()
    {
        return renderer_;
    }

    int max_texture = 2048; // the device limit the checks fake

  private:
    NoRenderer renderer_;
};

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
    double Seconds(const std::string &token) const override
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
