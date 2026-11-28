#pragma once
#include "core.h"
#include "v_data.h"
#include "attributes.h"
#include <string>

namespace storm::android
{
inline ATTRIBUTES *InterfaceStates()
{
    auto *v = static_cast<VDATA *>(core.GetScriptVariable("InterfaceStates"));
    return v ? v->GetAClass() : nullptr;
}

// The new-game screen's "skip the tutorial" choice: InterfaceStates.SkipTutorial, which the game's new-game start
// reads and removes (script fix new-game-without-tutorial). A tap made for an earlier opening never gets here: the
// action's opening is checked where it is applied (mobile_action_frame.hpp).
struct NewGameChoice
{
    bool selected() const
    {
        auto *states = InterfaceStates();
        return states && states->FindAClass(states, "SkipTutorial");
    }
    void set(bool skip) const
    {
        auto *states = InterfaceStates();
        if (!states)
            return;
        if (skip)
            states->SetAttribute("SkipTutorial", "1");
        else if (auto *choice = states->GetAttributeClass("SkipTutorial"))
            states->DeleteAttributeClassX(choice);
    }
    // The toggle's value, "1" or "0"; false for anything else.
    bool apply(const std::string &value) const
    {
        if (value != "1" && value != "0")
            return false;
        set(value == "1");
        return true;
    }
};
inline NewGameChoice tutorialSkip;

// Offered for the hero whose start the script fix hooks, with the app's starter kit loaded for this game version.
inline bool TutorialSkipSupported()
{
    auto *attributes = InterfaceStates();
    auto *supported = attributes ? attributes->FindAClass(attributes, "AndroidTutorialSkipSupported") : nullptr;
    auto *hero = static_cast<VDATA *>(core.GetScriptVariable("startHeroType"));
    int32_t type = 0;
    return supported && supported->HasValue() && supported->GetValue() == "1" && hero && hero->Get(type) && type == 1;
}
} // namespace storm::android
