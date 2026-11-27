// The VM side of script screens (storm_ui/script_screens.h): the admitted widgets go to the screen's object
// UiScreens.<capability>, the script runs in the events UiBuild(ui) and UiTap(ui, id, verb, value) with that object,
// and answers through the
// Ui* natives of the library "UiScripts" (script_port.cpp). See ../../README.md.
#pragma once

#include "script_segments.hpp"

namespace storm::ui_scripts
{
class VmPort final : public storm::ui::ScriptPort
{
  public:
    explicit VmPort(const Attacher &attacher) : attacher_(attacher)
    {
    }
    void Build(const storm::ui::ScriptScreen &screen, const std::vector<const storm::ui::Widget *> &widgets,
               storm::ui::ScriptCall &call) override;
    void Event(const storm::ui::ScriptScreen &screen, const std::string &id, const std::string &verb,
               const std::string &value, storm::ui::ScriptCall &call) override;

  private:
    // The program to raise the event in; throws when there is none or a script is running (an event raised from
    // inside a script would run the interface's script in the middle of the game's).
    storm::script::Program &Between(const storm::ui::ScriptScreen &screen, const std::string &what) const;
    const Attacher &attacher_;
};
} // namespace storm::ui_scripts
