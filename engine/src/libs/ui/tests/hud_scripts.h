// The HUD scenes of the touch interface's scripts for a HUD check: the fixture game's program with the application's
// scripts attached (tools/script_game.h) and the components of the application's built pack (the check's argument),
// given to a Bench's HUD as a host gives them.
#pragma once

#include "hud_bench.h"
#include "../src/component_spec.h"
#include "../tools/script_game.h"

#include <filesystem>

class ScriptPack
{
  public:
    explicit ScriptPack(const std::filesystem::path &pack)
    {
        const auto read = [&](const char *name) {
            std::ifstream file(pack / name, std::ios::binary);
            return Json::parse(file, nullptr, false);
        };
        const Json components = read("components.json");
        for (const Json &entry : components.at("components"))
            specs_.push_back(ParseComponentSpec(entry));
        std::string error;
        Expect(ParseLayout(read("hud-layout.json"), layout_, &error), "the pack's HUD layout is read: " + error);
    }
    const ComponentSpec *Find(const std::string &tag) const
    {
        for (const auto &spec : specs_)
            if (spec->tag == tag)
                return spec.get();
        return nullptr;
    }
    // The bench's HUD draws its script scenes with this pack.
    void Attach(Bench &bench) const
    {
        bench.mount.components = [this](const std::string &tag) { return Find(tag); };
        bench.mount.elements = [this](const std::string &scene, const std::string &element) {
            return FindLayoutElement(layout_, scene, element);
        };
        std::vector<ScriptScreen> scenes;
        for (const ScriptScreen &screen : ScriptGame::Get().Screens())
            if (screen.interface == kHudInterface)
                scenes.push_back(screen);
        Expect(!scenes.empty(), "the scripts give HUD scenes");
        bench.hud.SetScriptScenes(std::move(scenes), &ScriptGame::Get().Port());
    }

  private:
    std::vector<std::unique_ptr<ComponentSpec>> specs_;
    Layout layout_;
};

// The whole runtime draws the scripts' screens and HUD scenes, as a phone attaches them.
inline void AttachScripts(Runtime &run)
{
    run.host->SetScriptScreens(ScriptGame::Get().Screens(), &ScriptGame::Get().Port());
}
