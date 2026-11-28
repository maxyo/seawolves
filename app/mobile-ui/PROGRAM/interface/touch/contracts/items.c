// SPDX-License-Identifier: GPL-3.0-only
// The items screen's adapter (provides "adapter:items"): the things a person of the hero's company carries, what they
// wear in each slot and what the game's action does with one of them. `ui` is the screen's object its events hand
// over; a row is a position k in out.rows (the game's order), a slot a position in out.slots, a person a position in
// out.people; a screen finds each again by its id, which a change of the lists keeps. Whether an item can be put on,
// taken off or used, and what that does, is the game's.
//
// State(ui, out), once a build:
//   title; load (the weight carried as the game writes it, "" none)
//   people.p<k>.{key, label, meta (the slots' captions), image (picture, "" none)}; shown (k whose things the game
//     shows); waiting (the game is still to show the person picked, k picked: this build is a step)
//   tabs.t<n>.{id, label, selected, can} (none: the game has no categories)
//   slots.s<k>.{id, label, icon (ours of the slot), group (its items' group), place (of a group worn in two places,
//     which: "pistol", "musket"; "" the group's only), image (absent: empty), row (k of the row of what it holds, -1
//     none in the list), days ("" none: the days of force left)}
//   rows.r<k>.{id (the game's item), label, image (picture), count, weight (as the game writes it), selected, group
//     ("" none), place (where the game wears it of its group's places, "" any), worn, quest}
//   detail (absent: none selected).{row, title, image, slot (the caption of its slot, "" none), worn, kind, weight,
//     text, term ("" none: the days of force), left ("" none: of them left), figures.f<n>.{key (string key), value
//     (number), digits, better (1 more is better, -1 less, 0 neither), against (absent: nothing worn to set it
//     against)}, action.{label, can, reason, warn (it uses the item up)}}
//   map (absent: closed).{title, text, image}
extern void TouchAdpItems_State(aref ui, aref out);
extern void TouchAdpItems_Select(aref ui, int row);       // row k chosen: the game describes it
extern void TouchAdpItems_Act(aref ui);                   // the game's action on the chosen row
extern void TouchAdpItems_Tab(aref ui, int tab);          // the game's category n of out.tabs
extern void TouchAdpItems_Person(aref ui, int person);    // another person's things
extern void TouchAdpItems_CloseMap(aref ui);
extern void TouchAdpItems_Section(aref ui, string section); // another hero section (hero, ship, journal, …)
extern void TouchAdpItems_Leave(aref ui);
