// What the capture tool and the host checks do to a host besides what a platform does (ui.h): not part of the
// product interface.
#pragma once

#include "storm_ui/ui.h"

#include <string>

namespace storm::ui::tools
{

// Replaces the props of the component with this element id (a JSON object); false when there is none or the JSON
// is not an object.
bool SetProps(Host &host, const std::string &element_id, const std::string &json);
// Scrolls the element with this id to the top of its scroll container.
void ScrollTo(Host &host, const std::string &element_id);

} // namespace storm::ui::tools
