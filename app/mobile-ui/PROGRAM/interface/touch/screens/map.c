// SPDX-License-Identifier: GPL-3.0-only
// A map of the game to look at closely: pan, pinch, the game's markers on it, the maps the hero carries on the left,
// and the colony the player pointed at in a sheet on the right, in pages of ours (what it trades, the trade
// assistant's profits, the town) instead of one scrolling window. The page shown is the screen's own.

#event_handler("UiBuild", "TouchMap_Build");
#event_handler("UiTap", "TouchMap_Tap");

object TouchMap;   // the view
object TouchMap_Sheet; // .colony (the colony the sheet shows), .page (the page of it the player chose)

bool TouchMap_Has(aref info, string page)
{
	for (int k = 0; k < TouchLib_Count(info, "sections"); k++)
	{
		aref section = TouchLib_Item(info, "sections", k);
		if (section.page == page) return true;
	}
	return false;
}

void TouchMap_Tab(aref tabs, string id, string key, bool there)
{
	if (!there) return;
	aref tab = TouchLib_Add(tabs, "t");
	tab.id = id;
	tab.label = UiText(key);
	tab.selected = false;
}

// The colony sheet: a new colony opens on the first page it has; a page with nothing on it is not offered.
void TouchMap_Info(aref colony)
{
	aref info;
	makearef(info, TouchMap.info);
	CopyAttributes(info, colony);
	if (TouchMap_Sheet.colony != colony.title)
	{
		TouchMap_Sheet.colony = colony.title;
		TouchMap_Sheet.page = "";
	}
	object tabs;
	TouchMap_Tab(&tabs, "trade", "colony.trade", TouchMap_Has(info, "trade"));
	TouchMap_Tab(&tabs, "assistant", "colony.assistant", TouchMap_Has(info, "assistant"));
	TouchMap_Tab(&tabs, "city", "colony.city", TouchLib_Count(info, "facts") + TouchLib_Count(info, "notes") > 0);
	info.page = "trade";
	int k;
	aref tab;
	if (GetAttributesNum(&tabs) > 0)
	{
		tab = GetAttributeN(&tabs, 0);
		info.page = tab.id;
	}
	for (k = 0; k < GetAttributesNum(&tabs); k++)
	{
		tab = GetAttributeN(&tabs, k);
		if (tab.id == TouchMap_Sheet.page) info.page = TouchMap_Sheet.page;
	}
	aref shown;
	makearef(shown, info.tabs);
	if (GetAttributesNum(&tabs) < 2) return;
	for (k = 0; k < GetAttributesNum(&tabs); k++)
	{
		tab = GetAttributeN(&tabs, k);
		tab.selected = tab.id == info.page;
	}
	CopyAttributes(shown, &tabs);
	UiOn("info-tabs", "select");
}

void TouchMap_Build()
{
	aref ui = GetEventData();
	if (ui.screen != "map") return;
	if (UiFirst()) DeleteAttribute(&TouchMap_Sheet, "");
	if (!CheckAttribute(&TouchMap_Sheet, "colony")) TouchMap_Sheet.colony = "";
	object game;
	TouchAdpMap_State(ui, &game);
	DeleteAttribute(&TouchMap, "");
	TouchMap.title = game.title;
	if (game.title == "") TouchMap.title = UiText("map.title");
	TouchLib_Copy(&TouchMap, "map", &game, "map");
	aref map;
	makearef(map, TouchMap.map);
	TouchLib_Copy(map, "markers", &game, "markers");
	TouchMap.mapsTitle = game.mapsTitle;
	TouchMap.mapColumns.c0.w = "1";
	aref rows;
	makearef(rows, TouchMap.maps);
	for (int k = 0; k < TouchLib_Count(&game, "maps"); k++)
	{
		aref listed = TouchLib_Item(&game, "maps", k);
		aref row = TouchLib_Add(rows, "r");
		row.id = listed.key;
		row.cells.c0.text = listed.label;
		row.cells.c0.icon = "map";
		row.selected = listed.selected;
		row.disabled = !sti(listed.can);
	}
	UiOn("map", "point");
	UiOn("maps", "row");
	UiOn("header-back", "");
	UiOn("info-close", "");
	if (CheckAttribute(&game, "colony"))
	{
		aref colony;
		makearef(colony, game.colony);
		TouchMap_Info(colony);
		// The open colony takes the back key: it lets go of the colony as its cross does.
		UiBackTo("info-close");
	}
	else TouchMap_Sheet.colony = "";
	UiShow(&TouchMap);
}

void TouchMap_Tap()
{
	aref ui = GetEventData();
	if (ui.screen != "map") return;
	string id = GetEventData(), verb = GetEventData(), value = GetEventData();
	if (id == "map") { TouchAdpMap_Point(ui, value); return; }
	if (id == "info-close") { TouchAdpMap_Release(ui); return; }
	if (id == "header-back") { TouchAdpMap_Leave(ui); return; }
	if (id == "info-tabs")
	{
		TouchMap_Sheet.page = value;
		UiRebuild();
		return;
	}
	object game;
	TouchAdpMap_State(ui, &game);
	int map = TouchLib_Find(&game, "maps", "key", value);
	if (map < 0) return; // the map tapped is no longer carried
	TouchAdpMap_Choose(ui, map);
}
