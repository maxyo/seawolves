// What the HUD scene checks share: the synthetic states, a bench for a scene alone (Bench) and the whole
// runtime with a renderer that draws nothing and the small synthetic pack of tests/pack (Runtime).
#pragma once

#include "fakes.h"
#include "storm_ui/render_backend.h"

#include <RmlUi/Core/RenderInterface.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <set>

using namespace storm::ui;
using namespace storm::ui::hud;

inline std::string states_directory;

inline std::string Text(const std::string &name)
{
    std::ifstream file(states_directory + "/" + name + ".json", std::ios::binary);
    Expect(static_cast<bool>(file), "state file " + name + ".json exists");
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

inline Json State(const std::string &name)
{
    return Json::parse(Text(name), nullptr, false);
}

// The elements each layout scene may show (src/hud/layout-elements.json, the contract the pack
// build checks the pack's hud-layout.json against): a scene showing anything else fails here,
// not as the error toast of a device.
inline const Layout &DeclaredLayout()
{
    static const Layout layout = [] {
        Layout out;
        std::ifstream file(states_directory + "/../../src/hud/layout-elements.json", std::ios::binary);
        const Json declared = Json::parse(file, nullptr, false);
        Expect(declared.is_object() && declared.contains("scenes"), "src/hud/layout-elements.json is read");
        if (declared.is_object() && declared.contains("scenes"))
            for (const auto &scene : declared["scenes"].items())
                for (const Json &id : scene.value())
                {
                    LayoutElement element;
                    element.id = id.get<std::string>();
                    out[scene.key()].push_back(std::move(element));
                }
        return out;
    }();
    return layout;
}

// --- the scene alone --------------------------------------------------------------------

struct Bench
{
    FakeMount mount;
    Hud hud{mount};
    QuietSurface surface;
    screens::Router router{surface};

    Bench()
    {
        mount.marked_texts = false;
        mount.check = [](const View &view) {
            const std::string problem = CheckView(DeclaredLayout(), view);
            return problem.empty() ? problem : problem + " (src/hud/layout-elements.json)";
        };
        router.AttachHud(&hud);
        Feed("presentation", "presentation-game");
    }
    void Feed(const std::string &channel, const std::string &file)
    {
        router.SetState(channel, State(file));
        hud.Tick();
    }
    void Set(const std::string &channel, const Json &state)
    {
        router.SetState(channel, state);
        hud.Tick();
    }
    void Explore()
    {
        Feed("gameplay", "gameplay-explore");
        Feed("combat", "combat-explore");
        Feed("vitals", "vitals");
        Feed("worldInfo", "world-info");
        Feed("landCommands", "land-commands-idle");
    }
    void Melee()
    {
        Feed("gameplay", "gameplay-melee");
        Feed("combat", "combat-melee");
        Feed("vitals", "vitals");
        Feed("landCommands", "land-commands-idle");
    }
    // A frame: timed work, then the drawn-frame notice.
    void Frame(double seconds = 0.0)
    {
        mount.now += seconds;
        surface.now = mount.now;
        hud.Tick();
        hud.Drawn();
    }
    void Press(const std::string &id, const std::string &verb = "click", const std::string &value = {})
    {
        hud.OnEvent({id, verb, value});
        hud.Tick();
    }
    // The engine's answer to the HUD's last request (Hud::Request): applied and shown by `revision` of its
    // channel, or refused.
    void Answer(bool applied, uint64_t revision = 0)
    {
        router.Answer(router.Requests().Next() - 1, applied, applied ? "" : "refused in the check", revision);
        hud.Tick();
    }
};

// --- the whole runtime ------------------------------------------------------------------

class NoRenderer final : public Rml::RenderInterface
{
  public:
    // Distinct handles, as a real renderer gives (the runtime identifies meshes by handle).
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override
    {
        return ++next_;
    }
    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override
    {
    }
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override
    {
    }
    Rml::TextureHandle LoadTexture(Rml::Vector2i &, const Rml::String &) override
    {
        return 0;
    }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override
    {
        return ++next_;
    }
    void ReleaseTexture(Rml::TextureHandle) override
    {
    }
    Rml::TextureHandle SaveLayerAsTexture() override
    {
        return ++next_;
    }
    void EnableScissorRegion(bool) override
    {
    }
    void SetScissorRegion(Rml::Rectanglei) override
    {
    }

  private:
    uintptr_t next_ = 0;
};

class NoBackend final : public RenderBackend
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
        held_ = hold > 0;
        if (held_)
            --hold;
    }
    bool FrameHeld() const override
    {
        return held_;
    }
    PictureState AcquireGamePicture(const std::string &) override
    {
        return PictureState::Unavailable; // no game files in the checks
    }
    void Upload(Rml::TextureHandle) override
    {
        // No GPU copies in the checks.
    }
    int hold = 0; // the next frames a backend would hold back (their layer textures still being made)

  private:
    bool held_ = false;
    NoRenderer renderer_;
};

struct Runtime
{
    std::unique_ptr<FileSource> files;
    NoBackend backend;
    std::unique_ptr<Host> host;
    std::vector<std::string> commands;
    double clock = 0.0;

    explicit Runtime(const std::string &pack)
    {
        files = DirectoryFiles(pack);
        Config config;
        config.strict_templates = true; // a template path that names nothing is an error
        config.files = files.get();
        config.backend = &backend;
        config.width = 800; // density 1: pixels are dp
        config.height = 400;
        config.clock = [this] { return clock; };
        config.verify_unchanged =
            true; // Frame reports a frame Prepare wrongly called unchanged (FrameStats::mispredicted)
        config.on_action = [this](const Action &action) { commands.push_back(to_string(action)); };
        std::string error;
        host = Host::Create(config, &error);
        Expect(host != nullptr, "the runtime starts with the test pack: " + error);
    }
    void Feed(const std::string &channel, const std::string &file)
    {
        Expect(host->SetState(channel, Text(file)), "state " + file + " is accepted");
    }
    void Frame(double seconds = 1.0 / 60.0)
    {
        clock += seconds;
        host->Frame();
        Expect(!host->LastFrameStats().mispredicted,
               "a frame Prepare called unchanged draws the same as the frame shown");
    }
    void Explore()
    {
        Feed("presentation", "presentation-game");
        Feed("gameplay", "gameplay-explore");
        Feed("combat", "combat-explore");
        Feed("vitals", "vitals");
        Feed("worldInfo", "world-info");
        Feed("contextAction", "context-door");
        Feed("landCommands", "land-commands-idle");
        Frame();
        Frame();
    }
    bool Sent(const std::string &command) const
    {
        for (const std::string &sent : commands)
            if (sent == command)
                return true;
        return false;
    }
    std::string Last() const
    {
        return commands.empty() ? std::string() : commands.back();
    }
};
