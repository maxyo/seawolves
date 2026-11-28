// SPDX-License-Identifier: GPL-3.0-only
// The land HUD, exploring (layout "land") and fighting on foot ("land-combat"): the party and its effects, the place,
// the walk stick, the menu and the hero buttons, the weapon, the potion and Rush behind one picker, the context button
// with its hint at the bottom, the fight's strike wheel, block, shot and dash, the boarding's bars, and the engine's
// land menu as a sheet. The bars, fills and men counts move every frame: readings the runtime lays in. Which commands,
// actions and abilities exist and whether they can be used now is the game's, from the engine's states; what the
// game names (its places, people, commands, quick actions) the adapter says in the HUD's notions.

#event_handler("UiBuild", "TouchHudLand_Build");
#event_handler("UiTap", "TouchHudLand_Tap");

object TouchHudLand;         // the last build's taps: travel (the travel command), context, picks.<slot>, rows.<id>,
                             // party.<k> (the person of portrait k)
object TouchHudLand_PlayState;    // the gameplay
object TouchHudLand_CombatState;  // the land actions the engine offers
object TouchHudLand_VitalsState;  // the party
object TouchHudLand_MenuState;    // the engine's land menu
object TouchHudLand_View;

// The sword moves, the wheel's sectors clockwise from the top left, then its centre.
string TouchHudLand_Moves[6] = {"feint", "round", "break", "parry", "force", "fast"};
string TouchHudLand_MoveIcons[6] = {"feint", "attack-round", "attack-pierce", "parry", "attack-heavy", "attack-fast"};
// The travel places in one order of their kinds, so that a kind keeps its tile from town to town (the port, which
// comes and goes, last).
string TouchHudLand_PlaceOrder[11] = {"store", "shipyard", "tavern", "residence", "usurer", "church", "lantern",
	"portoffice", "prison", "fort", "port"};
string TouchHudLand_EffectIds[4] = {"poison", "drunk", "healing", "walkonly"};
string TouchHudLand_EffectIcons[4] = {"skull", "rum", "potion", "lock"};
string TouchHudLand_EffectTones[4] = {"danger", "effect", "active", "effect"};

// The icon of a command of the land menu by what it does.
string TouchHudLand_CommandIcon(string notion)
{
	switch (notion)
	{
		case "travel": return "travel";
		case "exit": return "door";
		case "use": return "potion";
		case "talk": return "talk";
		case "exchange": return "exchange";
		case "take": return "hand";
		case "put": return "put";
		case "chest": return "chest";
		case "search": return "search";
		case "thought": return "thought";
		case "repair": return "repair";
		case "alchemy": return "retort";
		case "atlas": return "map";
		case "rush": return "energy";
		case "deck": return "boat";
	}
	return "actions";
}

// The talk hint's icon by whom the hero talks to: a place's keeper has the place's icon.
string TouchHudLand_RoleIcon(string role)
{
	switch (role)
	{
		case "": return "talk";
		case "smuggler": return "sack";
		case "soldier": return "musket";
	}
	return role;
}

string TouchHudLand_ContextIcon(string notion)
{
	switch (notion)
	{
		case "enter": return "door";
		case "open": return "chest";
		case "toSea": return "boat";
		case "toShip": return "boat";
	}
	return "hand";
}

int TouchHudLand_PlaceRank(string notion)
{
	for (int i = 0; i < 11; i++)
		if (TouchHudLand_PlaceOrder[i] == notion) return i;
	return 11;
}

bool TouchHudLand_Transition(string source)
{
	string notion = TouchAdpHud_Command(source);
	return notion == "travel" || notion == "exit";
}

// The location line names the place only: town · place.
void TouchHudLand_Location(aref view)
{
	object info;
	UiState("worldInfo", &info);
	int n = TouchLib_Count(&info, "names");
	if (n == 0) return;
	aref last = TouchLib_Item(&info, "names", n - 1);
	string text = GetAttributeValue(last);
	if (n > 1) text = GetAttributeValue(TouchLib_Item(&info, "names", n - 2)) + " · " + text;
	view.elements.location.text = text;
}

// The effects row under the bars: one small icon each, in a fixed order.
void TouchHudLand_Effects(aref view, aref vitals)
{
	aref states, state;
	makearef(states, view.elements.effects.states);
	for (int i = 0; i < 4; i++)
	{
		if (!CheckAttribute(vitals, "effects." + TouchHudLand_EffectIds[i])) continue;
		state = TouchLib_Add(states, "s");
		state.icon = TouchHudLand_EffectIcons[i];
		state.fill = 100;
		state.fillTone = TouchHudLand_EffectTones[i];
		state.label = UiText("hud.effect." + TouchHudLand_EffectIds[i]);
	}
	if (GetAttributesNum(states) == 0) DeleteAttribute(view, "elements.effects");
}

aref TouchHudLand_Member(aref list, aref person, string health, string energy, bool tap)
{
	aref cell = TouchLib_Add(list, "m");
	cell.name = person.name;
	cell.texture = person.portrait.texture;
	cell.uv = person.portrait.uv;
	aref bars, bar;
	makearef(bars, cell.bars);
	bar = TouchLib_Add(bars, "b");
	bar.kind = "health";
	TouchLib_Reading(bar, "value", health, "percent");
	bar = TouchLib_Add(bars, "b");
	bar.kind = "energy";
	TouchLib_Reading(bar, "value", energy, "percent");
	if (tap) cell.tap = true;
	return cell;
}

// The party row: the hero first, then the officers who walk with him, each ringed by health and energy. A tap opens
// the character screen on that person; the game refuses that screen in a fight, so there it is not offered.
void TouchHudLand_Party(aref view, aref vitals, bool melee)
{
	bool tap = !melee && sti(vitals.hero) >= 0;
	aref list;
	makearef(list, view.elements.party.officers);
	TouchHudLand_Member(list, vitals, "vitals.health", "vitals.energy", tap);
	TouchHudLand.party.k0 = vitals.hero;
	int n = TouchLib_Count(vitals, "officers");
	for (int k = 0; k < n; k++)
	{
		aref officer = TouchLib_Item(vitals, "officers", k);
		TouchHudLand_Member(list, officer, "vitals.officerHealth." + k, "vitals.officerEnergy." + k, tap);
		TouchHudLand.party.("k" + (k + 1)) = officer.id;
	}
	if (tap) UiOn("party", "member");
}

// The menu split: the actions at the top; the travel to the town's places under the thumb (none in a fight).
void TouchHudLand_MenuButtons(aref view, aref menu, bool melee)
{
	aref offered, command;
	makearef(offered, menu.offered);
	bool actions = false;
	string travel = "";
	for (int i = 0; i < GetAttributesNum(offered); i++)
	{
		string source = GetAttributeName(GetAttributeN(offered, i));
		if (!TouchHudLand_Transition(source)) actions = true;
		if (TouchAdpHud_Command(source) == "travel") travel = source;
	}
	if (actions)
	{
		view.elements.actions.label = UiText("hud.sheet.actions");
		UiOn("actions", "click");
	}
	if (!melee && travel != "")
	{
		view.elements.travel.label = UiText("hud.travel");
		TouchHudLand.travel = travel;
		UiOn("travel", "click");
	}
}

string TouchHudLand_AbilityIcon(string id)
{
	if (id == "Rush") return "flame";
	return "actions";
}

// The potion and, in a fight, Rush behind one button (a picker the runtime opens: its slots abilities-1…): the potion
// with the count carried, greyed with nothing to drink, filled while one heals; Rush under the game's name, launched
// through the land menu's own command.
void TouchHudLand_Abilities(aref view, aref combat, aref vitals, bool ready, bool melee)
{
	bool potion = CheckAttribute(combat, "actions.potion");
	bool known = sti(vitals.known);
	int rush = 0;
	if (melee && known) rush = TouchLib_Count(vitals, "abilities");
	if (!potion && rush == 0) return;
	view.controls.abilities.kind = "fan";
	aref button, slot, ability;
	makearef(button, view.elements.abilities);
	button.icon = "rank";
	button.label = UiText("hud.ability.group");
	button.cue = false;
	int k = 1;
	if (potion)
	{
		makearef(slot, view.elements.("abilities-" + k));
		slot.icon = "potion";
		slot.label = UiText("hud.potion");
		bool none = known && sti(vitals.potions) == 0;
		slot.disabled = !ready || none;
		if (known)
		{
			slot.badge = vitals.potions;
			if (CheckAttribute(vitals, "effects.healing")) { slot.fill = 100; slot.fillTone = "active"; }
		}
		if (ready && !none)
		{
			TouchHudLand.picks.("abilities-" + k) = "";
			UiOn("abilities-" + k, "click");
		}
		k++;
	}
	for (int i = 0; i < rush; i++)
	{
		ability = TouchLib_Item(vitals, "abilities", i);
		string state = ability.state;
		makearef(slot, view.elements.("abilities-" + k));
		slot.icon = TouchHudLand_AbilityIcon(ability.id);
		slot.label = ability.label;
		if (slot.label == "") slot.label = UiText("hud.ability.name." + ability.id);
		if (state == "active" || state == "recovery")
		{
			TouchLib_Reading(slot, "fill", "vitals.ability." + ability.id + ".fill", "percent");
			slot.fillTone = "cooldown";
			if (state == "active") slot.fillTone = "reload";
			slot.reloading = true;
			TouchLib_Reading(slot, "badge", "vitals.ability." + ability.id + ".left", "whole");
		}
		if (state == "recovery" || state == "blocked") slot.disabled = true;
		if (state == "ready") button.cue = true;
		// One without its land menu command is the engine's to report; it launches nothing.
		if (state == "ready" && ability.command != "")
		{
			TouchHudLand.picks.("abilities-" + k) = ability.command;
			UiOn("abilities-" + k, "click");
		}
		k++;
	}
	if (CheckAttribute(vitals, "readings.abilitiesRunning"))
	{
		TouchLib_Reading(button, "fill", "vitals.abilitiesRunning", "percent");
		button.fillTone = "reload";
		button.reloading = true;
	}
}

// The context button and the hint that says what it does, from one place: the hint never names another target than
// the button acts on (a guard beside the door the button opens). The hint stays a little once what it names is gone.
void TouchHudLand_Context(aref view, aref combat, bool ready)
{
	aref context, hint;
	makearef(context, view.elements.context);
	makearef(hint, view.elements.hint);
	view.lingers.hint = true;
	if (!ready)
	{
		DeleteAttribute(view, "elements.context");
		DeleteAttribute(view, "elements.hint");
		return;
	}
	if (CheckAttribute(combat, "actions.interact"))
	{
		// The engine's action key: somebody to talk to stands in front of the hero, or the game waits for it to go on
		// after a won boarding fight (it outranks the script's quick action).
		string kind = combat.interactKind;
		string key = "hud.context.action";
		context.icon = "hand";
		if (kind == "dialog") { key = "hud.context.talk"; context.icon = "talk"; }
		if (kind == "boarding") { key = "hud.context.boarding"; context.icon = "grapple"; }
		context.label = UiText(key);
		TouchHudLand.context = "";
		UiOn("context", "click");
		if (kind != "dialog")
		{
			DeleteAttribute(view, "elements.hint");
			DeleteAttribute(view, "lingers");
			return;
		}
		hint.icon = TouchHudLand_RoleIcon(TouchAdpHud_Role(combat.interactDialog));
		hint.text = context.label;
		if (combat.interactName != "") hint.text = context.label + ": " + combat.interactName;
		return;
	}
	object action, door;
	UiState("contextAction", &action);
	UiState("reloadPlace", &door);
	string notion = TouchAdpHud_Context(action.name);
	if (notion == "")
	{
		DeleteAttribute(view, "elements.context");
		// No button: a door the game keeps closed now still says where it leads, with a closed lock.
		if (sti(door.locked) && door.place != "") { hint.icon = "lock"; hint.text = door.place; }
		else DeleteAttribute(view, "elements.hint");
		return;
	}
	context.icon = TouchHudLand_ContextIcon(notion);
	context.label = UiText("hud.context." + notion);
	TouchHudLand.context = action.name;
	UiOn("context", "click");
	// A door: where it leads (an open lock); a door, a thing or a chest the game names nothing for: the button's words.
	if (notion == "enter" && door.place != "") { hint.icon = "unlock"; hint.text = door.place; return; }
	if (notion == "enter" || notion == "take" || notion == "open")
	{
		hint.icon = context.icon;
		hint.text = context.label;
		return;
	}
	DeleteAttribute(view, "elements.hint");
	DeleteAttribute(view, "lingers");
}

// The fight: the strike and its move wheel, block, the shot, the dash, the cabin's captain or the deck's two crews.
void TouchHudLand_Combat(aref view, aref combat, aref vitals, bool ready)
{
	aref wheel, moves, move;
	makearef(moves, view.elements.moves.moves);
	makearef(wheel, view.controls.strike);
	bool any = false;
	for (int i = 0; i < 6; i++)
	{
		string id = TouchHudLand_Moves[i];
		move = TouchLib_Add(moves, "m");
		move.id = id;
		move.icon = TouchHudLand_MoveIcons[i];
		move.sector = "" + i;
		if (i == 5) move.sector = "centre";
		move.disabled = !CheckAttribute(combat, "actions." + id);
		if (!sti(move.disabled)) any = true;
		wheel.captions.(id) = UiText("hud.move." + id);
	}
	if (any)
	{
		view.elements.strike.disabled = !ready;
		wheel.kind = "wheel";
		wheel.moves = "moves";
		wheel.dims = "strike block shot dash abilities weapon";
	}
	else
	{
		DeleteAttribute(view, "elements.moves");
		DeleteAttribute(view, "controls.strike");
	}
	if (CheckAttribute(combat, "actions.block"))
	{
		view.elements.block.disabled = !ready;
		UiOn("block", "down");
		UiOn("block", "up");
		UiOn("block", "click");
	}
	bool known = sti(vitals.known);
	if (CheckAttribute(combat, "actions.pistol"))
	{
		aref shot;
		makearef(shot, view.elements.shot);
		if (combat.pistol == "no_weapon")
		{
			// Without a firearm the shot stays, inactive: a tap says why.
			shot.disabled = true;
			shot.reason = UiText("hud.combat.noFirearm");
		}
		else
		{
			bool loading = combat.pistol == "unloaded";
			shot.disabled = !ready || loading;
			shot.reloading = loading;
			if (loading)
			{
				TouchLib_Reading(shot, "fill", "combat.reload", "percent");
				shot.fillTone = "reload";
			}
			if (known && sti(vitals.capacity) > 0) shot.badge = vitals.loaded;
			UiOn("shot", "click");
		}
	}
	if (CheckAttribute(combat, "actions.dash") && known && vitals.dash != "none")
	{
		bool cooling = vitals.dash == "cooling";
		aref dash;
		makearef(dash, view.elements.dash);
		dash.disabled = !ready || cooling;
		if (cooling)
		{
			TouchLib_Reading(dash, "fill", "vitals.dash", "percent");
			dash.fillTone = "cooldown";
		}
		UiOn("dash", "click");
	}
	if (!known) return;
	// The enemy captain of a boarding cabin, or the boarding deck's two crews: in the place of the location line.
	if (vitals.opponent != "")
	{
		view.elements.opponent.title = UiText("hud.captain") + " " + vitals.opponent;
		aref health;
		makearef(health, view.elements.("opponent-hp"));
		TouchLib_Reading(health, "value", "vitals.opponent", "percent");
		DeleteAttribute(view, "elements.location");
		return;
	}
	if (!sti(vitals.boarding)) return;
	aref deck;
	makearef(deck, view.elements.boarding);
	TouchLib_Reading(deck, "ours", "vitals.ours", "percent");
	TouchLib_Reading(deck, "theirs", "vitals.theirs", "percent");
	TouchLib_Reading(deck, "oursCount", "vitals.oursMen", "whole");
	TouchLib_Reading(deck, "theirsCount", "vitals.theirsMen", "whole");
	if (CheckAttribute(view, "elements.location.text")) deck.title = view.elements.location.text;
	DeleteAttribute(view, "elements.location");
}

string TouchHudLand_RowIcon(aref item)
{
	if (item.kind == "potion") return "potion";
	if (item.kind == "ability") return "energy";
	if (item.kind == "travel")
	{
		string place = TouchAdpHud_Place(item.picture);
		if (place == "") return "travel";
		return place;
	}
	return TouchHudLand_CommandIcon(TouchAdpHud_Command(item.source));
}

// The engine's land menu as a sheet: the root's commands as rows, the travel places as tiles, potions and abilities
// under headings. A tap on a row acts at once; the runtime selects and activates it as the engine wants.
void TouchHudLand_Sheet(aref view, aref menu)
{
	bool root = menu.stage == "commands";
	bool travel = menu.stage == "travel";
	object items;
	aref item, row, rows;
	int n = TouchLib_Count(menu, "items");
	for (int i = 0; i < n; i++)
	{
		item = TouchLib_Item(menu, "items", i);
		if (item.kind == "cancel") continue;
		if (root && item.kind == "command" && TouchHudLand_Transition(item.source)) continue; // their own button
		row = TouchLib_Add(&items, "i");
		CopyAttributes(row, item);
		row.rank = 0;
		if (travel) row.rank = TouchHudLand_PlaceRank(TouchAdpHud_Place(item.picture));
	}
	// The place the hero stands in: the game leaves it out of the list; its tile is in its slot, marked, not tappable.
	if (travel && CheckAttribute(menu, "here"))
	{
		row = TouchLib_Add(&items, "i");
		row.id = "here";
		row.kind = "travel";
		row.label = menu.here.label;
		row.picture = menu.here.picture;
		row.enabled = false;
		row.cooldown = 0;
		row.source = "";
		row.here = true;
		row.rank = TouchHudLand_PlaceRank(TouchAdpHud_Place(menu.here.picture));
	}
	object ordered;
	TouchLib_Order(&items, "rank", false, &ordered);
	view.sheet = "s-hud-sheet";
	makearef(rows, view.sheet.rows);
	string group = "";
	int headings = 0;
	for (i = 0; i < GetAttributesNum(&ordered); i++)
	{
		item = GetAttributeN(&ordered, i);
		row = TouchLib_Add(rows, "r");
		row.id = item.id;
		row.title = item.label;
		row.icon = TouchHudLand_RowIcon(item);
		row.disabled = !sti(item.enabled);
		row.pressed = false;
		row.chevron = item.kind == "command" && TouchAdpHud_Command(item.source) == "use";
		row.cooldown = item.cooldown;
		if (CheckAttribute(item, "here"))
		{
			row.here = true;
			UiOn(row.id, "refused"); // the disabled tile shakes and asks the game nothing
			continue;
		}
		// Potions and abilities share one stage: each kind under its own heading.
		if (!root && !travel && item.kind != group)
		{
			group = item.kind;
			headings++;
			if (group == "potion") row.heading = UiText("hud.sheet.potions");
			else row.heading = UiText("hud.sheet.abilities");
		}
		TouchHudLand.rows.(item.id) = GetAttributeName(row);
		UiOn(row.id, "click");
	}
	view.sheet.title = menu.title;
	if (menu.title == "") view.sheet.title = UiText("hud.sheet.actions");
	view.sheet.back = !root && !travel && sti(menu.canBack);
	view.sheet.layout = "dense";
	if (root) view.sheet.layout = "rows";
	if (travel) view.sheet.layout = "tiles";
	view.sheet.headings = headings;
	UiOn("sheet-back", "click");
	UiOn("sheet-close", "click");
	UiOn("sheet", "close");
	// The back key: a stage back where there is one; the root and the travel (opened from its own button) close.
	if (sti(view.sheet.back)) UiBackTo("sheet-back");
	else UiBackTo("sheet-close");
}

void TouchHudLand_Build()
{
	aref ui = GetEventData();
	if (ui.screen != "land") return;
	DeleteAttribute(&TouchHudLand, "");
	UiState("gameplay", &TouchHudLand_PlayState);
	UiState("combat", &TouchHudLand_CombatState);
	UiState("vitals", &TouchHudLand_VitalsState);
	UiState("landCommands", &TouchHudLand_MenuState);
	aref view, play, combat, vitals, menu;
	makearef(view, TouchHudLand_View);
	makearef(play, TouchHudLand_PlayState);
	makearef(combat, TouchHudLand_CombatState);
	makearef(vitals, TouchHudLand_VitalsState);
	makearef(menu, TouchHudLand_MenuState);
	DeleteAttribute(view, "");
	bool melee = play.mode == "melee";
	bool ready = sti(combat.known) && sti(combat.current);
	view.layout = "land";
	if (melee) view.layout = "land-combat";
	bool analog = sti(play.analogMovement);
	if (analog) view.controls.move.kind = "stick";
	if (sti(vitals.known))
	{
		TouchHudLand_Effects(view, vitals);
		TouchHudLand_Party(view, vitals, melee);
	}
	bool menuKnown = sti(menu.known);
	if (menuKnown && sti(menu.active))
	{
		// The open menu blocks the game: the scene holds through it. One the sheet cannot show the runtime closes.
		view.holds = true;
		if (sti(menu.complete)) TouchHudLand_Sheet(view, menu);
		UiShow(view);
		return;
	}
	TouchHudLand_Location(view);
	view.log = true;
	view.elements.hero.disabled = melee;
	UiOn("hero", "click");
	view.elements.pause = "";
	UiOn("pause", "click");
	if (analog) view.elements.move = "";
	if (menuKnown && sti(menu.available)) TouchHudLand_MenuButtons(view, menu, melee);
	if (CheckAttribute(combat, "actions.weapon"))
	{
		view.elements.weapon.disabled = !ready;
		if (melee) view.elements.weapon.label = UiText("hud.weapon.sheathe");
		else view.elements.weapon.label = UiText("hud.weapon.draw");
		UiOn("weapon", "click");
	}
	TouchHudLand_Abilities(view, combat, vitals, ready, melee);
	if (melee) TouchHudLand_Combat(view, combat, vitals, ready);
	else TouchHudLand_Context(view, combat, ready);
	UiShow(view);
}

void TouchHudLand_Fight(string action, string phase)
{
	object fields = {action = "", phase = ""};
	fields.action = action;
	fields.phase = phase;
	UiCommand("combat", &fields);
}

void TouchHudLand_Step(string verb, string id)
{
	object fields = {menu = "land", verb = "", id = ""};
	fields.verb = verb;
	fields.id = id;
	UiCommand("menu", &fields);
}

void TouchHudLand_Tap()
{
	aref ui = GetEventData();
	if (ui.screen != "land") return;
	string id = GetEventData(), verb = GetEventData(), value = GetEventData();
	switch (id)
	{
		case "pause": TouchLib_Command("shortcut", "name", "mainmenu"); return;
		case "hero": TouchLib_Command("shortcut", "name", "character"); return;
		case "actions": TouchHudLand_Step("open", ""); return;
		case "travel": TouchHudLand_Step("open", TouchHudLand.travel); return;
		case "weapon": TouchHudLand_Fight("weapon", "tap"); return;
		case "shot": TouchHudLand_Fight("pistol", "tap"); return;
		case "dash": TouchHudLand_Fight("dash", "tap"); return;
		case "sheet-back": TouchHudLand_Step("back", ""); return;
		case "sheet-close": TouchHudLand_Step("close", ""); return;
		case "sheet": TouchHudLand_Step("close", ""); return;
		case "here": return;
	}
	if (id == "block")
	{
		// The press of a held button needs no action of its own.
		if (verb != "click") TouchHudLand_Fight("block", verb);
		return;
	}
	if (id == "context")
	{
		if (TouchHudLand.context == "") TouchHudLand_Fight("interact", "tap");
		else TouchLib_Command("context", "name", TouchHudLand.context);
		return;
	}
	if (id == "party")
	{
		// A tap on a portrait opens the character screen on that person.
		string k = "k" + UiAmount(value, "the tapped portrait");
		if (!CheckAttribute(&TouchHudLand, "party." + k)) return;
		object note = {character = "-1", position = "-1", tab = "", quest = ""};
		note.character = TouchHudLand.party.(k);
		UiPass("character", &note);
		TouchLib_Command("shortcut", "name", "characterscreen");
		return;
	}
	if (CheckAttribute(&TouchHudLand, "picks." + id))
	{
		// The potion drinks; an ability launches through the land menu's own command.
		string command = TouchHudLand.picks.(id);
		if (command == "") TouchHudLand_Fight("potion", "tap");
		else TouchHudLand_Step("open", command);
		return;
	}
	if (CheckAttribute(&TouchHudLand, "rows." + id))
	{
		// A row: drawn pressed until the game has answered its steps.
		TouchHudLand_Step("choose", id);
		TouchHudLand_View.sheet.rows.(TouchHudLand.rows.(id)).pressed = true;
		UiPredict(&TouchHudLand_View);
		return;
	}
	UiError("land HUD: a tap on '" + id + "' (" + verb + ") the scene did not offer");
}
