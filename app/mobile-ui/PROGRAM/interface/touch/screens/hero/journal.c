// SPDX-License-Identifier: GPL-3.0-only
// The journal, a hero section: a rail of four parts over the game's pages (quests: active and completed; documents;
// assets; statistics), the entries of a page with the open one's records on parchment, and the game's tables. The
// statistics are split in three of our tabs; which is shown is the screen's own.

#event_handler("UiBuild", "TouchJournal_Build");
#event_handler("UiTap", "TouchJournal_Tap");

object TouchJournal;        // the adapter's state of the last build (.game)
object TouchJournal_View;
object TouchJournal_Header; // the section tapped while the game opens it (TouchLib_Section)
object TouchJournal_Own;    // .stats: the statistics tab shown

// s: how long a move waits for the game's answer; past it the view shows what the game has.
float TouchJournal_Wait = 0.5;
string TouchJournal_Parts[4] = {"quests", "docs", "assets", "stats"};
string TouchJournal_Groups[3] = {"battles", "by_nation", "other"};

void TouchJournal_Count(aref game, aref at)
{
	for (int k = 0; k < TouchLib_Count(game, "pages"); k++)
	{
		aref page = TouchLib_Item(game, "pages", k);
		if (page.part == at.id)
		{
			if (CheckAttribute(page, "count")) at.count = page.count;
			return;
		}
	}
}

void TouchJournal_Rail(aref game, aref view)
{
	aref rail;
	makearef(rail, view.sections);
	for (int i, string part: TouchJournal_Parts)
	{
		aref at = TouchLib_Add(rail, "s");
		at.id = part;
		at.label = UiText("journal." + part);
		at.selected = part == game.part;
		// The quests count the entries of their first page, once the game has shown it.
		if (part == "quests") TouchJournal_Count(game, at);
		UiOn("section-" + part, "");
	}
}

void TouchJournal_Subtabs(aref game, aref view)
{
	aref tabs;
	makearef(tabs, view.subtabs);
	aref at;
	if (game.part == "stats")
	{
		for (int i, string group: TouchJournal_Groups)
		{
			at = TouchLib_Add(tabs, "t");
			at.id = group;
			at.label = UiText("journal." + group);
			at.selected = group == TouchJournal_Own.stats;
		}
		return;
	}
	if (game.part != "quests" && game.part != "assets") return;
	for (int k = 0; k < TouchLib_Count(game, "pages"); k++)
	{
		aref page = TouchLib_Item(game, "pages", k);
		if (page.part == game.part && page.label != "")
		{
			at = TouchLib_Add(tabs, "t");
			at.id = page.id;
			at.label = page.label;
			at.selected = page.selected;
		}
	}
}

void TouchJournal_Entries(aref game, aref view)
{
	view.empty = UiText("journal.no_quests");
	if (game.part == "docs") view.empty = UiText("journal.no_docs");
	aref list;
	makearef(list, view.list);
	for (int k = 0; k < TouchLib_Count(game, "entries"); k++)
	{
		aref entry = TouchLib_Item(game, "entries", k);
		aref row = TouchLib_Add(list, "r");
		row.cells.c0.text = entry.label;
		row.selected = entry.open;
	}
	view.entry = "";
	if (!CheckAttribute(game, "text")) return;
	view.entry.title = game.text.title;
	aref records;
	makearef(records, view.entry.entries);
	for (k = 0; k < TouchLib_Count(game, "text.records"); k++)
	{
		aref record = TouchLib_Item(game, "text.records", k);
		aref at = TouchLib_Add(records, "e");
		at.date = record.date;
		at.text = record.text;
	}
}

void TouchJournal_Tables(aref game, aref view)
{
	view.noTables = UiText("journal.nothing");
	aref tables;
	makearef(tables, view.tables);
	for (int k = 0; k < TouchLib_Count(game, "tables"); k++)
	{
		aref table = TouchLib_Item(game, "tables", k);
		bool shown = table.group == "";
		if (game.part == "stats") shown = table.group == TouchJournal_Own.stats;
		if (shown)
		{
			aref at = TouchLib_Add(tables, "t");
			at.id = "table-" + table.id;
			at.interactive = table.choose;
			int columns = TouchLib_Count(table, "columns");
			aref heads;
			makearef(heads, at.columns);
			for (int c = 0; c < columns; c++)
			{
				aref column = TouchLib_Item(table, "columns", c);
				aref head = TouchLib_Add(heads, "c");
				head.label = column.label;
				head.w = "1";
				head.align = "right";
				if (c == 0) { head.w = "2"; head.align = "left"; }
			}
			aref rows;
			makearef(rows, at.rows);
			int count = TouchLib_Count(table, "rows");
			for (int r = 0; r < count; r++)
			{
				aref from = TouchLib_Item(table, "rows", r);
				aref row = TouchLib_Add(rows, "r");
				CopyAttributes(row, from);
			}
			// The sheet scrolls, not each table: a table is as tall as its rows (head + a 44 dp row each).
			at.height = 56 + 44 * count;
			if (sti(table.choose)) UiOn(at.id, "row");
		}
	}
}

void TouchJournal_Build()
{
	aref ui = GetEventData();
	if (ui.screen != "journal") return;
	if (UiFirst()) TouchJournal_Own.stats = "battles";
	DeleteAttribute(&TouchJournal, "");
	aref game;
	makearef(game, TouchJournal.game);
	TouchAdpJournal_State(ui, game);
	// A quest's record tapped on the HUD opened the journal: its entry opens.
	object subject;
	if (UiTake("journal", &subject)) TouchAdpJournal_Find(ui, subject.quest);
	DeleteAttribute(&TouchJournal_View, "");
	aref view;
	makearef(view, TouchJournal_View);
	view.title = game.title;
	view.date = game.date;
	view.section = game.part;
	TouchLib_SectionHead(view, &TouchJournal_Header, "journal", sti(game.sections));
	TouchJournal_Rail(game, view);
	TouchJournal_Subtabs(game, view);
	UiOn("journal-tabs", "select");
	if (game.part == "quests" || game.part == "docs")
	{
		TouchJournal_Entries(game, view);
		UiOn("entries", "row");
	}
	if (game.part == "assets" || game.part == "stats") TouchJournal_Tables(game, view);
	UiOn("header-back", "");
	// An open text: the back key leaves the journal as ← does (the game's own back only closes the text).
	if (CheckAttribute(game, "text")) UiBackTo("header-back");
	UiShow(&TouchJournal_View);
}

// The first page of a part.
string TouchJournal_First(aref game, string part)
{
	for (int k = 0; k < TouchLib_Count(game, "pages"); k++)
	{
		aref page = TouchLib_Item(game, "pages", k);
		if (page.part == part) return page.id;
	}
	return "";
}

void TouchJournal_Tap()
{
	aref ui = GetEventData();
	if (ui.screen != "journal") return;
	string id = GetEventData(), verb = GetEventData(), value = GetEventData();
	aref game;
	makearef(game, TouchJournal.game);
	if (id == "header")
	{
		if (!TouchLib_SectionTap(&TouchJournal_Header, &TouchJournal_View, "journal", value)) return;
		TouchAdpJournal_Section(ui, value);
		UiHold(TouchJournal_Wait);
		return;
	}
	if (id == "header-back") { TouchAdpJournal_Leave(ui); UiHold(TouchJournal_Wait); return; }
	if (TouchLib_StartsWith(id, "section-"))
	{
		string part = TouchLib_Cut(id, 8, strlen(id));
		if (part == game.part) return;
		string first = TouchJournal_First(game, part);
		if (first == "") UiError("journal: the game has no page of " + part);
		TouchAdpJournal_Page(ui, first);
		UiHold(TouchJournal_Wait);
		return;
	}
	if (id == "journal-tabs")
	{
		if (game.part == "stats")
		{
			TouchJournal_Own.stats = value;
			UiRebuild();
			return;
		}
		TouchAdpJournal_Page(ui, value);
		UiHold(TouchJournal_Wait);
		return;
	}
	if (id == "entries")
	{
		TouchAdpJournal_Open(ui, TouchLib_TapIndex(value, TouchLib_Count(game, "entries"), "the entry"));
		UiHold(TouchJournal_Wait);
		return;
	}
	// A table's row: table-<node>.
	string table = TouchLib_Cut(id, 6, strlen(id));
	for (int k = 0; k < TouchLib_Count(game, "tables"); k++)
	{
		aref at = TouchLib_Item(game, "tables", k);
		if (at.id == table)
		{
			TouchAdpJournal_Choose(ui, table, TouchLib_TapIndex(value, TouchLib_Count(at, "rows"), "the row"));
			UiHold(TouchJournal_Wait);
			return;
		}
	}
	UiError("journal: no table " + table);
}
