// SPDX-License-Identifier: GPL-3.0-only
// The sail emblem's adapter (provides "adapter:sails"): an emblem painted on the ship's sails, or the one there taken
// off, for the shipwright's price. `ui` is the screen's object its events hand over.
//
// State(ui, out), once a build:
//   title ("" none); emblems.e<k>.{key, label, texture, selected (the chosen one), applied (on the sails now), can}
//   price (the work's, as the game asks it), purse (our money)
//   work.{remove (the work the game offers for the chosen emblem takes the one there off, else it paints it), can,
//     reason} (absent: the game offers none)
extern void TouchAdpSails_State(aref ui, aref out);
extern void TouchAdpSails_Choose(aref ui, int emblem); // emblems e<k>
extern void TouchAdpSails_Work(aref ui);               // the work the game offers
extern void TouchAdpSails_Leave(aref ui);
