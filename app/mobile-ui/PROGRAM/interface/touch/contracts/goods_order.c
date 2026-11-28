// SPDX-License-Identifier: GPL-3.0-only
// The purchase list's adapter (provides "adapter:goods_order"): the standing orders of goods a ship of the squadron is
// to have bought for it in port, one amount per good. `ui` is the screen's object its events hand over; a row is a
// position k in out.rows of State (the game's order). The amounts are the game's: an order is what the game keeps for
// the good, and setting one goes through the game's own window.
//
// State(ui, out), once a build:
//   rows.r<k>.{key (the good's identity), name, image.{kind, group, image}, order (the standing order), hold (what the
//     ship carries of it, as written; "" the game keeps no record), step (the pieces one press of the game's own
//     arrows adds or takes), can (an order can be set now), reason}
//   window (the row k whose amount window the game has open, -1 none)
//   ships.s<k>.{label}: the ships of the squadron the list can be for; ship (k shown); switch (another can be chosen)
//   contraband (absent: none).{label, on, can, reason}: whether contraband is bought too, for this ship
extern void TouchAdpGoodsOrder_State(aref ui, aref out);
// The moves; each is answered in the tap's frame (UiHold).
extern void TouchAdpGoodsOrder_Order(aref ui, int row, int amount); // the standing order of row k set to `amount`
extern void TouchAdpGoodsOrder_Ship(aref ui, int ship);             // the list of ship k
extern void TouchAdpGoodsOrder_Contraband(aref ui, bool on);
extern void TouchAdpGoodsOrder_Close(aref ui); // the game's open amount window closed with nothing set
extern void TouchAdpGoodsOrder_Leave(aref ui);
