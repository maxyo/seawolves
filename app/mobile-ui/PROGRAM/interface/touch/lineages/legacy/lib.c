// SPDX-License-Identifier: GPL-3.0-only
// What every adapter of this lineage reads the game's interfaces by: captions, the cells of a TABLE_LIST, the rows of
// a choice, and the game's amount window filled and confirmed.

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

// The window takes `amount` (< 0 gives) and its OK, guarded by the row it was opened on. Its edit box reads every
// number after a negative one as giving until a zero: the zero goes first, in the same flush.
void TouchLegLib_Confirm(aref model, int amount, string row)
{
	object value = {node = TouchLegLib_Edit(model), verb = "edit", value = "" + amount, guard = row, lead = "0"};
	UiSendTo(&value);
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
