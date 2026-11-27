// SPDX-License-Identifier: GPL-3.0-only
// Crew hire from the interface hirecrew: the ships' scroll SHIPS_SCROLL, the two crews CREW_QTY / CREW_QTY2 with
// their tables, the amount window QTY_* (its edit takes the amount: ChangeQTY_EDIT clamps it and puts it on both
// crews at once, its OK confirms). Implements contracts/crew.c.

// The tavern's men are the colony's crew (hirecrew.c refTown: the colony of the location's fast reload).
ref TouchLegCrew_Town()
{
	int colony = -1;
	if (CheckAttribute(loadedLocation, "fastreload")) colony = FindColony(loadedLocation.fastreload);
	if (colony < 0) UiError("crew: the hire is open in a location of no colony");
	return GetColonyByIndex(colony);
}

// The ship's crew whose window is open, by its scroll row: what every amount is bound to.
string TouchLegCrew_Owner(aref model)
{
	for (int k = 0; k < TouchLib_Count(model, "SHIPS_SCROLL.rows"); k++)
	{
		aref row = TouchLib_Item(model, "SHIPS_SCROLL.rows", k);
		if (row.value == "1") return row.id;
	}
	UiError("crew: no ship of the scroll is the current one");
	return "";
}

// The ship's other lines: its upkeep and its food.
string TouchLegCrew_Notes[2] = {"MONEY_SHIP", "FOOD_SHIP"};

void TouchAdpCrew_State(aref ui, aref out)
{
	aref model = TouchLegLib_Model(ui);
	DeleteAttribute(out, "");
	out.title = TouchLegLib_Label(model, "TITLE");
	if (out.title == "") out.title = UiOneLine(TouchLegLib_Label(model, "STORECAPTION1"));
	aref ships;
	makearef(ships, out.ships);
	TouchLegLib_Choices(model, "SHIPS_SCROLL", ships);
	out.info = UiOneLine(TouchLegLib_Label(model, "INFO_SHIP"));
	out.aboard = UiAmount(model.CREW_QTY.label, "CREW_QTY");
	out.ashore = UiAmount(TouchLegLib_Label(model, "CREW_QTY2"), "CREW_QTY2");
	// SetBackupQty keeps both crews as they were before the amount set (the window's origin)
	if (!CheckAttribute(&NullCharacter, "TavernBak.Hero.Quantity") || !CheckAttribute(&NullCharacter, "TavernBak.Tavern.Quantity"))
		UiError("crew: the game kept no crews before the amount (NullCharacter.TavernBak)");
	out.base = sti(NullCharacter.TavernBak.Hero.Quantity);
	out.baseAshore = sti(NullCharacter.TavernBak.Tavern.Quantity);
	out.price = UiAmount(TouchLegLib_Label(model, "TAVERN_PRICE"), "TAVERN_PRICE");
	string result = TouchLegLib_Label(model, "QTY_Result");
	out.cost = 0;
	if (UiHasAmount(result)) out.cost = UiAmount(result, "QTY_Result");
	out.edit = UiOffers("QTY_EDIT", "edit");
	out.confirm = false;
	out.reason = "";
	if (CheckAttribute(model, "QTY_OK_BUTTON"))
	{
		out.confirm = UiOffers("QTY_OK_BUTTON", "press");
		out.reason = model.QTY_OK_BUTTON.reason;
	}
	object purse;
	out.purse = "";
	if (TouchLegLib_Purse(model, &purse)) out.purse = purse.value;
	out.notes = "";
	for (int i, string node: TouchLegCrew_Notes)
	{
		string note = UiOneLine(TouchLegLib_Label(model, node));
		if (note == "") continue;
		if (out.notes != "") out.notes = out.notes + " · ";
		out.notes = out.notes + note;
	}
	aref crew;
	makearef(crew, out.ship);
	TouchLegLib_Crew(model, "TABLE_CREW", "CREW_MORALE_TEXT", TouchLegLib_ScrollCaptain("SHIPS_SCROLL"), crew);
	makearef(crew, out.tavern);
	TouchLegLib_Crew(model, "TABLE_CREW2", "CREW_MORALE_TEXT2", TouchLegCrew_Town(), crew);
}

void TouchAdpCrew_Offer(aref ui, int amount)
{
	aref model = TouchLegLib_Model(ui);
	TouchLegLib_Offer(model, amount, TouchLegCrew_Owner(model));
}

void TouchAdpCrew_Commit(aref ui, int amount)
{
	aref model = TouchLegLib_Model(ui);
	TouchLegLib_Confirm(model, amount, TouchLegCrew_Owner(model));
}

void TouchAdpCrew_Ship(aref ui, int ship)
{
	TouchLegLib_Choose(TouchLegLib_Model(ui), "SHIPS_SCROLL", ship);
}

void TouchAdpCrew_Leave(aref ui)
{
	string leave = "";
	if (CheckAttribute(ui, "model.EXIT_BTN")) leave = "EXIT_BTN";
	UiBack(leave);
}
