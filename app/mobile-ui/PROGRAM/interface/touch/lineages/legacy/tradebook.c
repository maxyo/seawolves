// SPDX-License-Identifier: GPL-3.0-only
// The trade book from the interface tradebook. Implements contracts/tradebook.c. TABLE_CITY: the nation's picture,
// the town, the island, "hour date" of its prices (no digits: none); TABLE_GOODS: the goods with its picture, the
// trade type's picture, sell, buy, stock.
// data: types.<picture> (a trade type picture of the game) = its tag.

void TouchAdpTradebook_State(aref ui, aref out)
{
	aref model = TouchLegLib_Model(ui);
	out.title = TouchLib_Trim(UiOneLine(TouchLegLib_Label(model, "TITLE")));
	out.sections = TouchLegLib_Sections(model);
	// Before the player picks a town the game shows the first one's prices without marking its row.
	int shown = TouchLegLib_Selected(model, "TABLE_CITY");
	if (shown < 0) shown = 0;
	int count = TouchLib_Count(model, "TABLE_CITY.rows");
	int k;
	aref row;
	aref places;
	makearef(places, out.places);
	for (k = 0; k < count; k++)
	{
		row = TouchLib_Item(model, "TABLE_CITY.rows", k);
		aref place = TouchLib_Add(places, "p");
		place.key = row.id;
		place.label = TouchLegLib_Cell(row, 1);
		place.sub = TouchLegLib_Cell(row, 2);
		place.flag = "";
		aref flag;
		if (CheckAttribute(row, "cells.c0.images.i0"))
		{
			makearef(flag, row.cells.c0.images.i0);
			place.flag = TouchLegLib_FlagOf(flag);
		}
		object when;
		DeleteAttribute(&when, "");
		TouchLib_Words(TouchLegLib_Cell(row, 3), &when);
		place.date = "";
		place.hour = "";
		int words = GetAttributesNum(&when);
		if (words > 0)
		{
			aref last = GetAttributeN(&when, words - 1);
			if (TouchLib_FindDigit(last.text, 0, true) >= 0) place.date = last.text;
		}
		if (words > 1 && place.date != "")
		{
			aref first = GetAttributeN(&when, 0);
			place.hour = first.text;
		}
		place.shown = k == shown;
		place.can = sti(row.enabled);
	}
	out.columns.sell = "";
	out.columns.buy = "";
	out.columns.stock = "";
	if (CheckAttribute(model, "TABLE_GOODS.headers.c4"))
	{
		out.columns.sell = model.TABLE_GOODS.headers.c2;
		out.columns.buy = model.TABLE_GOODS.headers.c3;
		out.columns.stock = model.TABLE_GOODS.headers.c4;
	}
	aref goods;
	makearef(goods, out.goods);
	for (k = 0; k < TouchLib_Count(model, "TABLE_GOODS.rows"); k++)
	{
		row = TouchLib_Item(model, "TABLE_GOODS.rows", k);
		aref at = TouchLib_Add(goods, "g");
		at.label = TouchLegLib_Cell(row, 0);
		TouchLib_Copy(at, "image", row, "cells.c0.images.i0");
		at.tag = "";
		if (CheckAttribute(row, "cells.c1.images.i0"))
		{
			string type = row.cells.c1.images.i0.image;
			if (CheckAttribute(ui, "data.types." + type)) at.tag = ui.data.types.(type);
		}
		at.sell = TouchLegLib_Cell(row, 2);
		at.buy = TouchLegLib_Cell(row, 3);
		at.stock = TouchLegLib_Cell(row, 4);
	}
}

void TouchAdpTradebook_Choose(aref ui, int place)
{
	TouchLegLib_Choose(TouchLegLib_Model(ui), "TABLE_CITY", place);
}

void TouchAdpTradebook_Section(aref ui, string section)
{
	TouchLegLib_Section(TouchLegLib_Model(ui), section);
}

void TouchAdpTradebook_Leave(aref ui)
{
	UiBack(TouchLegLib_Exit(TouchLegLib_Model(ui), "EXIT_BTN"));
}
