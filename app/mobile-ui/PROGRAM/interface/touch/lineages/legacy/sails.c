// SPDX-License-Identifier: GPL-3.0-only
// The sail emblem from the interface sailsgerald: the emblems' scroll SCROLL_GERALD, the game's two works B_OK (paint)
// and B_REMOVE (take off), of which SailsGerald.c CheckChangeSailStatus makes one selectable for the chosen emblem,
// and the engine's ANDROID_SAILS_GERALD with what the window's script keeps to itself (its price, the money, the
// emblem on the sails). Implements contracts/sails.c.

void TouchAdpSails_State(aref ui, aref out)
{
	aref model = TouchLegLib_Model(ui);
	DeleteAttribute(out, "");
	// the engine's, so no node of the game's window: the binding cannot require it
	if (!CheckAttribute(model, "ANDROID_SAILS_GERALD")) UiError("sail emblem: no ANDROID_SAILS_GERALD from the engine");
	aref marker;
	makearef(marker, model.ANDROID_SAILS_GERALD);
	if (marker.reason != "") UiError("sail emblem: " + marker.reason);
	out.title = TouchLegLib_Label(model, "INFO_CAPTION");
	out.price = sti(marker.context.price);
	out.purse = sti(marker.context.money);
	string applied = marker.context.applied;
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
	string exit = "";
	if (CheckAttribute(ui, "model.B_CANCEL")) exit = "B_CANCEL";
	if (CheckAttribute(ui, "model.EXIT_BTN")) exit = "EXIT_BTN";
	UiBack(exit);
}
