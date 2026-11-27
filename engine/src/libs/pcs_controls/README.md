# pcs_controls

The keyboard and mouse controls service (`CONTROLS`, engine.ini `controls = pcs_controls`): the scripts create named
controls, bind keys to them (`MapControl`) and read their state every frame (`GetControlState`).

## Groups, hold controls and schemes (`src/control_bindings.hpp`)

Caribbean Legend's scripts bind keys per control group and say which group is current, every frame
(`MapControl(control, key, group)`, `UpdateControlGroup`). A binding without a group (`MapControl(control, key)`, the
legacy games) holds in every group; the control answers to its binding in the current group, else to that one.

- Hold controls: a control marked as hold in a group (`SetControlHold`) shares its key with a tap control, the one
  paired with it (`SetHoldControls(tap, hold)`) or else the one other control on that key (a key shared more widely
  stays an ordinary key). A release before the threshold (`SetHoldThreshold`, milliseconds, 300 until the scripts set
  it) fires the tap control; holding to the threshold fires the hold control while the key is still down. Either
  reads as activated for one frame, inactivated the next, inactive otherwise; never active. A key already down when
  the group or the bindings change acts only after its release. `ResetHoldControls` rebuilds the pairing.
- Schemes: a control may belong to a combat scheme (`SetControlScheme`, 0 every scheme); one scheme is current
  (`SetClassicCombatControls`: 1 classic, 2 the new attacks). A control of another scheme has no key, and a key held
  through a switch moves none of the schemes' controls until it is released.

The natives are core built-ins (`core/src/natives_ext_list.h`) over the `CONTROLS` interface; the legacy games call
none of them and their controls behave as before.
