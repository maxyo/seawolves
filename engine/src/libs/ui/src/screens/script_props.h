// A script's view as the props of a pack screen (screens/script_screen.h). Needs nothing of RmlUi: the VM's host
// checks use it too.
#pragma once

#include "../component_spec.h"
#include "storm_ui/script_screens.h"

namespace storm::ui::screens
{
// Every value converted by its prop's type, a json prop by its "shape" in the spec (script_props.cpp Convert); enum
// values are checked where the props are set, as for every screen. A value the spec does not describe, or one that does
// not read as its type, throws naming its path.
Json ScriptProps(const ViewTree &view, const ComponentSpec &spec);
} // namespace storm::ui::screens
