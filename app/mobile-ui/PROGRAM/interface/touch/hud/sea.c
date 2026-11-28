// SPDX-License-Identifier: GPL-3.0-only
// The sea HUD, sailing (layout "sea") and in a sea battle ("sea-battle"): the squadron, the ship and menu buttons, the
// orders, the radar with the place, speed and date, the sail lever, the helm, the deck walk, the guns and the
// ammunition picker, the ship abilities, the spyglass, the time steps, the camera and the quick action; the engine's
// orders menu as a sheet. The speed, the date, the guns' reload and the abilities' fills move all the time: readings
// the runtime lays in. Which orders, actions and abilities exist and may be used now is the game's, from the engine's
// states; what the game names (its ship models, its orders) the adapter says in the HUD's notions.

#event_handler("UiBuild", "TouchHudSea_Build");
#event_handler("UiTap", "TouchHudSea_Tap");

object TouchHudSea;      // the scene's own state: layout (the last, kept under the orders), next (the time step a tap
                         // asks for), picks.<slot> (the control a picker slot sends), rows.<id> (a sheet row's list
                         // path), ships.k<n> (the order recipient of a fleet cell), quick.<element> (its action)
object TouchHudSea_Play; // the gameplay
object TouchHudSea_Ship;
object TouchHudSea_Orders;
object TouchHudSea_Quick;
object TouchHudSea_Spy;
object TouchHudSea_View;

// The ammunition kinds by the engine's names, in the order of its charge controls (hk_charge1…4).
string TouchHudSea_Ammo[4] = {"balls", "grapes", "knippels", "bombs"};
string TouchHudSea_AmmoIcons[4] = {"ammo", "grape", "chain", "bomb"};

// The icon of a sea order by what it does (the adapter's notion).
string TouchHudSea_OrderIcon(string notion)
{
	switch (notion)
	{
		case "moor": return "moor";
		case "board": return "grapple";
		case "landTroops": return "boat";
		case "map": return "map";
		case "sailTo": return "sail-to";
		case "sailDir": return "wheel";
		case "sailAway": return "sail-away";
		case "drift": return "drift";
		case "attack": return "swords";
		case "defend": return "shield";
		case "reload": return "exchange";
		case "charge": return "ammo";
		case "speed": return "speed";
		case "follow": return "follow";
		case "ability": return "energy";
		case "talk": return "talk";
		case "kill": return "skull";
		case "cabin": return "cabin";
		case "boat": return "boat";
		case "brander": return "brander";
		case "bomb": return "bomb";
		case "immediateReload": return "reload";
		case "lightRepair": return "repair";
		case "instantRepair": return "perk-instantrepair";
		case "turn180": return "maneuver";
	}
	return "orders";
}

// The icon of a ship ability the engine reports (its perk), a plain one for another.
string TouchHudSea_AbilityIcon(string id)
{
	switch (id)
	{
		case "ImmediateReload": return "reload";
		case "LightRepair": return "repair";
		case "InstantRepair": return "perk-instantrepair";
		case "Turn180": return "maneuver";
	}
	return "actions";
}

// The ship token of a ship model.
string TouchHudSea_ShipIcon(string type)
{
	string kind = TouchAdpHud_ShipKind(type);
	if (kind == "") return "ship";
	return "ship-" + kind;
}

bool TouchHudSea_Has(aref play, string control)
{
	return CheckAttribute(play, "controls." + control);
}

// The squadron column: the flagship first, then the companions, each the battle interface's ship picture ringed by
// its hull, crew and sails. A tap opens the orders for that ship, as a click on the engine's icon does.
void TouchHudSea_Fleet(aref view, aref ship, aref orders)
{
	int n = TouchLib_Count(ship, "fleet");
	if (n == 0) return;
	bool tap = sti(orders.known) && sti(orders.available) && !sti(orders.active);
	DeleteAttribute(&TouchHudSea, "ships");
	aref ships, vessel, cell, bars, bar;
	makearef(ships, view.elements.fleet.ships);
	for (int i = 0; i < n; i++)
	{
		vessel = TouchLib_Item(ship, "fleet", i);
		cell = TouchLib_Add(ships, "s");
		cell.name = vessel.name;
		cell.texture = vessel.texture;
		cell.uv = vessel.uv;
		makearef(bars, cell.bars);
		if (sti(vessel.hull) >= 0) { bar = TouchLib_Add(bars, "b"); bar.kind = "health"; bar.value = vessel.hull; }
		if (sti(vessel.crew) >= 0) { bar = TouchLib_Add(bars, "b"); bar.kind = "crew"; bar.value = vessel.crew; }
		if (sti(vessel.sails) >= 0) { bar = TouchLib_Add(bars, "b"); bar.kind = "sails"; bar.value = vessel.sails; }
		int r = TouchLib_Find(orders, "recipients", "character", vessel.character);
		if (r >= 0)
		{
			aref recipient = TouchLib_Item(orders, "recipients", r);
			TouchHudSea.ships.("k" + i) = recipient.id;
			if (tap) cell.tap = true;
		}
	}
	if (tap) UiOn("fleet", "ship");
}

// The radar's disc is the engine's (covered "navigator"); above it the place and the speed, under it the date. While
// the engine hides its own readouts (a command menu), so do we: the radar stays.
void TouchHudSea_Minimap(aref view, aref ship)
{
	if (!sti(ship.captions) && !sti(ship.navigator)) return;
	aref minimap;
	makearef(minimap, view.elements.minimap);
	minimap.native = ship.navigator;
	view.covers.minimap = "navigator";
	if (!sti(ship.captions))
	{
		minimap.readout = false;
		return;
	}
	if (ship.location != "") minimap.place = ship.location;
	if (CheckAttribute(ship, "readings.speed")) TouchLib_Reading(minimap, "speed", "ship.speed", "knots");
	if (CheckAttribute(ship, "readings.date")) TouchLib_Reading(minimap, "date", "ship.date", "word");
}

// The guns: the fire button filled while they reload, the loaded charge with its count and the other kinds behind it
// (a picker: its slots ammo-1…). Every kind whose control the engine binds can be chosen, an empty one too: the
// scripts switch to it and their reload refuses to load from an empty hold.
void TouchHudSea_Guns(aref view, aref play, aref ship, bool spyglass)
{
	if (TouchHudSea_Has(play, "Ship_Fire"))
	{
		view.elements.fire = "";
		if (sti(ship.current) && CheckAttribute(ship, "readings.charge"))
		{
			aref fire;
			makearef(fire, view.elements.fire);
			TouchLib_Reading(fire, "fill", "ship.charge", "percent");
			fire.fillTone = "reload";
			fire.reloading = true;
		}
		UiOn("fire", "click");
	}
	if (!sti(ship.current)) return;
	int loaded = -1;
	int others = 0;
	int i;
	for (i = 0; i < 4; i++)
	{
		if (ship.ammo == TouchHudSea_Ammo[i]) loaded = i;
		else if (TouchHudSea_Has(play, "hk_charge" + (i + 1))) others++;
	}
	if (loaded < 0) return; // no loaded kind known: no button
	aref ammo, slot;
	makearef(ammo, view.elements.ammo);
	ammo.icon = TouchHudSea_AmmoIcons[loaded];
	ammo.label = UiText("hud.sea.ammo." + TouchHudSea_Ammo[loaded]);
	int count = sti(ship.ammoCount);
	if (count >= 0)
	{
		ammo.badge = count;
		ammo.badgeTone = "gold";
		if (sti(ship.ammoLow) || count == 0) ammo.badgeTone = "danger";
		// The loaded kind ran out: its 0 is a count, the switch to another kind stays live.
		if (count == 0 && others > 0) ammo.countOnly = true;
	}
	// The raised spyglass keeps the guns, not the open picker.
	if (others == 0 || spyglass) return;
	view.controls.ammo.kind = "fan";
	int k = 1;
	for (i = 0; i < 4; i++)
	{
		if (i == loaded) continue;
		string control = "hk_charge" + (i + 1);
		bool loadable = TouchHudSea_Has(play, control);
		makearef(slot, view.elements.("ammo-" + k));
		slot.icon = TouchHudSea_AmmoIcons[i];
		slot.label = UiText("hud.sea.ammo." + TouchHudSea_Ammo[i]);
		slot.disabled = !loadable;
		int left = sti(ship.ammoCounts.(TouchHudSea_Ammo[i]));
		if (left >= 0) slot.badge = left;
		if (left == 0) { slot.badgeTone = "danger"; slot.countOnly = true; }
		if (loadable)
		{
			TouchHudSea.picks.("ammo-" + k) = control;
			UiOn("ammo-" + k, "click");
		}
		k++;
	}
}

// The ship abilities behind one button (a picker: its slots abilities-1…): ready (a tap launches it through the
// engine's command BI_<id>), running (its fill and seconds), recovering and blocked (greyed); one whose command the
// engine does not bind now is blocked.
void TouchHudSea_Abilities(aref view, aref play, aref ship)
{
	int n = TouchLib_Count(ship, "abilities");
	if (n == 0) return;
	view.controls.abilities.kind = "fan";
	aref button, slot, ability;
	makearef(button, view.elements.abilities);
	button.icon = "rank";
	button.label = UiText("hud.ability.group");
	button.cue = false;
	for (int i = 0; i < n; i++)
	{
		ability = TouchLib_Item(ship, "abilities", i);
		string state = ability.state;
		if (state == "ready" && !TouchHudSea_Has(play, "BI_" + ability.id)) state = "blocked";
		string at = "abilities-" + (i + 1);
		makearef(slot, view.elements.(at));
		slot.icon = TouchHudSea_AbilityIcon(ability.id);
		slot.label = ability.label;
		if (slot.label == "") slot.label = UiText("hud.ability.name." + ability.id);
		if (state == "active" || state == "recovery")
		{
			TouchLib_Reading(slot, "fill", "ship.ability." + ability.id + ".fill", "percent");
			slot.fillTone = "cooldown";
			if (state == "active") slot.fillTone = "reload";
			slot.reloading = true;
			TouchLib_Reading(slot, "badge", "ship.ability." + ability.id + ".left", "whole");
		}
		if (state == "recovery" || state == "blocked") slot.disabled = true;
		if (state == "ready")
		{
			button.cue = true;
			TouchHudSea.picks.(at) = "BI_" + ability.id;
			UiOn(at, "click");
		}
	}
	if (CheckAttribute(ship, "readings.abilitiesRunning"))
	{
		TouchLib_Reading(button, "fill", "ship.abilitiesRunning", "percent");
		button.fillTone = "reload";
		button.reloading = true;
	}
}

// The quick action with its own look; the world map beside it when the game allows both.
void TouchHudSea_Action(aref view, aref quick)
{
	if (!sti(quick.available)) return;
	string icon = "";
	string variant = "primary";
	switch (quick.action)
	{
		case "Map": icon = "map"; break;
		case "Moor": icon = "moor"; break;
		case "Board": icon = "grapple"; variant = "combat"; break;
		case "LandTroops": icon = "boat"; variant = "combat"; break;
		case "Reload": icon = "exchange"; break;
	}
	if (icon == "") return; // the engine offers only these five
	view.elements.context.icon = icon;
	view.elements.context.label = quick.label;
	view.elements.context.variant = variant;
	TouchHudSea.quick.context = quick.action;
	UiOn("context", "click");
	if (!sti(quick.map)) return;
	view.elements.map.icon = "map";
	view.elements.map.label = quick.mapLabel;
	view.elements.map.variant = "primary";
	TouchHudSea.quick.map = "Map";
	UiOn("map", "click");
}

// The engine's orders as a sheet: the ships on the left (another one is chosen at the first stage only, as in the
// engine's menu), the commands and options as tiles, abilities and targets as rows. A tap on a row acts at once; the
// runtime selects and activates it as the engine wants.
void TouchHudSea_Sheet(aref view, aref orders, aref ship)
{
	bool root = orders.stage == "commands";
	view.sheet = "s-hud-orders";
	aref sheet, list, row, entry, item;
	makearef(sheet, view.sheet);
	sheet.title = UiText("hud.orders.title");
	sheet.back = !root && sti(orders.canBack);
	sheet.crumb = "";
	if (!root) sheet.crumb = orders.title;
	DeleteAttribute(&TouchHudSea, "rows");
	DeleteAttribute(&TouchHudSea, "slots");
	DeleteAttribute(&TouchHudSea, "pressed");
	makearef(list, sheet.recipients);
	int n = TouchLib_Count(orders, "recipients");
	int i;
	for (i = 0; i < n; i++)
	{
		entry = TouchLib_Item(orders, "recipients", i);
		string id = "recipient-" + entry.id;
		bool selected = entry.id == orders.recipient;
		row = TouchLib_Add(list, "r");
		row.id = id;
		row.title = entry.name;
		row.meta = "";
		if (sti(entry.main)) row.meta = UiText("hud.orders.flagship");
		row.image.icon = "ship";
		row.image.enamel = "teal";
		row.value = "";
		row.selected = selected;
		int k = TouchLib_Find(ship, "fleet", "character", entry.character);
		if (k >= 0)
		{
			aref vessel = TouchLib_Item(ship, "fleet", k);
			row.image.icon = TouchHudSea_ShipIcon(vessel.type);
			if (sti(vessel.hull) >= 0) row.value = vessel.hull + "%";
			if (selected && !sti(entry.main) && sti(vessel.hull) >= 0 && sti(vessel.crew) >= 0 && sti(vessel.sails) >= 0)
			{
				row.bars.hull = vessel.hull;
				row.bars.crew = vessel.crew;
				row.bars.sails = vessel.sails;
			}
		}
		// Another ship is chosen at the first stage only, as in the engine's menu; later its tap does nothing.
		TouchHudSea.rows.(id) = "recipient";
		if (!root) TouchHudSea.rows.(id) = "";
		UiOn(id, "click");
	}
	n = TouchLib_Count(orders, "items");
	for (i = 0; i < n; i++)
	{
		item = TouchLib_Item(orders, "items", i);
		string kind = item.kind;
		if (kind == "cancel") continue;
		string at = "rows";
		if (kind == "command" || kind == "charge" || kind == "option") at = "tiles";
		makearef(list, sheet.(at));
		string slot = "e" + GetAttributesNum(list);
		row = TouchLib_Add(list, "e");
		row.id = "item-" + item.id;
		row.title = item.label;
		row.image.icon = TouchHudSea_ItemIcon(item);
		// Ships and forts are objects: enamel tokens (the engine does not say whose side a target is on).
		if (kind == "ship" || kind == "fort") row.image.enamel = "iron";
		row.disabled = !sti(item.enabled);
		row.pressed = false;
		row.cooldown = item.cooldown;
		TouchHudSea.rows.(row.id) = at;
		TouchHudSea.slots.(row.id) = slot;
		UiOn(row.id, "click");
	}
	UiOn("sheet-back", "click");
	UiOn("sheet-close", "click");
	UiOn("sheet", "close");
	if (root) UiBackTo("sheet-close"); else UiBackTo("sheet-back");
}

string TouchHudSea_ItemIcon(aref item)
{
	int i;
	switch (item.kind)
	{
		case "ship": return "ship";
		case "fort": return "fort";
		case "land": return "port";
		case "town": return "port";
		case "charge":
			// The engine names a charge by its goods ("balls"): the ammunition icons.
			for (i = 0; i < 4; i++)
				if (findSubStr(item.source, TouchHudSea_Ammo[i], 0) >= 0) return TouchHudSea_AmmoIcons[i];
			return "ammo";
	}
	string notion = TouchAdpHud_Order(item.source);
	if (notion == "" && item.kind == "ability") return "energy";
	return TouchHudSea_OrderIcon(notion);
}

void TouchHudSea_Build()
{
	aref ui = GetEventData();
	if (ui.screen != "sea") return;
	if (UiFirst()) DeleteAttribute(&TouchHudSea, "");
	UiState("gameplay", &TouchHudSea_Play);
	UiState("ship", &TouchHudSea_Ship);
	UiState("seaOrders", &TouchHudSea_Orders);
	UiState("seaQuickAction", &TouchHudSea_Quick);
	UiState("spyglass", &TouchHudSea_Spy);
	aref view, play, ship, orders;
	makearef(view, TouchHudSea_View);
	makearef(play, TouchHudSea_Play);
	makearef(ship, TouchHudSea_Ship);
	makearef(orders, TouchHudSea_Orders);
	DeleteAttribute(view, "");
	DeleteAttribute(&TouchHudSea, "picks");
	DeleteAttribute(&TouchHudSea, "quick");
	// An open orders menu blocks the gameplay (mode "unknown"): the last layout stays under it.
	if (play.mode == "sailing") TouchHudSea.layout = "sea";
	if (play.mode == "naval") TouchHudSea.layout = "sea-battle";
	if (!CheckAttribute(&TouchHudSea, "layout")) TouchHudSea.layout = "sea";
	view.layout = TouchHudSea.layout;
	bool current = sti(ship.current);
	bool menu = sti(orders.known) && sti(orders.active);
	view.holds = menu;
	if (current) TouchHudSea_Fleet(view, ship, orders);
	if (menu)
	{
		// One the engine marked as not drawable stays the engine's (the runtime says so).
		if (sti(orders.complete))
		{
			if (current) TouchHudSea_Minimap(view, ship);
			TouchHudSea_Sheet(view, orders, ship);
		}
		UiShow(view);
		return;
	}
	view.log = true;
	view.band.after = "fleet";
	view.band.row = "ship pause orders";
	view.band.under = "minimap";
	// At sea the person button is the ship's: it opens the ship screen.
	view.elements.ship = "";
	UiOn("ship", "click");
	view.elements.pause = "";
	UiOn("pause", "click");
	if (sti(orders.known) && sti(orders.available))
	{
		view.elements.orders = "";
		UiOn("orders", "click");
	}
	// The helm floats in its zone; first person on deck it stays where it rests (the walk stick floats there).
	bool deck = TouchHudSea_Has(play, "DeckCamera_Forward") || TouchHudSea_Has(play, "DeckCamera_Backward");
	if (current)
	{
		TouchHudSea_Minimap(view, ship);
		if (sti(ship.canSetSails) && sti(ship.sails) >= 0)
		{
			view.elements.sail.value = ship.sails;
			UiOn("sail", "change");
		}
	}
	if (TouchHudSea_Has(play, "Ship_TurnLeft") || TouchHudSea_Has(play, "Ship_TurnRight"))
	{
		view.elements.helm.fixed = deck;
		view.controls.helm.kind = "helm";
		view.controls.helm.left = "Ship_TurnLeft";
		view.controls.helm.right = "Ship_TurnRight";
	}
	// The spyglass is up or down as the game has it, never ahead of it.
	bool spyglass = sti(TouchHudSea_Spy.up);
	// The deck walk stick left of the guns; the raised spyglass has no walk zone.
	if (deck && !spyglass)
	{
		view.elements.walk = "";
		view.controls.walk.kind = "deck";
	}
	TouchHudSea_Guns(view, play, ship, spyglass);
	TouchHudSea.next = "";
	if (current && CheckAttribute(ship, "timeScale")) TouchHudSea.next = TouchLib_TimeScale(view, play, ship.timeScale);
	if (TouchHudSea_Has(play, "TelescopeIn") && TouchHudSea_Has(play, "TelescopeOut"))
	{
		view.elements.spyglass.variant = "normal";
		view.elements.spyglass.label = UiText("hud.sea.spyglass.raise");
		if (spyglass)
		{
			view.elements.spyglass.variant = "active";
			view.elements.spyglass.label = UiText("hud.sea.spyglass.lower");
			UiBackTo("spyglass");
		}
		UiOn("spyglass", "click");
	}
	if (spyglass)
	{
		// The raised spyglass keeps the steering and the guns; the top row, the squadron and the radar give the
		// target card and the look their room.
		DeleteAttribute(view, "elements.ship");
		DeleteAttribute(view, "elements.pause");
		DeleteAttribute(view, "elements.orders");
		DeleteAttribute(view, "elements.fleet");
		DeleteAttribute(view, "elements.minimap");
		DeleteAttribute(view, "covers");
		UiShow(view);
		return;
	}
	if (current) TouchHudSea_Abilities(view, play, ship);
	TouchHudSea_Action(view, &TouchHudSea_Quick);
	if (TouchHudSea_Has(play, "Sea_CameraSwitch"))
	{
		view.elements.camera = "";
		UiOn("camera", "click");
	}
	UiShow(view);
}

void TouchHudSea_Control(string control)
{
	TouchLib_Command("control", "control", control);
}

void TouchHudSea_Menu(string verb, string id)
{
	object fields = {menu = "sea", verb = "", id = ""};
	fields.verb = verb;
	fields.id = id;
	UiCommand("menu", &fields);
}

void TouchHudSea_Tap()
{
	aref ui = GetEventData();
	if (ui.screen != "sea") return;
	string id = GetEventData(), verb = GetEventData(), value = GetEventData();
	switch (id)
	{
		case "ship": TouchLib_Command("shortcut", "name", "shipscreen"); return;
		case "pause": TouchLib_Command("shortcut", "name", "mainmenu"); return;
		case "orders": TouchHudSea_Menu("open", ""); return;
		case "fire": TouchHudSea_Control("Ship_Fire"); return;
		case "camera": TouchHudSea_Control("Sea_CameraSwitch"); return;
		case "time": TouchHudSea_Control(TouchHudSea.next); return;
		case "sheet-back": TouchHudSea_Menu("back", ""); return;
		case "sheet-close": TouchHudSea_Menu("close", ""); return;
		case "sheet": TouchHudSea_Menu("close", ""); return;
	}
	if (id == "spyglass")
	{
		// Asked of the game: the button changes when the game's spyglass state says it did.
		if (sti(TouchHudSea_Spy.up)) TouchHudSea_Control("TelescopeOut");
		else TouchHudSea_Control("TelescopeIn");
		return;
	}
	if (id == "sail")
	{
		// The lever at the position asked, drawn so until the game answers.
		int position = UiAmount(value, "the sail lever's position");
		object fields = {position = "0"};
		fields.position = position;
		UiCommand("sails", &fields);
		TouchHudSea_View.elements.sail.value = position;
		UiPredict(&TouchHudSea_View);
		return;
	}
	if (id == "fleet")
	{
		// A tap on a ship opens the orders for it (only a ship the engine gives orders to is tappable).
		string k = "k" + UiAmount(value, "the tapped ship");
		if (CheckAttribute(&TouchHudSea, "ships." + k)) TouchHudSea_Menu("open", TouchHudSea.ships.(k));
		return;
	}
	if (CheckAttribute(&TouchHudSea, "quick." + id))
	{
		TouchLib_Command("seaAction", "action", TouchHudSea.quick.(id));
		return;
	}
	if (CheckAttribute(&TouchHudSea, "picks." + id))
	{
		TouchHudSea_Control(TouchHudSea.picks.(id));
		return;
	}
	if (CheckAttribute(&TouchHudSea, "rows." + id))
	{
		string at = TouchHudSea.rows.(id);
		if (at == "") return;
		if (at == "recipient")
		{
			TouchHudSea_Menu("recipient", TouchLib_Cut(id, 10, strlen(id)));
			return;
		}
		// A row: drawn pressed until the game has answered its steps.
		TouchHudSea_Menu("choose", TouchLib_Cut(id, 5, strlen(id)));
		// One row is drawn pressed: a tap before the engine answered the last one replaces it.
		if (CheckAttribute(&TouchHudSea, "pressed"))
		{
			string was = TouchHudSea.pressed;
			TouchHudSea_View.sheet.(TouchHudSea.rows.(was)).(TouchHudSea.slots.(was)).pressed = false;
		}
		TouchHudSea.pressed = id;
		string slot = TouchHudSea.slots.(id);
		TouchHudSea_View.sheet.(at).(slot).pressed = true;
		UiPredict(&TouchHudSea_View);
		return;
	}
	UiError("sea HUD: a tap on '" + id + "' (" + verb + ") the scene did not offer");
}
