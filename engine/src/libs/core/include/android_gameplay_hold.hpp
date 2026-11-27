#pragma once
#include "attributes.h"
#include "core.h"
#include "v_data.h"

#include <string_view>

namespace storm::android
{
// What holds play for the scripts in any scene: a dialogue, an open interface, a cutscene (the hero is an actor), a
// scene change (the fader) or a game video. "" when nothing does, else the reason in GameplaySnapshot::blocked's
// words. The touch controls of every mode wait while one holds.
inline std::string_view gameplayHold(ATTRIBUTES *hero)
{
    int32_t dialogue = 0;
    if (auto *value = static_cast<VDATA *>(core.GetScriptVariable("dialogRun"));
        value && value->Get(dialogue) && dialogue)
        return "dialogue";
    auto *interfaces = static_cast<VDATA *>(core.GetScriptVariable("InterfaceStates"));
    auto *states = interfaces ? interfaces->GetAClass() : nullptr;
    if (states && states->GetAttributeAsDword("Launched", 0))
        return "interface";
    auto *type = hero ? hero->FindAClass(hero, "chr_ai.type") : nullptr;
    if (type && type->HasValue() && type->GetValue() == "actor")
        return "cutscene";
    if (core.GetEntityId("fader") != invalid_entity)
        return "transition";
    if (core.GetEntityId("CAviPlayer") != invalid_entity || core.GetEntityId("WMVideoPlay") != invalid_entity)
        return "video";
    return {};
}
} // namespace storm::android
