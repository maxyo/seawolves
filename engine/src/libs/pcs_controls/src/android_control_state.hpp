#pragma once
#ifdef __ANDROID__
#include "controls.h"

#include <platform/android_combat_input.hpp>
#include <platform/android_held_controls.hpp>

namespace storm::android
{
// A control's state as the game reads it: the physical state merged with the touch HUD's held and tapped controls of
// the same name. A held HUD control carries its analog part (the helm's deflection); a key is all the way.
inline void mergeTouchControl(const char *name, CONTROL_STATE &state)
{
    static_assert(CST_INACTIVE == 0 && CST_ACTIVE == 1 && CST_INACTIVATED == 2 && CST_ACTIVATED == 3);
    const int physical = state.state;
    const int combined = heldControls.merge(name, combatInput.merge(name, physical));
    if (combined == physical)
        return;
    state.state = static_cast<CONTROL_STATE_TYPE>(combined);
    state.lValue = (combined == CST_ACTIVE || combined == CST_ACTIVATED) ? 1 : 0;
    const float strength = heldControls.strength(name);
    state.fValue = state.lValue && strength >= 0.0f ? strength : static_cast<float>(state.lValue);
}
} // namespace storm::android
#endif
