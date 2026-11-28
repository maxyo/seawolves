// SPDX-License-Identifier: GPL-3.0-only
// The journal from the interface questbook. Implements contracts/journal.c. The game's window shows either the
// titles of its page (QUEST_TITLE) or one entry's text (QUEST_TEXT), never both: the adapter keeps the titles it last
// saw of each page, so the list stays while a text is open. Another entry is opened from the titles: its back (which
// the window's ProcessCancelExit turns into "back to the titles") first, then the title.
// data: pages.p<k>.{node (the game's tab), part, label (our string key, "" the game's TABSTR_ caption), entries (its
// titles are entries)}; tables.t<k>.{node, group, choose}.

// The walk: asked (the entry to open, as the game names it), looked.<tab> (pages that did not hold it), pending (the
// entry whose open was sent), open (the entry shown), leave (the player's back: the text closes, then the journal);
// titles.<tab>.e<k>.{id, label}.
object TouchLegJournal;

string TouchLegJournal_Active(aref ui, aref model)
{
	aref pages;
	makearef(pages, ui.data.pages);
	for (int k = 0; k < GetAttributesNum(pages); k++)
	{
		aref page = GetAttributeN(pages, k);
		string node = page.node;
		if (CheckAttribute(model, node)) { if (model.(node).value == "1") return node; }
	}
	return "";
}

// A date line of the game's entry texts ("1 October 1654 11:14"): a digit first, a four-digit year, a time.
bool TouchLegJournal_DateLine(string line)
{
	if (strlen(line) == 0 || strlen(line) > 48 || !TouchLib_Digit(line, 0)) return false;
	if (findSubStr(line, ":", 0) < 0) return false;
	int run = 0;
	for (int i = 0; i < strlen(line); i++)
	{
		run = run + 1;
		if (!TouchLib_Digit(line, i)) run = 0;
		if (run >= 4) return true;
	}
	return false;
}

// The game's text of an entry as records {date, text}, one per date line.
void TouchLegJournal_Records(string text, aref out)
{
	string newline = TouchLib_Newline();
	aref record;
	int from = 0;
	while (from <= strlen(text))
	{
		int end = findSubStr(text, newline, from);
		if (end < 0) end = strlen(text);
		string line = TouchLib_Trim(TouchLib_Cut(text, from, end));
		from = end + 1;
		if (line != "")
		{
			bool date = TouchLegJournal_DateLine(line);
			if (date || GetAttributesNum(out) == 0)
			{
				record = TouchLib_Add(out, "r");
				record.date = "";
				record.text = "";
				if (date) record.date = line;
				else record.text = line;
			}
			else
			{
				record = GetAttributeN(out, GetAttributesNum(out) - 1);
				if (record.text != "") record.text = record.text + newline;
				record.text = record.text + line;
			}
		}
	}
}

// Sends a step of the walk and waits for its answer; the first build of an opening is still drawn.
void TouchLegJournal_Waits()
{
	UiHold();
	if (!UiFirst()) UiAwait();
}

// The next step towards the asked entry: the text closed, the title opened, or the next page of entries looked at.
// True while the walk goes on.
bool TouchLegJournal_Step(aref ui, aref model, string active, bool text)
{
	if (sti(TouchLegJournal.leave))
	{
		if (text) return true;
		TouchLegJournal.leave = false;
		UiBack("");
		return true;
	}
	string answer = UiAnswer();
	if (TouchLegJournal.pending != "" && !text)
	{
		if (answer == "refused" || answer == "late") UiError("journal: the game did not open the entry " + TouchLegJournal.pending);
		if (answer == "applied") UiAwait();
		return true;
	}
	string asked = TouchLegJournal.asked;
	if (asked == "") return false;
	if (answer == "waiting") return true;
	if (answer == "refused" || answer == "late") UiError("journal: the game did not answer a step towards " + asked);
	if (text)
	{
		UiBack("");
		TouchLegJournal_Waits();
		return true;
	}
	int k;
	for (k = 0; k < TouchLib_Count(model, "QUEST_TITLE.rows"); k++)
	{
		aref row = TouchLib_Item(model, "QUEST_TITLE.rows", k);
		if (row.id == asked)
		{
			object action = {node = "QUEST_TITLE", row = row.index, verb = "open", value = row.id};
			UiSendTo(&action);
			TouchLegJournal.pending = row.id;
			TouchLegJournal.asked = "";
			TouchLegJournal_Waits();
			return true;
		}
	}
	// A quest's record names its quest, not the page: the record that closes a quest moves it to the completed ones,
	// a document's is on the documents. Look there.
	if (active != "") TouchLegJournal.looked.(active) = true;
	aref pages;
	makearef(pages, ui.data.pages);
	for (k = 0; k < GetAttributesNum(pages); k++)
	{
		aref page = GetAttributeN(pages, k);
		string node = page.node;
		if (sti(page.entries) && !CheckAttribute(&TouchLegJournal, "looked." + node) && CheckAttribute(model, node))
		{
			if (UiOffers(node, "press"))
			{
				UiSend(node, "press", "");
				TouchLegJournal_Waits();
				return true;
			}
		}
	}
	UiError("journal: the entry " + asked + " is on none of the pages of entries");
	return false;
}

void TouchLegJournal_Titles(aref model, string active)
{
	DeleteAttribute(&TouchLegJournal, "titles." + active);
	TouchLegJournal.titles.(active) = "";
	aref titles;
	makearef(titles, TouchLegJournal.titles.(active));
	for (int k = 0; k < TouchLib_Count(model, "QUEST_TITLE.rows"); k++)
	{
		aref row = TouchLib_Item(model, "QUEST_TITLE.rows", k);
		aref at = TouchLib_Add(titles, "e");
		at.id = row.id;
		at.label = TouchLib_Trim(row.label);
	}
}

void TouchLegJournal_Table(aref ui, aref model, aref spec, aref tables)
{
	string node = spec.node;
	int count = TouchLib_Count(model, node + ".rows");
	if (count == 0) return;
	aref table = TouchLib_Add(tables, "t");
	table.id = node;
	table.group = spec.group;
	table.choose = sti(spec.choose);
	aref first = TouchLib_Item(model, node + ".rows", 0);
	aref cells;
	makearef(cells, first.cells);
	int columns = GetAttributesNum(cells);
	int c;
	string cell;
	for (c = 0; c < columns; c++)
	{
		cell = "c" + c;
		table.columns.(cell).label = "";
		if (CheckAttribute(model, node + ".headers." + cell)) table.columns.(cell).label = TouchLib_Trim(model.(node).headers.(cell));
	}
	aref rows;
	makearef(rows, table.rows);
	for (int k = 0; k < count; k++)
	{
		aref row = TouchLib_Item(model, node + ".rows", k);
		aref at = TouchLib_Add(rows, "r");
		at.selected = sti(spec.choose) && row.value == "1";
		for (c = 0; c < columns; c++)
		{
			cell = "c" + c;
			at.cells.(cell).text = TouchLib_Trim(TouchLegLib_Cell(row, c));
			TouchLib_Copy(at, "cells." + cell + ".image", row, "cells." + cell + ".images.i0");
		}
	}
}

void TouchAdpJournal_State(aref ui, aref out)
{
	aref model = TouchLegLib_Model(ui);
	if (UiFirst())
	{
		DeleteAttribute(&TouchLegJournal, "");
		TouchLegJournal.asked = "";
		TouchLegJournal.pending = "";
		TouchLegJournal.open = "";
		TouchLegJournal.leave = false;
	}
	string active = TouchLegJournal_Active(ui, model);
	bool text = CheckAttribute(model, "QUEST_TEXT");
	if (!text)
	{
		if (active != "") TouchLegJournal_Titles(model, active);
		TouchLegJournal.open = "";
	}
	if (text && TouchLegJournal.pending != "")
	{
		TouchLegJournal.open = TouchLegJournal.pending;
		TouchLegJournal.pending = "";
	}
	TouchLegJournal_Step(ui, model, active, text);
	out.title = TouchLib_Trim(TouchLegLib_Label(model, "TITLE"));
	out.date = TouchLib_Trim(TouchLegLib_Label(model, "CURRENT_DATE_CAPTION"));
	out.sections = TouchLegLib_Sections(model);
	out.part = "";
	aref pages, page, at;
	makearef(pages, ui.data.pages);
	aref shown;
	makearef(shown, out.pages);
	int k;
	string node;
	for (k = 0; k < GetAttributesNum(pages); k++)
	{
		page = GetAttributeN(pages, k);
		node = page.node;
		if (CheckAttribute(model, node))
		{
			at = TouchLib_Add(shown, "p");
			at.id = node;
			at.part = page.part;
			at.label = TouchLib_Trim(TouchLegLib_Label(model, "TABSTR_" + TouchLib_Cut(node, 7, strlen(node))));
			if (page.label != "") at.label = UiText(page.label);
			at.selected = node == active;
			if (CheckAttribute(&TouchLegJournal, "titles." + node)) at.count = TouchLib_Count(&TouchLegJournal, "titles." + node);
			if (node == active) out.part = page.part;
			if (node == active && sti(page.entries)) out.entries = "";
		}
	}
	if (CheckAttribute(out, "entries") && active != "" && CheckAttribute(&TouchLegJournal, "titles." + active))
	{
		aref entries;
		makearef(entries, out.entries);
		for (k = 0; k < TouchLib_Count(&TouchLegJournal, "titles." + active); k++)
		{
			aref title = TouchLib_Item(&TouchLegJournal, "titles." + active, k);
			aref entry = TouchLib_Add(entries, "e");
			entry.label = title.label;
			entry.open = text && title.id == TouchLegJournal.open;
			if (sti(entry.open)) out.text.title = title.label;
		}
	}
	if (text)
	{
		if (!CheckAttribute(out, "text.title")) out.text.title = "";
		aref records;
		makearef(records, out.text.records);
		TouchLegJournal_Records(model.QUEST_TEXT.label, records);
	}
	aref specs, tables;
	makearef(specs, ui.data.tables);
	makearef(tables, out.tables);
	for (k = 0; k < GetAttributesNum(specs); k++) TouchLegJournal_Table(ui, model, GetAttributeN(specs, k), tables);
}

void TouchAdpJournal_Page(aref ui, string page)
{
	if (!UiOffers(page, "press")) return;
	UiSend(page, "press", "");
}

void TouchAdpJournal_Open(aref ui, int entry)
{
	aref model = TouchLegLib_Model(ui);
	string active = TouchLegJournal_Active(ui, model);
	aref title = TouchLib_Item(&TouchLegJournal, "titles." + active, entry);
	if (title.id == TouchLegJournal.open) return;
	TouchLegJournal.asked = title.id;
	DeleteAttribute(&TouchLegJournal, "looked");
	if (CheckAttribute(model, "QUEST_TEXT")) { UiBack(""); return; }
	aref row = TouchLib_Item(model, "QUEST_TITLE.rows", entry);
	object action = {node = "QUEST_TITLE", row = row.index, verb = "open", value = row.id};
	UiSendTo(&action);
	TouchLegJournal.pending = row.id;
	TouchLegJournal.asked = "";
}

// In a build, after State: the walk to the quest's entry starts from the page shown.
void TouchAdpJournal_Find(aref ui, string quest)
{
	aref model = TouchLegLib_Model(ui);
	TouchLegJournal.asked = quest;
	DeleteAttribute(&TouchLegJournal, "looked");
	TouchLegJournal_Step(ui, model, TouchLegJournal_Active(ui, model), CheckAttribute(model, "QUEST_TEXT"));
}

void TouchAdpJournal_Choose(aref ui, string table, int row)
{
	aref model = TouchLegLib_Model(ui);
	aref at = TouchLib_Item(model, table + ".rows", row);
	if (at.value == "1") return;
	TouchLegLib_Choose(model, table, row);
}

void TouchAdpJournal_Section(aref ui, string section)
{
	TouchLegLib_Section(TouchLegLib_Model(ui), section);
}

// The player's back: an open text closes first (the window's back), then the journal.
void TouchAdpJournal_Leave(aref ui)
{
	if (CheckAttribute(ui, "model.QUEST_TEXT")) TouchLegJournal.leave = true;
	UiBack("");
}
