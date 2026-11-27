// SPDX-License-Identifier: GPL-3.0-only
// The menus' adapter (provides "adapter:menu"): the title, the pause menu over a game in progress, the plate of a
// quick save being written and the death screen. `ui` is the screen's object its events hand over.
//
// Title(ui, out): last (the newest record «Продолжить» loads, as the engine names it; "" there is none).
//
// Pause(ui, out), once a build:
//   game (the game in progress as the engine names it, "" unknown); year, month, day (its date, 0 unknown)
//   resume {label, can, reason}: the game's «continue»; can: the game resumes on the menu's cancel
//   buttons.<id> {label, can, reason} for id save, load, options, exit, updates (absent: the game has none)
//   home: the exit is the game's own way to its main menu (1), or its way out of the program (0), after which the
//     application starts again at the main menu
//   quickLoad: the record of a quick save exists (a quick save is offered where buttons.save can)
//   confirm (absent: no question open) {question, no (the game's caption of its "no", "" none), canNo}
extern void TouchAdpMenu_Title(aref ui, aref out);
extern void TouchAdpMenu_Pause(aref ui, aref out);
extern void TouchAdpMenu_Press(aref ui, string id); // a button of Pause by its id
extern void TouchAdpMenu_Resume(aref ui);
extern void TouchAdpMenu_Answer(aref ui, bool yes); // the open question
//
// Saving(ui, out): title (the game's caption, "" none), text (its line).
extern void TouchAdpMenu_Saving(aref ui, aref out);
//
// Over(ui, out): kind (sea, land, defeat: how the game ended). End(ui): the game's own end of its death picture.
extern void TouchAdpMenu_Over(aref ui, aref out);
extern void TouchAdpMenu_End(aref ui);
