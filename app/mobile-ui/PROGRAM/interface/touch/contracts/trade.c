// SPDX-License-Identifier: GPL-3.0-only
// The trade screens' adapter (provides "adapter:trade"): what the game's goods or items window shows, and its moves.
// A row is a position in out.rows of State (the game's order); its key is the row identity the moves are guarded by.
//
// State(capability, out), read once a build:
//   out.title: the game's caption ("" without); out.headers.{own, other, name}: its column captions ("" without rows)
//   out.rows.r<k>.{key, name, image.{kind, group, image}, own, other (amounts), ownText, otherText (as the game
//     writes them), activate, select (offered), reason, selected, type ("export" | "import" | "demand" |
//     "contraband" | ""), pack (pieces a figure is for, "" none), weight (one pack's or piece's weight as the game
//     writes it, "" none) and weightUnit ("cwt" | "")}
//   out.selected: k of the selected row, -1 none; out.deal: the game's amount window is open on it, with
//     out.editable (it takes an amount), out.ok (it has an OK), out.confirm (which can be pressed),
//     out.confirmReason, out.dealTitle, out.dealText (the row's description); out.cancel: the window's cancel can be
//     pressed
//   out.money: deals have prices; out.weighs: deals have a weight (Weight); out.pieces: single pieces with
//     fractional weights (else goods in packs, whole cwt)
//   out.owners.o<k>.{key, label, selected, can}: whose side (ships, characters)
//   out.tabs.t<k>.{position (1-based), label, selected, can}: the game's own category tabs
//   out.carry, out.place, out.storage, out.info: the game's texts of what we carry, the place's load, the place's
//     note and its description ("" without)
//   out.buttons.b<k>.{label, can, reason}: the place's own buttons (bury, …); out.getAll.{label, can, reason}: the
//     game's take-all; out.takeAll.can: a take-all the engine runs where the game hides its own (each absent without)
extern void TouchAdpTrade_State(string capability, aref out);
// What `amount` of row k weighs, as the game counts it; 0 for a window whose deals have no weight.
extern float TouchAdpTrade_Weight(string capability, int row, int amount);
extern void TouchAdpTrade_Open(int row);        // the game's window of row k (a loop after its selection)
extern void TouchAdpTrade_Select(int row);
extern void TouchAdpTrade_Close();              // the open window closed with nothing moved
extern void TouchAdpTrade_Move(int amount);     // the open window's amount (< 0 gives) and its OK, in one flush
extern void TouchAdpTrade_Owner(string capability, int owner); // another side (out.owners o<k>)
extern void TouchAdpTrade_Tab(int tab);         // out.tabs t<k>
extern void TouchAdpTrade_Press(int button);    // out.buttons b<k>
extern void TouchAdpTrade_GetAll();
extern void TouchAdpTrade_TakeAll();
extern int TouchAdpTrade_TakingAll();           // the engine's take-all has not gone out yet
extern void TouchAdpTrade_Exit();
