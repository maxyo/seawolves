// SPDX-License-Identifier: GPL-3.0-only
// Self repair: hull, masts and sails as amount rows from what each part is now to the whole, the ship selector in the
// header, the planks and sailcloth in stock and the repair button with its time and the materials the order takes
// (pack screen s-repair). A part's amount is the game's order: a move asks the game for it and the row shows where
// the game stopped.

#event_handler("UiBuild", "TouchRepair_Build");
#event_handler("UiTap", "TouchRepair_Tap");

// The adapter's state of the last build (the taps act on what it showed) and the view.
object TouchRepair;
object TouchRepair_View;

// s: how long a move waits for the game's answer; past it the view shows what the game has.
float TouchRepair_Wait = 0.5;

void TouchRepair_Build()
{
	aref ui = GetEventData();
	if (ui.screen != "repair") return;
	aref game;
	makearef(game, TouchRepair.game);
	TouchAdpRepair_State(ui, game);
	aref view;
	makearef(view, TouchRepair_View);
	DeleteAttribute(view, "");
	view.title = UiText("repair.title");
	aref parts;
	makearef(parts, view.parts);
	for (int k = 0; k < TouchLib_Count(game, "parts"); k++)
		TouchRepair_Part(game, TouchLib_Item(game, "parts", k), TouchLib_Add(parts, "p"));
	if (GetAttributesNum(parts) == 0) view.parts = "";
	aref stock;
	makearef(stock, view.stock);
	TouchRepair_Stock(stock, "planks", "repair.planks", game.stock.planks);
	TouchRepair_Stock(stock, "sailcloth", "repair.sailcloth", game.stock.sailcloth);
	TouchRepair_Subject(game, view);
	view.actions = "";
	if (CheckAttribute(game, "all"))
	{
		aref actions, all;
		makearef(actions, view.actions);
		makearef(all, game.all);
		TouchLib_Button(actions, "all-ships", UiText("repair.all_ships"), "quiet", all);
	}
	TouchRepair_Primary(game, view);
	UiOn("header-back", "");
	UiShow(view);
}

// A part's row: the whole 0…full with the knob at the game's order and a floor at what the part is now; the game
// stops the arrows where the materials end. A part to repair without its material in stock says so.
void TouchRepair_Part(aref game, aref part, aref at)
{
	int full = sti(part.full);
	int state = sti(part.state);
	bool pieces = sti(part.pieces);
	bool whole = state >= full;
	string material = part.material;
	bool none = !whole && sti(game.stock.(material)) == 0;
	at.id = part.key;
	at.icon = "hull";
	if (part.key == "masts") at.icon = "sail-furled";
	if (part.key == "sails") at.icon = "sail-full";
	at.label = part.label;
	at.now = UiText("repair.now") + " " + state + "%";
	if (pieces) at.now = UiText("repair.now") + " " + state + " " + UiText("common.of") + " " + full;
	object q = {value = part.amount, min = 0, max = full, floor = state, unit = "%", summary = "-", all = true,
	            allLabel = UiText("common.all"), disabled = whole || !sti(part.can)};
	if (pieces)
	{
		q.unit = "";
		q.summary = UiText("common.of") + " " + full;
	}
	q.steps.s0 = 1;
	q.steps.s1 = 10;
	aref to;
	makearef(to, at.q);
	CopyAttributes(to, &q);
	at.need = "";
	if (none) at.need = UiText("repair.no_" + material);
	at.short = none;
	at.disabled = whole;
	UiOn(part.key, "change");
	UiOn(part.key, "commit");
}

int TouchRepair_Index(aref game, string key)
{
	for (int k = 0; k < TouchLib_Count(game, "parts"); k++)
	{
		aref part = TouchLib_Item(game, "parts", k);
		if (part.key == key) return k;
	}
	return -1;
}

void TouchRepair_Stock(aref stock, string icon, string label, string value)
{
	aref at = TouchLib_Add(stock, "s");
	at.icon = icon;
	at.label = UiText(label);
	at.value = value;
}

// The ship being repaired, with arrows to the squadron's others.
void TouchRepair_Subject(aref game, aref view)
{
	int ships = TouchLib_Count(game, "ships");
	view.subject = "";
	if (ships <= 1) return;
	int current = TouchRepair_Current(game);
	aref ship = TouchLib_Item(game, "ships", current);
	object subject = {label = ship.label, sub = (current + 1) + " " + UiText("common.of") + " " + ships,
	                  prev = current > 0, next = current + 1 < ships, list = false};
	aref at;
	makearef(at, view.subject);
	CopyAttributes(at, &subject);
	UiOn("header", "subject-prev");
	UiOn("header", "subject-next");
}

int TouchRepair_Current(aref game)
{
	int k = TouchLib_Selected(game, "ships");
	if (k < 0) return 0;
	return k;
}

// «Починить» with the game's time of the order and the materials it takes; nothing ordered, nothing to do.
void TouchRepair_Primary(aref game, aref view)
{
	int hours = sti(game.hours);
	string sub = "";
	if (hours > 0) sub = hours + " " + UiText("save.hours");
	sub = TouchRepair_Takes(sub, "repair.planks", sti(game.takes.planks));
	sub = TouchRepair_Takes(sub, "repair.sailcloth", sti(game.takes.sailcloth));
	object primary = {label = UiText("repair.do"), sub = sub, disabled = !sti(game.confirm) || hours <= 0,
	                  reason = game.reason, busy = UiPending("primary")};
	aref at;
	makearef(at, view.primary);
	CopyAttributes(at, &primary);
	UiOn("primary", "");
}

string TouchRepair_Takes(string sub, string word, int takes)
{
	if (takes <= 0) return sub;
	if (sub != "") sub += " · ";
	return sub + UiText(word) + " " + takes;
}

void TouchRepair_Tap()
{
	aref ui = GetEventData();
	if (ui.screen != "repair") return;
	string id = GetEventData(), verb = GetEventData(), value = GetEventData();
	aref game;
	makearef(game, TouchRepair.game);
	if (id == "header-back") { TouchAdpRepair_Leave(ui); return; }
	if (id == "primary") { TouchAdpRepair_Do(ui); UiHold(TouchRepair_Wait); return; }
	if (id == "all-ships") { TouchAdpRepair_All(ui); UiHold(TouchRepair_Wait); return; }
	if (id == "header")
	{
		int ship = TouchRepair_Current(game) + 1;
		if (verb == "subject-prev") ship = TouchRepair_Current(game) - 1;
		TouchAdpRepair_Ship(ui, TouchLib_TapIndex("" + ship, TouchLib_Count(game, "ships"), "the ship"));
		UiHold(TouchRepair_Wait);
		return;
	}
	int part = TouchRepair_Index(game, id);
	if (part < 0) UiError("repair: no tap of " + id);
	int amount = UiAmount(value, "the part's amount");
	aref at = TouchLib_Item(game, "parts", part);
	if (amount == sti(at.amount)) { UiRebuild(); return; }
	TouchAdpRepair_Set(ui, part, amount);
	UiHold(TouchRepair_Wait);
}
