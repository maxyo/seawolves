// The touch interface's scripts for the UI's host checks: the application's mobile-ui layer over the fixture game
// (engine tests/ui_scripts/game) on the engine's script VM, attached as on a phone. The checks give its script
// screens to their hosts and routers, so a screen written in script is checked like a C++ one.
#pragma once

#include "../src/component_spec.h"
#include "../src/screens/screen.h"
#include "storm_ui/script_screens.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace storm::ui
{
class ScriptGame
{
  public:
    // The process's program (the VM is one per process), started at the first call; a core segment that does not
    // attach throws.
    static ScriptGame &Get();
    ~ScriptGame();

    const std::vector<ScriptScreen> &Screens() const;
    ScriptPort &Port();
    // A check's game state: a string global of the fixture game, or the attribute at `path` ("Global.attribute.…") of
    // a global object.
    void Set(const std::string &path, const std::string &value);
    // The script screens for a router without a host, with the components of the pack at `pack`.
    screens::Router::ScriptScreens Entries(const std::filesystem::path &pack);

  private:
    ScriptGame();
    struct Program;
    std::unique_ptr<Program> program_;
    std::vector<std::unique_ptr<ComponentSpec>> specs_; // of Entries, alive with the routers' screens
};
} // namespace storm::ui
