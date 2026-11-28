// SPDX-License-Identifier: GPL-3.0-only
// The save list's adapter (provides "adapter:saves"): the records the engine lists, what can be loaded, written over
// or deleted, and the game's questions about it. `ui` is the screen's object its events hand over.
//
// State(ui, out), once a build:
//   saving: the list writes a record (it offers a new one) (1), or loads one (0)
//   records.r<k> in the engine's order {key (the record's identity, its profile and file; "" the new record), file (its
//     file name), fresh (the new record),
//     autosave, unreadable (no description: the game cannot load it), hero, location, gameDate (YYYY-MM-DD),
//     gameTime (HH:MM), created (YYYY-MM-DD HH:MM), createdWhen (today, yesterday, year: this year, "" another),
//     minutes (play time, -1 unknown), preview (the record's picture, "" none), selected (the game's), can (it can be
//     loaded or written), canDelete, reason, enabled}
//   question (absent: none) {text, yes, no ("" none), canYes, canNo}: the game's own question
//   naming: the game's name editor waits for its OK (1); records are named by the game
extern void TouchAdpSaves_State(aref ui, aref out);
extern void TouchAdpSaves_Activate(aref ui, string key); // load, write or write over the record
extern void TouchAdpSaves_Delete(aref ui, string key);
extern void TouchAdpSaves_Answer(aref ui, bool yes); // the open question
extern void TouchAdpSaves_Name(aref ui);             // the name the game proposes, accepted
extern void TouchAdpSaves_Leave(aref ui);
