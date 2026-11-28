// SPDX-License-Identifier: GPL-3.0-only
// The new game's adapter (provides "adapter:new_game"): the game's options for a new game, its hero and its starts.
// `ui` is the screen's object its events hand over.
//
// State(ui, out), once a build:
//   rows.r<k>: the game's options in its order, each {kind, key, label}:
//     kind "step": text, left, right (the game's ‹ › can be pressed), note (the choice's text, "" none), hero (1: a
//       choice of the hero's, shown beside him), occupation (absent: not the occupation; else the game's key of it)
//     kind "choice": one of several: selected (k), can, options.o<k>.label (a sea battle type, a page of the window)
//   toggles.t<k> {key, label, on, can, reason}: the game's switches in its order
//   hero {name, nation (our flag icon of the hero's nation, "" none), bio, bioTitle, portrait (absent: none)}
//   starts.s<k> {key, label ("": the screen's own word), can, reason}: the ways to start, the first the main one
//   profile: the game asks whether to keep the existing profile folder (1); the port has no profiles
//   window (absent: none) {text, buttons.b<k>.{label, can}, name (absent: none) {label, value}}: another window of
//     the game over the screen; name is its field the player names something in
extern void TouchAdpNewGame_State(aref ui, aref out);
extern void TouchAdpNewGame_Step(aref ui, string key, bool right);
extern void TouchAdpNewGame_Choose(aref ui, string key, int k);
extern void TouchAdpNewGame_Toggle(aref ui, string key, bool on);
extern void TouchAdpNewGame_Start(aref ui, string key);
extern void TouchAdpNewGame_Cancel(aref ui);
extern void TouchAdpNewGame_Profile(aref ui); // keeps the folder: the game starts
extern void TouchAdpNewGame_Window(aref ui, int k);
extern void TouchAdpNewGame_Name(aref ui, string title); // the phone's editor over the window's name field
