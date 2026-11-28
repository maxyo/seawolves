// SPDX-License-Identifier: GPL-3.0-only
// The ship's adapter (provides "adapter:ship"), a hero section: a ship of the squadron, its figures, its crew and the
// hold, and what the game does with them (renamed, its guns set, the crew's morale raised or its shares paid, goods
// thrown overboard, the exchange with the hero's ship). `ui` is the screen's object its events hand over. What an
// action costs, whether it can be done and how far it goes are the game's: the adapter asks the game's windows and
// State says what they show.
//
// State(ui, out), once a build:
//   title; sections (the hero sections open from here)
//   ships.s<k>.{key, label, selected, can}: the squadron's ships, the one shown selected
//   ship (absent: its captain has none).{name, type, rank (its class, "" none), captain, companion (not the hero's
//     own ship), image (absent: none), rename (the game takes a new name), exchange (the game offers the exchange
//     with the hero's ship)}
//   figures.f<k>.{key ("hull", "sails", "speed", "turn", "upwind", "capacity"), label, now, most (-1: none written)}
//   crew.{count, least, full}; quality.{groups.g<k>.{label, value, word}, morale, moraleWord} (TouchLegLib_Crew)
//   guns.{label, text (calibre and count as the game writes them), caliber (the biggest the ship takes), can (opens
//     the guns' window)}
//   facts.f<k>.{icon ("food", "money"), title, meta}: the game's lines on food and upkeep
//   morale, shares (absent: not offered).{label, can, reason}: the game's buttons
//   heads.{name, count, weight, pack, packWeight}; goods.g<k>.{key, label, image (absent: none), count, weight, pack,
//     packWeight, can}
//   drop (absent: no good's window open).{label (the good), total (what the hold had), dropping, can (the amount can
//     be set), mine (absent: none).{label, can, reason}, ok.{can, reason}}
//   question (absent: none asked).{kind ("question", "shares"), title, text, lines.l<k>.{text} (the shares' lines),
//     accept.{label, can, reason}}
//   cannons (absent: the guns' window closed).{title, fitted, stock.{label, count}, calibres.c<k>.{key (the game's
//     good of the guns), label, sub, image, count, selected, can}, sides.s<k>.{key ("bow", "port", "starboard",
//     "stern"), now, most, can}, fit, remove, ok (absent: none).{label, can, reason}}
extern void TouchAdpShip_State(aref ui, aref out);
extern void TouchAdpShip_Ship(aref ui, int ship);              // ships s<k>
extern void TouchAdpShip_Rename(aref ui, string title);        // in a tap: the player names the ship in the game's field
extern void TouchAdpShip_Exchange(aref ui);
extern void TouchAdpShip_Guns(aref ui);                        // the guns' window opened
extern void TouchAdpShip_Morale(aref ui);
extern void TouchAdpShip_Shares(aref ui);
extern void TouchAdpShip_Accept(aref ui);                      // the question answered yes
extern void TouchAdpShip_Close(aref ui);                       // the window open closed
extern void TouchAdpShip_Good(aref ui, int good);              // goods g<k>: its window opened
extern void TouchAdpShip_Drop(aref ui, int amount);            // the open good: `amount` set to go overboard
extern void TouchAdpShip_Overboard(aref ui);
extern void TouchAdpShip_Mine(aref ui);
extern void TouchAdpShip_Calibre(aref ui, int calibre);        // cannons calibres c<k>
extern void TouchAdpShip_Side(aref ui, int side, int guns);    // cannons sides s<k> set to `guns`
extern void TouchAdpShip_Fit(aref ui);
extern void TouchAdpShip_RemoveGuns(aref ui);
extern void TouchAdpShip_GunsOk(aref ui);
extern void TouchAdpShip_Section(aref ui, string section);     // another hero section (hero, journal, …)
extern void TouchAdpShip_Leave(aref ui);
