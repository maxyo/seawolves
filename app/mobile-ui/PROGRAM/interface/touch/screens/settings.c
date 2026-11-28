// SPDX-License-Identifier: GPL-3.0-only
// Settings: pages on a rail («Игра», «Управление», «Звук и графика», and «Отладка» where the application has it), each
// in columns of rows; a group of the game's options its window shows on another of its pages is one row that turns
// there. The game's own options (the adapter) apply as they change and are kept or dropped with «done» or
// the way back. The application's (the engine's table "appOptions": what each one is, its values and why it cannot
// be set) are a draft of this opening the engine keeps, handed to the phone with «done».

#event_handler("UiBuild", "TouchSettings_Build");
#event_handler("UiTap", "TouchSettings_Tap");

object TouchSettings;
object TouchSettings_App; // the engine's table of the last build
int TouchSettings_Page = 0;

// k of the entry of the application's group `id`, -1 for a group the engine does not offer.
int TouchSettings_Group(string id)
{
	return TouchLib_Find(&TouchSettings_App, "groups", "id", id);
}

aref TouchSettings_Column(aref columns, string title)
{
	aref column = TouchLib_Add(columns, "c");
	column.title = title;
	column.rows = "";
	return column;
}

aref TouchSettings_Row(aref column, string kind, string id, string key, string label)
{
	aref rows;
	makearef(rows, column.rows);
	aref row = TouchLib_Add(rows, "r");
	row.kind = kind;
	row.id = id;
	row.key = key;
	row.label = label;
	row.disabled = false;
	row.reason = "";
	return row;
}

// The game's group `group` of the adapter's state as rows; a group its window shows on another page, one row that
// turns to it.
void TouchSettings_GameRows(aref column, aref game, string group)
{
	aref row;
	if (CheckAttribute(game, "hidden." + group))
	{
		row = TouchSettings_Row(column, "action", "show-" + group, group, "");
		row.title = UiText("settings.show");
		row.selected = false;
		row.busy = false;
		DeleteAttribute(row, "key"); // a part of the generic layout draws it, whose rows have no key
		UiOn(row.id, "");
		return;
	}
	for (int k = 0; k < TouchLib_Count(game, "groups." + group); k++)
	{
		aref option = TouchLib_Item(game, "groups." + group, k);
		row = TouchSettings_Row(column, option.kind, option.key, option.key, option.label);
		row.disabled = !sti(option.can);
		row.reason = option.reason;
		if (option.kind == "step")
		{
			row.kind = "stepper";
			DeleteAttribute(row, "key");
			row.text = option.text;
			row.left = option.left;
			row.right = option.right;
			UiOn(option.key + "-left", "");
			UiOn(option.key + "-right", "");
			continue;
		}
		if (option.kind == "toggle") row.on = option.on;
		if (option.kind == "slider") row.value = option.value;
		if (option.kind == "segment")
		{
			TouchLib_Copy(row, "options", option, "options");
			row.selected = option.selected;
		}
		UiOn(option.key, "");
	}
}

// The application's group `id` as rows; a row named like its column («Графика» over the preset) shows no label.
void TouchSettings_AppRows(aref column, string id)
{
	int g = TouchSettings_Group(id);
	if (g < 0) return;
	aref group = TouchLib_Item(&TouchSettings_App, "groups", g);
	for (int k = 0; k < TouchLib_Count(group, "options"); k++)
	{
		aref option = TouchLib_Item(group, "options", k);
		string label = option.label;
		if (label == column.title) label = "";
		aref row = TouchSettings_Row(column, option.kind, "app-" + option.key, option.key, label);
		TouchLib_Copy(row, "options", option, "options");
		row.selected = option.selected;
		row.on = option.on;
		row.stack = option.stack;
		row.restart = option.restart;
		row.disabled = option.disabled;
		row.reason = option.reason;
		row.hint = option.hint;
		UiOn(row.id, "change");
	}
}

void TouchSettings_Cache(aref column)
{
	if (!CheckAttribute(&TouchSettings_App, "cache")) return;
	aref cache;
	makearef(cache, TouchSettings_App.cache);
	aref row = TouchSettings_Row(column, "button", "app-cache-clear", "cache", cache.label);
	row.hint = cache.hint;
	row.display = cache.display;
	row.disabled = cache.disabled;
	row.reason = cache.reason;
	UiOn("app-cache-clear", "");
}

void TouchSettings_Columns(aref game, aref columns)
{
	aref column;
	if (TouchSettings_Page == 0)
	{
		column = TouchSettings_Column(columns, UiText("settings.sea_and_map"));
		TouchSettings_AppRows(column, "map");
		TouchSettings_GameRows(column, game, "sea");
		TouchSettings_GameRows(TouchSettings_Column(columns, UiText("settings.markers")), game, "marks");
		return;
	}
	if (TouchSettings_Page == 1)
	{
		TouchSettings_GameRows(TouchSettings_Column(columns, UiText("settings.camera")), game, "camera");
		TouchSettings_AppRows(TouchSettings_Column(columns, UiText("settings.screen_touch")), "screen");
		return;
	}
	if (TouchSettings_Page == 2)
	{
		TouchSettings_GameRows(TouchSettings_Column(columns, UiText("settings.volume")), game, "sound");
		column = TouchSettings_Column(columns, UiText("settings.graphics"));
		TouchSettings_AppRows(column, "graphics");
		TouchSettings_GameRows(column, game, "graphics");
		TouchSettings_Cache(column);
		int textures = TouchSettings_Group("textures");
		if (textures < 0) return;
		aref group = TouchLib_Item(&TouchSettings_App, "groups", textures);
		TouchSettings_Row(column, "heading", "texture-processing", "textures", group.title);
		TouchSettings_AppRows(column, "textures");
		return;
	}
	aref debug = TouchLib_Item(&TouchSettings_App, "groups", TouchSettings_Group("debug"));
	TouchSettings_AppRows(TouchSettings_Column(columns, debug.title), "debug");
}

void TouchSettings_View(aref game)
{
	DeleteAttribute(&TouchSettings, "");
	TouchSettings.title = UiText("settings.title");
	object pages;
	pages.p0 = UiText("settings.tab_game");
	pages.p1 = UiText("settings.tab_controls");
	pages.p2 = UiText("settings.tab_sound_graphics");
	int debug = TouchSettings_Group("debug");
	if (debug >= 0)
	{
		aref group = TouchLib_Item(&TouchSettings_App, "groups", debug);
		pages.p3 = group.tab;
	}
	aref tabs;
	makearef(tabs, TouchSettings.tabs);
	for (int p = 0; p < GetAttributesNum(&pages); p++)
	{
		aref tab = TouchLib_Add(tabs, "t");
		tab.label = GetAttributeValue(GetAttributeN(&pages, p));
		tab.selected = p == TouchSettings_Page;
	}
	UiOn("tabs", "select");
	aref sections;
	makearef(sections, TouchSettings.sections);
	aref section = TouchLib_Add(sections, "s");
	section.title = "";
	aref columns;
	makearef(columns, section.columns);
	TouchSettings_Columns(game, columns);
	object done = {label = UiText("settings.done"), disabled = !sti(game.done.can), reason = game.done.reason,
	               busy = UiPending("done")};
	aref at;
	makearef(at, TouchSettings.done);
	CopyAttributes(at, &done);
	UiOn("done", "");
	UiOn("header-back", "");
}

void TouchSettings_Build()
{
	aref ui = GetEventData();
	if (ui.screen != "settings") return;
	if (UiFirst())
	{
		TouchSettings_Page = 0;
		TouchLib_Command("settings", "verb", "open");
	}
	object game;
	TouchAdpSettings_State(ui, &game);
	UiState("appOptions", &TouchSettings_App);
	TouchSettings_View(&game);
	UiShow(&TouchSettings);
}

// The application's option `key` set to its choice `value` (the tap's: k of a segment, 1 or 0 of a toggle).
void TouchSettings_Set(string key, string value)
{
	UiState("appOptions", &TouchSettings_App);
	for (int g = 0; g < TouchLib_Count(&TouchSettings_App, "groups"); g++)
	{
		aref group = TouchLib_Item(&TouchSettings_App, "groups", g);
		int k = TouchLib_Find(group, "options", "key", key);
		if (k < 0) continue;
		aref option = TouchLib_Item(group, "options", k);
		object setting = {key = key, value = 0};
		if (option.kind == "toggle") setting.value = value == "1";
		else
		{
			aref values;
			makearef(values, option.values);
			int chosen = TouchLib_TapIndex(value, GetAttributesNum(values), "the choice of " + key);
			setting.value = GetAttributeValue(GetAttributeN(values, chosen));
		}
		UiCommand("setting", &setting);
		return;
	}
	UiError("settings: the engine offers no setting " + key);
}

// The game's switch `key` drawn where the finger put it in the tap's frame (the game's answer confirms it).
void TouchSettings_Predict(string key, string field, string value)
{
	aref columns;
	makearef(columns, TouchSettings.sections.s0.columns);
	for (int c = 0; c < GetAttributesNum(columns); c++)
	{
		aref column = GetAttributeN(columns, c);
		int k = TouchLib_Find(column, "rows", "key", key);
		if (k < 0) continue;
		aref row = TouchLib_Item(column, "rows", k);
		row.(field) = value;
		UiPredict(&TouchSettings);
		return;
	}
}

void TouchSettings_Tap()
{
	aref ui = GetEventData();
	if (ui.screen != "settings") return;
	string id = GetEventData(), verb = GetEventData(), value = GetEventData();
	if (id == "tabs")
	{
		int pages = 3;
		if (TouchSettings_Group("debug") >= 0) pages = 4;
		TouchSettings_Page = TouchLib_TapIndex(value, pages, "the page");
		// A game that shows its options by pages turns to the first group of ours.
		object game;
		TouchAdpSettings_State(ui, &game);
		string first = "";
		if (TouchSettings_Page == 0) first = "sea";
		if (TouchSettings_Page == 1) first = "camera";
		if (TouchSettings_Page == 2) first = "sound";
		if (first != "" && CheckAttribute(&game, "hidden." + first)) TouchAdpSettings_Show(ui, first);
		else UiRebuild();
		return;
	}
	if (id == "header-back") { TouchAdpSettings_Leave(ui); return; }
	if (id == "done")
	{
		TouchLib_Command("settings", "verb", "keep");
		TouchAdpSettings_Keep(ui);
		return;
	}
	if (id == "app-cache-clear")
	{
		if (sti(TouchSettings_App.cache.clear)) TouchLib_Command("host", "action", "app-cache-clear");
		return;
	}
	if (TouchLib_StartsWith(id, "app-")) { TouchSettings_Set(TouchLib_Cut(id, 4, strlen(id)), value); return; }
	if (TouchLib_StartsWith(id, "show-")) { TouchAdpSettings_Show(ui, TouchLib_Cut(id, 5, strlen(id))); return; }
	// The game's options: a slider goes out where it is let go, not while it is dragged.
	object state;
	TouchAdpSettings_State(ui, &state);
	string kind = "";
	string key = id;
	bool right = TouchLib_EndsWith(id, "-right");
	if (right || TouchLib_EndsWith(id, "-left"))
	{
		key = TouchLib_Cut(id, 0, strlen(id) - 5);
		if (right) key = TouchLib_Cut(id, 0, strlen(id) - 6);
	}
	aref groups;
	makearef(groups, state.groups);
	for (int g = 0; g < GetAttributesNum(groups); g++)
	{
		string group = GetAttributeName(GetAttributeN(groups, g));
		int k = TouchLib_Find(groups, group, "key", key);
		if (k >= 0)
		{
			aref option = TouchLib_Item(groups, group, k);
			kind = option.kind;
		}
	}
	if (kind == "step") TouchAdpSettings_Step(ui, key, right);
	else if (kind == "toggle")
	{
		string on = "0";
		if (value == "1") on = "1";
		TouchSettings_Predict(id, "on", on);
		TouchAdpSettings_Toggle(ui, id, value == "1");
	}
	else if (kind == "segment")
	{
		int chosen = UiAmount(value, "the choice of " + id);
		TouchSettings_Predict(id, "selected", "" + chosen);
		TouchAdpSettings_Choose(ui, id, chosen);
	}
	else if (kind == "slider") { if (verb == "commit") TouchAdpSettings_Slide(ui, id, UiAmount(value, "the position of " + id)); }
	else UiError("settings: the game offers no option " + id);
}
