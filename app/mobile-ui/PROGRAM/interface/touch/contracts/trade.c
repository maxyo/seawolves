// SPDX-License-Identifier: GPL-3.0-only
// The trade screens' adapter (provides "adapter:trade"): what the game's goods or items window shows, and its moves.
// A row is a position in out.rows of State (the game's order); its key is the row identity the moves are guarded by.
//
// `ui` is the screen's object its events hand over. State(ui, out), read once a build:
//   out.title: the game's caption ("" without); out.headers.{own, other, name}: its column captions ("" without rows)
//   out.rows.r<k>.{key, name, image.{kind, group, image}, own, other (amounts), ownText, otherText (as the game
//     writes them), activate, select (offered), reason, selected, type ("export" | "import" | "demand" |
//     "contraband" | ""), buyText, sellText (a pack's prices as the game writes them, "-" not traded so, "" none),
//     buy, sell (those prices, 0 for "-"), pack (pieces a figure is for, "" none), weight (one pack's or piece's
//     weight as the game writes it, "" none) and weightUnit ("cwt" | "")}
//   out.selected: k of the selected row, -1 none; out.deal: the game's amount window is open on it, with
//     out.editable (it takes an amount), out.ok (it has an OK), out.confirm (which can be pressed),
//     out.confirmReason, out.dealTitle, out.dealText (the row's description); out.cancel: the window's cancel can be
//     pressed
//   out.money: deals have prices (Total, Reach); out.weighs: deals have a weight (Weight); out.pieces: single pieces with
//     fractional weights (else goods in packs, whole cwt)
//   out.owners.o<k>.{key, label, selected, can}: whose side (ships, characters)
//   out.tabs.t<k>.{position (1-based), label, selected, can}: the game's own category tabs
//   out.carry, out.place, out.storage, out.info: the game's texts of what we carry, the place's load, the place's
//     note and its description ("" without)
//   out.load, out.capacity: what we carry of what we can (capacity -1: not told); out.purse: our money's figure as the
//     game writes it ("" without), out.balance its amount (-1: not told), out.doubloons: the merchant takes doubloons
//   out.buttons.b<k>.{label, can, reason}: the place's own buttons (bury, …); out.getAll.{label, can, reason}: the
//     game's take-all; out.takeAll.can: a take-all the engine runs where the game hides its own (each absent without)
extern void TouchAdpTrade_State(aref ui, aref out);
// What `amount` of row k weighs, as the game counts it; 0 for a window whose deals have no weight.
extern float TouchAdpTrade_Weight(aref ui, int row, int amount);
// What a move of `amount` pieces of row k costs (> 0: bought) or brings (< 0: sold), as the game charges it.
extern int TouchAdpTrade_Total(aref ui, int row, int amount);
// How many of `stock` pieces of row k can be bought: out.most within the purse and the hold, out.room within the hold.
extern void TouchAdpTrade_Reach(aref ui, int row, int stock, aref out);
extern void TouchAdpTrade_Open(aref ui, int row);        // the game's window of row k (a loop after its selection)
extern void TouchAdpTrade_Select(aref ui, int row);
extern void TouchAdpTrade_Close(aref ui);              // the open window closed with nothing moved
extern void TouchAdpTrade_Move(aref ui, int amount);     // the open window's amount (< 0 gives) and its OK, in one flush
extern void TouchAdpTrade_Owner(aref ui, int owner); // another side (out.owners o<k>)
extern void TouchAdpTrade_Tab(aref ui, int tab);         // out.tabs t<k>
extern void TouchAdpTrade_Press(aref ui, int button);    // out.buttons b<k>
extern void TouchAdpTrade_GetAll(aref ui);
extern void TouchAdpTrade_TakeAll(aref ui);
extern int TouchAdpTrade_TakingAll(aref ui);           // the engine's take-all has not gone out yet
extern void TouchAdpTrade_Exit(aref ui);
