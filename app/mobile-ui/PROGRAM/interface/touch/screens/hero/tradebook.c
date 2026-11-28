// SPDX-License-Identifier: GPL-3.0-only
// The trade book, a hero section: the places whose prices the hero has seen on the left (flag, town, island, the
// date of the prices) and the price list of the one shown on the right (goods with its trade type, sell, buy, stock).

#event_handler("UiBuild", "TouchTradebook_Build");
#event_handler("UiTap", "TouchTradebook_Tap");

object TouchTradebook;      // the adapter's state of the last build (.game)
object TouchTradebook_View;
object TouchTradebook_Header; // the section tapped while the game opens it (TouchLib_Section)

// s: how long a move waits for the game's answer; past it the view shows what the game has.
float TouchTradebook_Wait = 0.5;

void TouchTradebook_Places(aref game, aref view)
{
	view.townColumns.c0.w = "1";
	view.townColumns.c1.w = "112px";
	view.townColumns.c1.align = "right";
	aref towns;
	makearef(towns, view.towns);
	for (int k = 0; k < TouchLib_Count(game, "places"); k++)
	{
		aref place = TouchLib_Item(game, "places", k);
		aref row = TouchLib_Add(towns, "r");
		row.cells.c0.text = place.label;
		row.cells.c0.sub = place.sub;
		if (place.flag != "")
		{
			row.cells.c0.icon = place.flag;
			row.cells.c0.iconTone = "color";
			row.cells.c0.iconSize = 30;
		}
		row.cells.c1.text = place.date;
		row.cells.c1.tone = "";
		if (place.date == "")
		{
			row.cells.c1.text = UiText("tradebook.no_data");
			row.cells.c1.tone = "dim";
		}
		if (place.hour != "") row.cells.c1.sub = place.hour;
		row.selected = place.shown;
		row.disabled = !sti(place.can);
	}
}

void TouchTradebook_Column(aref view, string slot, string label)
{
	view.columns.(slot).label = label;
	view.columns.(slot).w = "72px";
	view.columns.(slot).align = "right";
}

void TouchTradebook_Prices(aref game, aref view)
{
	string shown = "";
	for (int k = 0; k < TouchLib_Count(game, "places"); k++)
	{
		aref place = TouchLib_Item(game, "places", k);
		if (sti(place.shown)) shown = place.label;
	}
	view.columns.c0.label = shown;
	view.columns.c0.w = "1";
	TouchTradebook_Column(view, "c1", game.columns.sell);
	TouchTradebook_Column(view, "c2", game.columns.buy);
	TouchTradebook_Column(view, "c3", game.columns.stock);
	aref prices;
	makearef(prices, view.prices);
	for (int n = 0; n < TouchLib_Count(game, "goods"); n++)
	{
		aref goods = TouchLib_Item(game, "goods", n);
		aref row = TouchLib_Add(prices, "r");
		row.cells.c0.text = goods.label;
		TouchLib_Copy(row, "cells.c0.image", goods, "image");
		if (goods.tag != "") row.cells.c0.tag = goods.tag;
		row.cells.c1 = goods.sell;
		row.cells.c2 = goods.buy;
		row.cells.c3 = goods.stock;
	}
}

void TouchTradebook_Build()
{
	aref ui = GetEventData();
	if (ui.screen != "tradebook") return;
	DeleteAttribute(&TouchTradebook, "");
	aref game;
	makearef(game, TouchTradebook.game);
	TouchAdpTradebook_State(ui, game);
	DeleteAttribute(&TouchTradebook_View, "");
	aref view;
	makearef(view, TouchTradebook_View);
	view.title = game.title;
	TouchLib_SectionHead(view, &TouchTradebook_Header, "tradebook", sti(game.sections));
	TouchTradebook_Places(game, view);
	TouchTradebook_Prices(game, view);
	UiOn("towns", "row");
	UiOn("header-back", "");
	UiShow(&TouchTradebook_View);
}

void TouchTradebook_Tap()
{
	aref ui = GetEventData();
	if (ui.screen != "tradebook") return;
	string id = GetEventData(), verb = GetEventData(), value = GetEventData();
	if (id == "header")
	{
		if (!TouchLib_SectionTap(&TouchTradebook_Header, &TouchTradebook_View, "tradebook", value)) return;
		TouchAdpTradebook_Section(ui, value);
		UiHold(TouchTradebook_Wait);
		return;
	}
	if (id == "header-back") { TouchAdpTradebook_Leave(ui); return; }
	aref game;
	makearef(game, TouchTradebook.game);
	TouchAdpTradebook_Choose(ui, TouchLib_TapIndex(value, TouchLib_Count(game, "places"), "the town"));
	UiHold(TouchTradebook_Wait);
}
