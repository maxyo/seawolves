// SPDX-License-Identifier: GPL-3.0-only
// What every adapter of this lineage reads the game's interfaces by: captions, the cells of a TABLE_LIST, the rows of
// a choice, and the game's amount window filled and confirmed.

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

// The amount a column writes; an empty cell holds none.
int TouchLegLib_Amount(aref row, int column, string what)
{
	string text = TouchLegLib_Cell(row, column);
	if (text == "") return 0;
	return UiAmount(text, what);
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

// The game's amount window takes `amount` (< 0 gives) and its OK, guarded by the row it was opened on. Its edit box
// reads every number after a negative one as giving until a zero: the zero goes first, in the same flush.
void TouchLegLib_Confirm(string edit, string ok, int amount, string row)
{
	object value = {node = edit, verb = "edit", value = "" + amount, guard = row, lead = "0"};
	UiSendTo(&value);
	object press = {node = ok, verb = "press", value = "", guard = row};
	UiSendTo(&press);
}
