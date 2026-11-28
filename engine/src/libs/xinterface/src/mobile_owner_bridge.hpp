#pragma once
#include "mobile_owner_selection.hpp"
#include "core.h"
#include "v_data.h"
#include "platform/android_ui.hpp"
#include <set>

namespace storm::mobile
{
inline const char *OwnerProfile()
{
    auto *profile = static_cast<VDATA *>(core.GetScriptVariable("PlayerProfile"));
    return profile && profile->GetAClass() ? profile->GetAClass()->GetAttribute("name") : nullptr;
}
inline std::string OwnerLabel(uint32_t index, bool ship)
{
    auto *characters = static_cast<VDATA *>(core.GetScriptVariable("Characters"));
    if (!characters || index >= characters->GetElementsNum())
        return {};
    auto *person = characters->GetAClass(index);
    if (!person)
        return {};
    if (ship)
    {
        auto *vessel = person->GetAttributeClass("ship");
        const char *name = vessel ? vessel->GetAttribute("name") : nullptr;
        if (name && *name)
            return name;
    }
    std::string result;
    for (const char *field : {"name", "lastname"})
        if (const char *value = person->GetAttribute(field); value && *value)
        {
            if (!result.empty())
                result += " ";
            result += value;
        }
    return result;
}
// A game owner list that exports no rows because of its `current`, said once per window and node.
inline void SayOwnerCurrentDefect(std::string_view node, bool vertical, ATTRIBUTES *attrs, size_t capacity)
{
    static std::set<std::string> said;
    const std::string screen = storm::android::mobileScreen;
    const std::string defect = OwnerCurrentDefect(screen, node, vertical, attrs, capacity);
    if (defect.empty() || !said.insert(screen + "\n" + std::string(node)).second)
        return;
    core.Trace("ERROR interface %s: the owner list %s exports no rows, %s: the screen shows no selector",
               screen.c_str(), std::string(node).c_str(), defect.c_str());
}
} // namespace storm::mobile
