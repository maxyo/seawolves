// SPDX-License-Identifier: GPL-3.0-only
// The new game's adapter (provides "adapter:new_game"): the game's options for a new game, its hero and «start».
// `ui` is the screen's object its events hand over.
//
// State(ui, out), once a build:
//   difficulty, setup (absent: the game shows none) {text, left, right (the game's ‹ › can be pressed)}; setup also
//     note (the occupation's text) and occupation (the game's key of it, "" none)
//   battle (absent: none) {label, selected (k), can, options.o<k>.label}: the sea battle type, one of several
//   toggles.t<k> {key, label, on, can, reason}: the game's switches in its order
//   hero {name, nation (our flag icon of the hero's nation, "" none), bio, bioTitle, portrait (absent: none)}
//   start {can, reason}
//   profile: the game asks whether to keep the existing profile folder (1); the port has no profiles
//   window (absent: none) {text, buttons.b<k>.{label, can}}: another window of the game over the screen
extern void TouchAdpNewGame_State(aref ui, aref out);
extern void TouchAdpNewGame_Step(aref ui, string id, bool right); // id: difficulty, setup
extern void TouchAdpNewGame_Battle(aref ui, int k);
extern void TouchAdpNewGame_Toggle(aref ui, string key, bool on);
extern void TouchAdpNewGame_Start(aref ui);
extern void TouchAdpNewGame_Cancel(aref ui);
extern void TouchAdpNewGame_Profile(aref ui); // keeps the folder: the game starts
extern void TouchAdpNewGame_Window(aref ui, int k);
