// SPDX-License-Identifier: GPL-3.0-only
// Self repair from the interface repair: the parts' captions REPAIR_QTY_H / _M / _S ("75%", "2/3") with their arrows
// REPAIR_LEFT_* / REPAIR_RIGHT_*, the time TIME_STR, REPAIR_OK, REPAIR_ALL_SQUADRON and the ships' scroll. The order
// of the ship shown is the game's chr.repair (repair.c: what each part has set, the sails' maximum, the planks and
// sailcloth it takes); the arrows move it and repair.c ClickRepairArror stops them where the materials end.
// Implements contracts/repair.c.

string TouchLegRepair_Keys[3] = {"hull", "masts", "sails"};
string TouchLegRepair_Nodes[3] = {"H", "M", "S"};
string TouchLegRepair_Captions[3] = {"REPAIR_HULL_CAPTION", "REPAIR_MAST_CAPTION", "REPAIR_SAIL_CAPTION"};
string TouchLegRepair_Orders[3] = {"hull", "mast", "sail"};
string TouchLegRepair_Materials[3] = {"planks", "planks", "sailcloth"};

void TouchAdpRepair_State(aref ui, aref out)
{
	aref model = TouchLegLib_Model(ui);
	DeleteAttribute(out, "");
	aref ships;
	makearef(ships, out.ships);
	TouchLegLib_Choices(model, "SHIPS_SCROLL", ships);
	ref chr = TouchLegLib_ScrollCaptain("SHIPS_SCROLL");
	if (!CheckAttribute(chr, "repair.preq")) UiError("repair: the game keeps no order for the ship shown (chr.repair)");
	ref mc = GetMainCharacter();
	out.stock.planks = GetSquadronGoods(mc, FindGood("Planks"));
	out.stock.sailcloth = GetSquadronGoods(mc, FindGood("Sailcloth"));
	out.takes.planks = sti(chr.repair.preq);
	out.takes.sailcloth = sti(chr.repair.sreq);
	aref parts;
	makearef(parts, out.parts);
	// Part k is TouchLegRepair_Nodes[k]: both games' windows have all three captions and the time.
	for (int p = 0; p < 3; p++) TouchLegRepair_Part(model, chr, p, TouchLib_Add(parts, "p"));
	out.hours = UiAmount(TouchLegLib_Label(model, "TIME_STR"), "TIME_STR");
	out.confirm = UiOffers("REPAIR_OK", "press");
	out.reason = model.REPAIR_OK.reason;
	if (!CheckAttribute(model, "REPAIR_ALL_SQUADRON")) return;
	aref at;
	makearef(at, out.all);
	TouchLegLib_Button(model, "REPAIR_ALL_SQUADRON", at);
}

// Part p as its caption writes it with the order set: "75%" (of 100, the sails of the game's maximum) or "2/3" masts.
void TouchLegRepair_Part(aref model, ref chr, int p, aref at)
{
	string node = TouchLegRepair_Nodes[p];
	string order = TouchLegRepair_Orders[p];
	string qty = model.("REPAIR_QTY_" + node).label;
	at.key = TouchLegRepair_Keys[p];
	at.label = TouchLegLib_Label(model, TouchLegRepair_Captions[p]);
	at.pieces = node == "M";
	at.amount = UiAmount(qty, "REPAIR_QTY_" + node);
	at.state = sti(at.amount) - sti(chr.repair.(order));
	at.full = 100;
	if (node == "S") at.full = sti(chr.repair.sailmax);
	if (node == "M")
	{
		int slash = findSubStr(qty, "/", 0);
		if (slash < 0) UiError("repair: no whole in the masts' caption '" + qty + "'");
		at.full = UiAmount(TouchLib_Cut(qty, slash + 1, strlen(qty)), "the masts' whole");
	}
	at.can = CheckAttribute(model, "REPAIR_LEFT_" + node) && CheckAttribute(model, "REPAIR_RIGHT_" + node);
	at.material = TouchLegRepair_Materials[p];
}

// The arrows of a part move its order by one per press (repair.c ProcessCommandExecute: a click is ±1, the masts'
// all that fell), and the game stops them where the materials or the part end; the build shows where it stopped.
void TouchAdpRepair_Set(aref ui, int part, int amount)
{
	object state;
	TouchAdpRepair_State(ui, &state);
	aref at = TouchLib_Item(&state, "parts", part);
	int shown = sti(at.amount);
	string node = TouchLegRepair_Nodes[part];
	string arrow = "REPAIR_RIGHT_" + node;
	if (amount < shown) arrow = "REPAIR_LEFT_" + node;
	int presses = abs(amount - shown);
	if (sti(at.pieces)) presses = 1;
	for (int i = 0; i < presses; i++) UiSend(arrow, "press", "");
}

void TouchAdpRepair_Ship(aref ui, int ship)
{
	TouchLegLib_Choose(TouchLegLib_Model(ui), "SHIPS_SCROLL", ship);
}

void TouchAdpRepair_All(aref ui)
{
	UiSend("REPAIR_ALL_SQUADRON", "press", "");
}

void TouchAdpRepair_Do(aref ui)
{
	UiSend("REPAIR_OK", "press", "");
}

void TouchAdpRepair_Leave(aref ui)
{
	UiBack(TouchLegLib_Exit(TouchLegLib_Model(ui), "REPAIR_CANCEL EXIT_BTN"));
}
