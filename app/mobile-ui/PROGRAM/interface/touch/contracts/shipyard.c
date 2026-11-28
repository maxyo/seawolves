// SPDX-License-Identifier: GPL-3.0-only
// The shipyard's adapter (provides "adapter:shipyard"): our ships beside the yard's, a ship bought, sold or traded in,
// repaired, its guns looked at and a captain given to a ship bought for a free place. `ui` is the screen's object its
// events hand over. Prices, what a deal costs, what can be bought or sold and how far a repair goes are the game's:
// the adapter asks the game's window and State says what it shows.
//
// State(ui, out), once a build:
//   title ("" none), money (our purse as the game writes it)
//   fleet.f<k>.{key, label, meta (its class), value (what the yard pays for it), image, free (a place with no ship),
//     selected (where a purchase goes), can}
//   offers.o<k>.{key, label, meta, value (its price), image, selected, can, reason}
//   card (absent: no ship to look at).{offer (an offer of the yard, else our chosen ship), title, rank (its class),
//     image, text, facts.f<k>.{key, label, value}, tradeIn (an offer: what the yard pays for our ship it takes in,
//     "" none)}
//   deals.d<k>.{key ("repair", "guns", "sell", "buy"), label, sub (its price, "" none), can, reason}: what can be done
//     with the card's ship
//   window (absent: none of the game's is open).{kind, title, text, ok.{label, can, reason}, cancel.{label, can}}:
//     "confirm" (the game asks before a deal, `text` its question with what it costs; deal: the deal it asks about,
//       "buy" or "sell", "" another question);
//     "repair" (parts.p<k>.{key, label, amount (as the order leaves it)}, price (the order's), all.{label, can,
//       reason} the game's whole repair);
//     "captain" (candidates.c<k>.{key, label, image, selected, can, reason}, name, facts.f<k>.{label, value},
//       skills.s<k>.{key, value, tone});
//     "guns" (plan (an image), bow, stern, port, starboard: the guns of each side)
extern void TouchAdpShipyard_State(aref ui, aref out);
extern void TouchAdpShipyard_Fleet(aref ui, int ship);             // fleet f<k>
extern void TouchAdpShipyard_Offer(aref ui, int offer);            // offers o<k>
extern void TouchAdpShipyard_Deal(aref ui, string key);            // deals by key
extern void TouchAdpShipyard_Repair(aref ui, int part, int amount); // window parts p<k> ordered to `amount`
extern void TouchAdpShipyard_RepairAll(aref ui);
extern void TouchAdpShipyard_Candidate(aref ui, int candidate);    // window candidates c<k>
extern void TouchAdpShipyard_Ok(aref ui);                          // the open window's ok
extern void TouchAdpShipyard_Close(aref ui);                       // the open window closed
extern void TouchAdpShipyard_Leave(aref ui);
