// SPDX-License-Identifier: GPL-3.0-only
// The trade book's adapter (provides "adapter:tradebook"): the places whose prices the hero has seen and the price
// list of one of them, as the game recorded it. `ui` is the screen's object its events hand over.
//
// State(ui, out), once a build:
//   title ("" none); sections (the hero sections open from here)
//   places.p<k>.{label, sub (where it lies, "" none), flag (our flag icon, "" none), date (of its prices, "" the game
//     has none), hour ("" none), shown (its prices are the list), can}
//   columns.{sell, buy, stock}: the game's headings of the price list
//   goods.g<k>.{label, image (absent: none).{kind, group, image, uv}, tag ("export", "import", "contraband",
//     "demand"; "" another), sell, buy, stock (as the game writes them)}
extern void TouchAdpTradebook_State(aref ui, aref out);
extern void TouchAdpTradebook_Choose(aref ui, int place);         // places p<k>: its prices shown
extern void TouchAdpTradebook_Section(aref ui, string section);   // another hero section (hero, ship, journal, …)
extern void TouchAdpTradebook_Leave(aref ui);
