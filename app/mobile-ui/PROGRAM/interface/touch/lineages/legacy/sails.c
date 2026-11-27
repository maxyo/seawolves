// SPDX-License-Identifier: GPL-3.0-only
// The sail emblem from the interface sailsgerald: the emblems' scroll SCROLL_GERALD, the game's two works B_OK (paint)
// and B_REMOVE (take off), of which SailsGerald.c CheckChangeSailStatus makes one selectable for the chosen emblem.
// The work's price is that window's global `price`; the emblem on the sails is the hero's ship's, as the window reads
// it (shref, GetRealShip of GetCharacterShipType). Implements contracts/sails.c.

void TouchAdpSails_State(aref ui, aref out)
{
	aref model = TouchLegLib_Model(ui);
	DeleteAttribute(out, "");
	out.title = TouchLegLib_Label(model, "INFO_CAPTION");
	out.price = UiGameInt("price");
	ref hero = GetMainCharacter();
	out.purse = sti(hero.Money);
	ref ship = GetRealShip(GetCharacterShipType(hero));
	string applied = "";
	if (CheckAttribute(ship, "ShipSails.Gerald_Name")) applied = ship.ShipSails.Gerald_Name;
	aref emblems;
	makearef(emblems, out.emblems);
	for (int k = 0; k < TouchLib_Count(model, "SCROLL_GERALD.rows"); k++)
	{
		aref row = TouchLib_Item(model, "SCROLL_GERALD.rows", k);
		object emblem = {key = row.id, label = row.label, texture = row.context.texture, selected = row.value == "1",
		                 applied = applied != "" && row.id == applied, can = CheckAttribute(row, "offers.select")};
		aref at = TouchLib_Add(emblems, "e");
		CopyAttributes(at, &emblem);
	}
	string node = TouchLegSails_Work(model);
	if (node == "") return;
	object work = {remove = node == "B_REMOVE", can = UiOffers(node, "press"), reason = model.(node).reason};
	aref to;
	makearef(to, out.work);
	CopyAttributes(to, &work);
}

// The game's button of the work it offers: B_REMOVE where it offers that and not B_OK, else B_OK; "" without both.
string TouchLegSails_Work(aref model)
{
	bool paint = false;
	if (CheckAttribute(model, "B_OK")) paint = UiOffers("B_OK", "press");
	if (CheckAttribute(model, "B_REMOVE") && !paint)
	{
		if (UiOffers("B_REMOVE", "press")) return "B_REMOVE";
	}
	if (CheckAttribute(model, "B_OK")) return "B_OK";
	return "";
}

void TouchAdpSails_Choose(aref ui, int emblem)
{
	TouchLegLib_Choose(TouchLegLib_Model(ui), "SCROLL_GERALD", emblem);
}

void TouchAdpSails_Work(aref ui)
{
	string node = TouchLegSails_Work(TouchLegLib_Model(ui));
	if (node == "") UiError("sail emblem: the window offers no work");
	UiSend(node, "press", "");
}

void TouchAdpSails_Leave(aref ui)
{
	UiBack(TouchLegLib_Exit(TouchLegLib_Model(ui), "EXIT_BTN B_CANCEL"));
}
