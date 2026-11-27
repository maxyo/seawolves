// SPDX-License-Identifier: GPL-3.0-only
// The trade screens' adapter (provides "adapter:trade"): two sides hold amounts of the same goods, and a deal moves an
// amount of a row between them, for money where the window has prices. `ui` is the screen's object its events hand
// over; a row is a position k in out.rows of State (the game's order). Every amount, sum and limit is the game's: the
// adapter offers it to the game's window and State says what the game answered; nothing of it is computed here.
//
// State(ui, out), once a build:
//   title ("" none); headers.{own, other, name}: the game's column captions ("" where it names no side)
//   rows.r<k>.{key, name, image.{kind, group, image}, own, other (amounts), ownText, otherText (as the game writes
//     them), can (a deal can be opened), reason (why not), selected, pack (the pieces a price and weight are for),
//     weight (one pack's or piece's, as written; "" none), buy, sell (a pack's prices; absent: not traded so),
//     buyText, sellText (as written), about (what the game's further columns say of it, "" nothing),
//     type.{word, tone, profit} (absent: plain goods)}
//   deal (absent: none open).{row (k), edit (an amount can be set), confirm (it can be confirmed), reason, title, text,
//     amount (what the game's window holds now, as it clamped what was offered: > 0 taken, < 0 given), total (the
//     money it writes for it, 0 none), weight (as written, "" none), note (what else it says of it, "" nothing)}
//   money (deals have prices), weighs (deals have a weight), pieces (single pieces, else packs of goods), types (rows
//     may have a trade type)
//   sides.s<k>.{key, label, selected, can}: whose side (ships, characters); switch: a side can be chosen now
//   tabs.t<k>.{label, selected, can} (absent: the screen's own filters): the game's categories
//   figures.f<k>.{kind ("carry": what we carry, during a deal the game's figure after it; "place": the place's load;
//     "purse": our money), label ("" none), value (as written), currency (purse: the string key of what is paid, ""
//     money)}; storage, info: the game's texts of the place's note and its description
//   buttons.b<k>.{label, can, reason}: the place's own; takeAll.{label ("" ours), can, reason} (absent: none)
extern void TouchAdpTrade_State(aref ui, aref out);
// The moves; each takes as many of the game's frames as it needs while the screen waits (UiHold).
extern void TouchAdpTrade_Deal(aref ui, int row);                  // a deal opened on row k
extern void TouchAdpTrade_Offer(aref ui, int amount);              // `amount` (< 0 given) offered to the open deal
extern void TouchAdpTrade_Close(aref ui);                          // the open deal closed with nothing moved
extern void TouchAdpTrade_Commit(aref ui, int amount, bool stay);  // `amount` moved (< 0 given); stay: deal again
extern void TouchAdpTrade_Take(aref ui, int row);                  // all of their stack of row k taken
extern void TouchAdpTrade_Side(aref ui, int side);                 // another side (sides s<k>), the deal following
extern void TouchAdpTrade_Category(aref ui, int tab);              // tabs t<k>
extern void TouchAdpTrade_Press(aref ui, int button);              // buttons b<k>
extern void TouchAdpTrade_TakeAll(aref ui);
extern void TouchAdpTrade_Leave(aref ui);
