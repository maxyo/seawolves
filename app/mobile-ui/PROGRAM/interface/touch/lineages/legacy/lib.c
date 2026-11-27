// SPDX-License-Identifier: GPL-3.0-only
// What every adapter of this lineage reads the game's interfaces by: captions, the cells of a TABLE_LIST, the rows of
// a choice, the game's amount window filled and confirmed, the purse caption, the hero sections and the game's
// descriptions.

aref TouchLegLib_Model(aref ui)
{
	aref model;
	makearef(model, ui.model);
	return model;
}

// The caption of an optional node, "" without it.
string TouchLegLib_Label(aref model, string node)
{
	if (!CheckAttribute(model, node)) return "";
	return model.(node).label;
}

// The text of a row's column; -1 is a column the window does not have.
string TouchLegLib_Cell(aref row, int column)
{
	if (column < 0) return "";
	string cell = "c" + column;
	if (!CheckAttribute(row, "cells." + cell)) UiError("row " + row.id + " has no column " + column);
	return row.cells.(cell).text;
}

int TouchLegLib_Amount(aref row, int column, string what)
{
	return UiAmount(TouchLegLib_Cell(row, column), what);
}

// A row's picture: its column's, else the row's own, else the first a cell of it has.
void TouchLegLib_Picture(aref row, int column, aref out)
{
	aref cells;
	makearef(cells, row.cells);
	string cell = "c" + column;
	aref named;
	makearef(named, cells.(cell));
	if (TouchLegLib_Image(named, out)) return;
	if (TouchLegLib_Image(row, out)) return;
	for (int i = 0; i < GetAttributesNum(cells); i++)
		if (TouchLegLib_Image(GetAttributeN(cells, i), out)) return;
}

bool TouchLegLib_Image(aref at, aref out)
{
	if (!CheckAttribute(at, "images")) return false;
	aref images;
	makearef(images, at.images);
	aref first = GetAttributeN(images, 0);
	out.kind = first.kind;
	out.group = first.group;
	out.image = first.image;
	return true;
}

// The rows of a choice node (ships, characters) as {key, label, selected, can}, "" without the node.
void TouchLegLib_Choices(aref model, string node, aref out)
{
	if (!CheckAttribute(model, node + ".rows")) return;
	aref rows;
	makearef(rows, model.(node).rows);
	for (int k = 0; k < GetAttributesNum(rows); k++)
	{
		aref row = GetAttributeN(rows, k);
		object choice = {key = row.id, label = row.label, selected = row.value == "1",
		                 can = CheckAttribute(row, "offers.select")};
		aref at = TouchLib_Add(out, "s");
		CopyAttributes(at, &choice);
	}
}

void TouchLegLib_Choose(aref model, string node, int k)
{
	aref row = TouchLib_Item(model, node + ".rows", k);
	object action = {node = node, row = row.index, verb = "select", value = row.id};
	UiSendTo(&action);
}

// The game's amount window QTY_*: its field (QTY_EDIT, or QTY_EDITBOX in the windows that name it so), and k of
// the TABLE_LIST row it is open on, -1 none.
string TouchLegLib_Edit(aref model)
{
	if (CheckAttribute(model, "QTY_EDIT")) return "QTY_EDIT";
	return "QTY_EDITBOX";
}

int TouchLegLib_Window(aref model)
{
	if (!CheckAttribute(model, TouchLegLib_Edit(model))) return -1;
	if (!CheckAttribute(model, "QTY_OK_BUTTON") && !CheckAttribute(model, "QTY_CANCEL_BUTTON")) return -1;
	for (int k = 0; k < TouchLib_Count(model, "TABLE_LIST.rows"); k++)
	{
		aref row = TouchLib_Item(model, "TABLE_LIST.rows", k);
		if (row.value == "1") return k;
	}
	return -1;
}

// Row k as the player's tap opens it: activated where it offers it, else selected; true when a window comes.
bool TouchLegLib_Open(aref model, int k)
{
	aref row = TouchLib_Item(model, "TABLE_LIST.rows", k);
	object action = {node = "TABLE_LIST", row = row.index, verb = "select", value = row.id};
	if (CheckAttribute(row, "offers.activate")) action.verb = "activate";
	UiSendTo(&action);
	return action.verb == "activate";
}

// The open window closed with its cancel, guarded by its row; k of that row, -1 none open.
int TouchLegLib_Cancel(aref model)
{
	int open = TouchLegLib_Window(model);
	if (open < 0) return -1;
	aref row = TouchLib_Item(model, "TABLE_LIST.rows", open);
	object action = {node = "QTY_CANCEL_BUTTON", verb = "press", value = "", guard = row.id};
	UiSendTo(&action);
	return open;
}

// The window takes `amount` (< 0 gives), guarded by the row it was opened on. Its edit box reads every number after a
// negative one as giving until a zero: the zero goes first, in the same flush.
void TouchLegLib_Offer(aref model, int amount, string row)
{
	object value = {node = TouchLegLib_Edit(model), verb = "edit", value = "" + amount, guard = row, lead = "0"};
	UiSendTo(&value);
}

// The window takes `amount` and its OK.
void TouchLegLib_Confirm(aref model, int amount, string row)
{
	TouchLegLib_Offer(model, amount, row);
	object press = {node = "QTY_OK_BUTTON", verb = "press", value = "", guard = row};
	UiSendTo(&press);
}

// The game's category tabs TABBTN_<n> with their captions TABSTR_<n> as {node (pressed: the caption where it takes
// it), label (`words` t<n>, else the game's), selected, can}; none without tabs.
void TouchLegLib_Tabs(aref model, aref words, aref out)
{
	aref tabs;
	makearef(tabs, out.tabs);
	for (int position = 1; position <= 8; position++)
	{
		string caption = "TABSTR_" + position;
		string button = "TABBTN_" + position;
		string word = "t" + position;
		if (CheckAttribute(model, button) && TouchLegLib_Label(model, caption) != "")
		{
			object tab = {node = button, label = model.(caption).label, selected = model.(button).value == "1"};
			if (CheckAttribute(model, caption + ".offers.press")) tab.node = caption;
			if (CheckAttribute(words, word)) tab.label = UiText(words.(word));
			tab.can = UiOffers(tab.node, "press");
			aref at = TouchLib_Add(tabs, "t");
			CopyAttributes(at, &tab);
		}
	}
	if (GetAttributesNum(tabs) == 0) DeleteAttribute(out, "tabs");
}

// Our money from the game's "Наши деньги 1 500 песо": out.value (its figure as written), out.currency (the string key
// of what the merchant takes, "" money); false without OUR_GOLD.
bool TouchLegLib_Purse(aref model, aref out)
{
	if (!CheckAttribute(model, "OUR_GOLD")) return false;
	string gold = model.OUR_GOLD.label;
	UiAmount(gold, "OUR_GOLD");
	out.currency = "";
	if (CheckAttribute(model, "OUR_GOLD.context.currency"))
		if (model.OUR_GOLD.context.currency == "doubloons") out.currency = "trade.doubloons";
	int digit = TouchLib_FindDigit(gold, 0, true);
	int end = digit;
	while (end < strlen(gold) && findSubStr("0123456789 ", strcut(gold, end, end), 0) >= 0) end++;
	while (end > digit && strcut(gold, end - 1, end - 1) == " ") end--;
	out.value = TouchLib_Cut(gold, digit, end);
	return true;
}

// The hero sections' windows (character, ship, journal, trade book, nations, items) open one another by their
// buttons I_<SECTION>; a section the window has no button for is the screen's defect.
void TouchLegLib_Section(aref model, string section)
{
	object nodes = {hero = "I_CHARACTER", ship = "I_SHIP", journal = "I_QUESTBOOK", tradebook = "I_TRADEBOOK",
	                nations = "I_NATIONS", items = "I_ITEMS"};
	if (!CheckAttribute(&nodes, section)) UiError("no hero section " + section);
	string node = nodes.(section);
	bool can = false;
	if (CheckAttribute(model, node)) can = UiOffers(node, "press");
	if (!can) UiError("the section " + section + " cannot be opened here (" + node + ")");
	UiSend(node, "press", "");
}

// A description of the game's text files as a sheet shows it: a line of only "~" (their paragraph mark) empty, no
// empty lines before or after it.
string TouchLegLib_StockText(string text)
{
	string newline = TouchLib_Newline();
	string out = "";
	string held = "";
	string line;
	int from = 0;
	while (from <= strlen(text))
	{
		int end = findSubStr(text, newline, from);
		if (end < 0) end = strlen(text);
		line = TouchLib_Cut(text, from, end);
		from = end + 1;
		if (UiOneLine(line) == "~" || UiOneLine(line) == "")
		{
			if (out != "") held += newline;
			continue;
		}
		out += held + line;
		held = newline;
	}
	return out;
}

// A crew as the game's windows draw it (SetCrewExpTable): the rows Sailors, Cannoners, Soldiers of `table` with their
// caption and word, the crew's figure of each where the game words it (a crew without men has none: an empty bar),
// the morale with its word after the colon of `moraleText`. out.{groups.g<k>.{label, value, word}, morale,
// moraleWord}.
void TouchLegLib_Crew(aref model, string table, string moraleText, ref crew, aref out)
{
	aref groups;
	makearef(groups, out.groups);
	string group;
	for (int k = 0; k < TouchLib_Count(model, table + ".rows"); k++)
	{
		aref row = TouchLib_Item(model, table + ".rows", k);
		group = row.id;
		object figure = {label = TouchLib_Trim(TouchLegLib_Cell(row, 1)), value = 0,
		                 word = TouchLib_Trim(TouchLegLib_Cell(row, 2))};
		if (figure.word != "") figure.value = sti(crew.Ship.Crew.Exp.(group));
		aref at = TouchLib_Add(groups, "g");
		CopyAttributes(at, &figure);
	}
	out.morale = sti(crew.Ship.Crew.Morale);
	out.moraleWord = TouchLib_AfterColon(UiOneLine(TouchLegLib_Label(model, moraleText)));
}
