// SPDX-License-Identifier: GPL-3.0-only
// The crew hire's adapter (provides "adapter:crew"): men move between a ship of the fleet and a tavern, for a price a
// man when hired. `ui` is the screen's object its events hand over. The amount is the game's: the adapter offers it
// to the game's window and State says what the game made of it; nothing of it is computed here.
//
// State(ui, out), once a build:
//   title ("" none); ships.s<k>.{key, label, selected, can}: whose crew; info: the game's line of the ship
//   aboard, ashore: the men on the ship and in the tavern as the game has them now, with the amount set on them
//   base, baseAshore: the same before the amount (the deal's origin)
//   price (a man's, as written), cost (what the amount set costs, as the game writes it; 0 none)
//   edit (an amount can be set), confirm (it can be confirmed), reason (why not)
//   purse (our money as written, "" not shown); notes: the game's other lines of the ship ("" none)
//   ship, tavern: the crews' quality .{groups.g<k>.{label, value, word}, morale, moraleWord}
extern void TouchAdpCrew_State(aref ui, aref out);
extern void TouchAdpCrew_Offer(aref ui, int amount); // `amount` men hired (< 0 dismissed) offered to the game
extern void TouchAdpCrew_Commit(aref ui, int amount); // `amount` confirmed
extern void TouchAdpCrew_Ship(aref ui, int ship);     // ships s<k>
extern void TouchAdpCrew_Leave(aref ui);
