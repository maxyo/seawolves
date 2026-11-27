// SPDX-License-Identifier: GPL-3.0-only
// The nations screen's adapter (provides "adapter:nations"): how the nations stand with each other and with the hero,
// and the flag the hero's ship flies. `ui` is the screen's object its events hand over; a nation is a position k in
// out.nations (the game's order). Which flags may be flown and when is the game's: the adapter asks its window.
//
// State(ui, out), once a build:
//   title ("" none)
//   nations.n<k>.{key, label, icon (our flag icon of it, "" none: its image), image.{kind, group, image},
//     relations.r<j>.{self (j is k), kind ("ally", "neutral", "war"; "" another: its image), label, image},
//     flown (the flag the ship would raise now), flag (it can be asked for now)}
//   standingTitle; standing.s<n>.{nation (k), word (the game's), score (absent: none; the game's -100..100),
//     tone ("" neutral, "hostile", "good")}
//   flags (a flag can be chosen now); apply (absent: none).{label, can, reason}: raising the chosen flag
//   help.{standing, flag}: the game has a help text of that part
extern void TouchAdpNations_State(aref ui, aref out);
extern void TouchAdpNations_Pick(aref ui, int nation);              // the flag of nation k chosen
extern void TouchAdpNations_Raise(aref ui);                         // the chosen flag raised
extern void TouchAdpNations_Help(aref ui, string part, aref out);   // part "standing" or "flag": out.{title, text}
extern void TouchAdpNations_Section(aref ui, string section);       // another hero section (hero, ship, journal, …)
extern void TouchAdpNations_Leave(aref ui);
