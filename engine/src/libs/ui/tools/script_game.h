// The touch interface's scripts for the UI's host checks: the application's mobile-ui layer over the fixture game
// (engine tests/ui_scripts/game) on the engine's script VM, attached as on a phone. The checks give its script
// screens to their hosts and routers, so a screen written in script is checked like a C++ one.
#pragma once

#include "../src/component_spec.h"
#include "../src/screens/screen.h"
#include "storm_ui/script_screens.h"

#include <filesystem>
#include <map>
#include <memory>
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
    // The fixture game's state for one case: each member of `game` names a global object of the fixture and gives
    // its attributes (a JSON object tree, leaves as text). Objects an earlier call set and `game` does not name get
    // back what the fixture's Main gave them.
    void Play(const Json &game);
    // The script screens for a router without a host, with the components of the pack at `pack`.
    screens::Router::ScriptScreens Entries(const std::filesystem::path &pack);

  private:
    ScriptGame();
    struct Program;
    std::unique_ptr<Program> program_;
    std::vector<std::unique_ptr<ComponentSpec>> specs_; // of Entries, alive with the routers' screens
    std::map<std::string, Json> main_state_;            // the objects Play set, as Main left them
};
} // namespace storm::ui
