// SPDX-License-Identifier: GPL-3.0-only
// The self repair's adapter (provides "adapter:repair"): the crew repairs the parts of the squadron's ships with the
// materials in their holds, over hours. `ui` is the screen's object its events hand over. How far a part goes, what
// it takes and how long it lasts are the game's: the adapter moves the game's order and State says what it holds.
//
// State(ui, out), once a build:
//   ships.s<k>.{key, label, selected, can}: whose ship
//   parts.p<k>.{key ("hull", "masts", "sails"), label (the game's), pieces (counted in pieces, else in per cent),
//     amount (the part as the order leaves it), state (as it is), full (the part whole), can (the order can move),
//     material ("planks", "sailcloth": what it takes)}
//   stock.{planks, sailcloth}: in the squadron's holds; takes.{planks, sailcloth}: what the order takes
//   hours (how long the order lasts, 0 nothing ordered), confirm (it can be done), reason
//   all.{label, can, reason} (absent: none): the order of every part of every ship
extern void TouchAdpRepair_State(aref ui, aref out);
extern void TouchAdpRepair_Set(aref ui, int part, int amount); // parts p<k> ordered to `amount`
extern void TouchAdpRepair_Ship(aref ui, int ship);            // ships s<k>
extern void TouchAdpRepair_All(aref ui);
extern void TouchAdpRepair_Do(aref ui);
extern void TouchAdpRepair_Leave(aref ui);
