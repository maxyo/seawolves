// SPDX-License-Identifier: GPL-3.0-only
// The title's adapter (provides "adapter:title"): the commands of the game's main menu. `ui` is the screen's object
// its events hand over.
//
// State(ui, out): the commands as the game offers them now:
//   commands.<id> {can} for id continue, new, load, options, credits, exit (absent: the game has none now)
//   last: the record continue loads, as the engine names it ("" unnamed)
//   noRecord: continue is off because there is no record to continue
//   confirm (absent: no question open) {question, no (the game's caption of its "no", "" none), canNo}
extern void TouchAdpTitle_State(aref ui, aref out);
extern void TouchAdpTitle_Command(aref ui, string id); // a command of State that can
extern void TouchAdpTitle_Answer(aref ui, bool yes); // the open question
