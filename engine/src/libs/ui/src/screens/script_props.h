// A script's view as the props of a pack screen (screens/script_screen.h). Needs nothing of RmlUi: the VM's host
// checks use it too.
#pragma once

#include "../component_spec.h"
#include "storm_ui/script_screens.h"

#include <functional>
#include <optional>

namespace storm::ui::screens
{
// A HUD scene's reading where a value would be (a node with a `reading` field, hud/script_scene.cpp): given the node,
// where it lies in the props (`at`, also the path an error names) and the scalar type the spec gives it there, the
// value now as the reading's format makes it, or none to leave the field out (a prediction).
using Readings =
    std::function<std::optional<Json>(const ViewNode &node, const std::string &at, const std::string &type)>;

// Every value converted by its prop's type, a json prop by its "shape" in the spec (script_props.cpp Convert); enum
// values are checked where the props are set, as for every screen. A value the spec does not describe, or one that does
// not read as its type, throws naming its path. Without `readings` a reading is a field like any other.
Json ScriptProps(const ViewNode &view, const ComponentSpec &spec, const Readings &readings = {});
// A reading's value as a scalar of `type` ("text", "bool", "int", "number") takes it: a number in a text is its
// decimal figures, a word in a number is read as one; anything else throws naming `at`.
Json ReadingAs(const Json &value, const std::string &type, const std::string &at);
// A state channel's JSON as a script reads it (UiState): an array's items named by their place, true and false as 1
// and 0, null as an empty text.
ViewTree StateTree(const std::string &name, const Json &json);
} // namespace storm::ui::screens
