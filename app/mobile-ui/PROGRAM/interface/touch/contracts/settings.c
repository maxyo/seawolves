// SPDX-License-Identifier: GPL-3.0-only
// The settings' adapter (provides "adapter:settings"): the game's own options as its window offers them, grouped by
// what they are about; «done» keeps what the player changed, the way back drops it. The game applies, checks and
// stores them. `ui` is the screen's object its events hand over.
//
// State(ui, out), once a build:
//   groups.<group>.r<k> {key (the option's identity), kind (toggle | segment | slider), label, on (toggle), value
//     (slider: 0..100 of its range), options.o<k> (segment: the names of its choices), selected (segment: k), can,
//     reason}; group: sea (sailing and the map), marks (the marks and hints over the scene), camera, sound; a group
//     the game has none of is absent
//   done {can, reason}
extern void TouchAdpSettings_State(aref ui, aref out);
extern void TouchAdpSettings_Toggle(aref ui, string key, bool on);
extern void TouchAdpSettings_Choose(aref ui, string key, int k);
extern void TouchAdpSettings_Slide(aref ui, string key, int percent); // the position let go, 0..100 of the range
extern void TouchAdpSettings_Keep(aref ui);
extern void TouchAdpSettings_Leave(aref ui);
