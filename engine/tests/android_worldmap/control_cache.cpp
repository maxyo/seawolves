// The state the game reads for a control (mergeTouchControl, PCS_CONTROLS::GetControlState): the physical state as it
// is while no HUD control of that name is held; a held HUD control's state and its analog part (the helm's deflection)
// otherwise.
#include "android_control_state.hpp"

#include <cassert>
#include <iostream>

int main()
{
    for (const CONTROL_STATE physical :
         {CONTROL_STATE{CST_ACTIVATED, 1, 1}, CONTROL_STATE{CST_ACTIVE, 0.75f, 173}, CONTROL_STATE{CST_INACTIVE, 0, 0}})
    {
        auto state = physical;
        storm::android::mergeTouchControl("Ship_TurnLeft", state);
        assert(state.state == physical.state && state.fValue == physical.fValue && state.lValue == physical.lValue);
    }
    auto &held = storm::android::heldControls;
    held.guard = [](std::string_view) { return true; };
    held.update(5);
    const auto epoch = storm::android::combatCancelEpoch.load();
    const auto read = [] {
        CONTROL_STATE state{CST_INACTIVE, 0, 0};
        storm::android::mergeTouchControl("Ship_TurnLeft", state);
        return state;
    };
    assert(held.offer(storm::ui::hud::Hold{5, "Ship_TurnLeft", true, 40}, epoch)); // the helm pulled 40%
    held.advance(epoch);
    auto state = read();
    assert(state.state == CST_ACTIVATED && state.lValue == 1 && state.fValue == 0.4f);
    assert(held.offer(storm::ui::hud::Hold{5, "Ship_TurnLeft", true, 0}, epoch)); // a plain hold: all the way
    held.advance(epoch);
    state = read();
    assert(state.state == CST_ACTIVE && state.lValue == 1 && state.fValue == 1.0f);
    assert(held.offer(storm::ui::hud::Hold{5, "Ship_TurnLeft", false, 0}, epoch));
    held.advance(epoch);
    state = read();
    assert(state.state == CST_INACTIVATED && state.lValue == 0 && state.fValue == 0.0f);
    std::cout << "PASS control state: physical as it is, a held HUD control with its analog part\n";
}
