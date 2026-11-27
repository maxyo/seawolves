// SPDX-License-Identifier: GPL-3.0-only
// The trade screens' adapter (provides "adapter:trade"): two sides hold amounts of the same goods, and a deal moves an
// amount of a row between them, for money where the window has prices. `ui` is the screen's object its events hand
// over; a row is a position k in out.rows of State (the game's order).
//
// State(ui, out), once a build:
//   title ("" none); headers.{own, other, name}: the game's column captions ("" where it names no side)
//   rows.r<k>.{key, name, image.{kind, group, image}, own, other (amounts), ownText, otherText (as the game writes
//     them), can (a deal can be opened), reason (why not), selected, pack (the pieces a price and weight are for),
//     weight (one pack's or piece's, as written; "" none), buy, sell (a pack's prices; absent: not traded so),
//     buyText, sellText (as written),
//     type.{word, tone, profit} (absent: plain goods)}
//   deal (absent: none open).{row (k), edit (an amount can be set), confirm (it can be confirmed), reason, title, text}
//   money (deals have prices), weighs (deals have a weight), pieces (single pieces, else packs of goods)
//   sides.s<k>.{key, label, selected, can}: whose side (ships, characters); switch: a side can be chosen now
//   tabs.t<k>.{label, selected, can} (absent: the screen's own filters): the game's categories
//   carry, place, storage, info: the game's texts of what we carry, the place's load, its note and its description
//   load, capacity (-1: not told): what we carry of what we can; purse (the figure as written, "" none), balance (-1:
//     not told), currency (the string key of what is paid, "" money)
//   buttons.b<k>.{label, can, reason}: the place's own; takeAll.{label ("" ours), can, reason} (absent: none)
extern void TouchAdpTrade_State(aref ui, aref out);
// What moving `amount` of row k (> 0: taken, < 0: given) is, as the game counts it: out.total (the money it costs or
// brings, 0 without prices), out.weight (what it weighs), out.note (what else the game says of it, a fee; "" none).
extern void TouchAdpTrade_Terms(aref ui, int row, int amount, aref out);
// How far a deal on row k can go: out.take, out.give (pieces), out.takeWhy, out.giveWhy (the string key of what stops
// it short of the other side's amount, "" nothing).
extern void TouchAdpTrade_Reach(aref ui, int row, aref out);
// The moves; each takes as many of the game's frames as it needs while the screen waits (UiHold).
extern void TouchAdpTrade_Deal(aref ui, int row);                  // a deal opened on row k
extern void TouchAdpTrade_Close(aref ui);                          // the open deal closed with nothing moved
extern void TouchAdpTrade_Commit(aref ui, int amount, bool stay);  // `amount` moved (< 0 given); stay: deal again
extern void TouchAdpTrade_Take(aref ui, int row);                  // all of their stack of row k taken
extern void TouchAdpTrade_Side(aref ui, int side);                 // another side (sides s<k>), the deal following
extern void TouchAdpTrade_Category(aref ui, int tab);              // tabs t<k>
extern void TouchAdpTrade_Press(aref ui, int button);              // buttons b<k>
extern void TouchAdpTrade_TakeAll(aref ui);
extern void TouchAdpTrade_Leave(aref ui);
