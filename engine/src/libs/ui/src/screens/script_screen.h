// A screen whose logic is a script (storm_ui/script_screens.h): admission and bindings as for every screen, the view
// and the answers to its events from the script through ScriptPort.
#pragma once

#include "screen.h"
#include "storm_ui/script_screens.h"

#include "../component_spec.h"

namespace storm::ui::screens
{
std::unique_ptr<Screen> CreateScriptScreen(const ScriptScreen &screen, const ComponentSpec &component,
                                           ScriptPort &port);
} // namespace storm::ui::screens
