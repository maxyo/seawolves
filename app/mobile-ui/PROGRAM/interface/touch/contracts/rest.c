// SPDX-License-Identifier: GPL-3.0-only
// The rest screen's adapter (provides "adapter:rest"): what an hour is, the adapter knows. `ui` is the screen's object
// its events hand over. State(ui, out): out.hours (the whole hours the game's choice gives), out.canGo, out.reason (why not), out.note (the
// game's own text of its choice).
extern void TouchAdpRest_State(aref ui, aref out);
extern void TouchAdpRest_Choose(aref ui, int hours); // the game's choice that gives that many hours
extern void TouchAdpRest_Go(aref ui);
extern void TouchAdpRest_Close(aref ui);
