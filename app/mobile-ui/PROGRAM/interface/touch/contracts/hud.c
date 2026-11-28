// SPDX-License-Identifier: GPL-3.0-only
// The HUD scenes' adapter (provides "adapter:hud"): what the game names on land, in the notions the scenes draw.
// Place(picture): the kind of a fast-travel place by the game's picture of it (a landCommands item's `picture`):
// store, shipyard, tavern, usurer, residence, prison, church, fort, portoffice, lantern, port; "" for a picture the
// adapter does not know (it says so).
extern string TouchAdpHud_Place(string picture);
// Role(dialog): whom the hero would talk to, by the dialog file the target talks through (combat `interactDialog`):
// a place kind of Place, ammo, officer, smuggler, soldier; "" for an ordinary person.
extern string TouchAdpHud_Role(string dialog);
// Command(source): what a root command of the land menu (the game's command event) does: travel (to a place of the
// town) and exit (out of the building), which have their own button, use, talk, exchange, take, put, chest, search,
// thought, repair, alchemy, atlas, rush, deck; "" for one the adapter does not know.
extern string TouchAdpHud_Command(string source);
// Context(action): the quick action the game's scripts published (`contextAction`): enter, take, open, toSea, toShip,
// action (one the adapter does not know by name, which it says); "" for none: nothing to do, or the talk that the
// engine's own target (combat `interact`) offers.
extern string TouchAdpHud_Context(string action);
// ShipKind(type): the kind of a ship by the engine's ship model name (a ship `type` of the ship channel): lugger, sloop,
// schooner, barkentine, brigantine, brig, caravel, fluyt, galleon, frigate, ketch, tartane, xebec, manowar; "" for a
// model the adapter has no kind for.
extern string TouchAdpHud_ShipKind(string type);
// Order(source): what a sea order does by the game's command event (a seaOrders item's `source`): moor, board,
// landTroops, map, sailTo, sailDir, sailAway, drift, attack, defend, reload, charge, speed, follow, ability, talk,
// kill, cabin, boat, brander, bomb, immediateReload, lightRepair, instantRepair, turn180; "" for one the
// adapter does not know (it says so).
extern string TouchAdpHud_Order(string source);
