// SPDX-License-Identifier: GPL-3.0-only
// The trade windows of the game (chests and bodies, the storage): one goods table TABLE_LIST with the columns of each
// window, the amount window QTY_*, the side switch and the place's buttons. Implements contracts/trade.c, which it
// does not include: a segment cannot define what it declares extern.

// The columns of each window's table (-1: none): ours, theirs, the name, the pack, the total weight of each side's
// pieces; whether its rows are goods in packs (their weight is the game's GetGoodWeightByType); the side switch's node.
void TouchLegTrade_Columns(string capability, aref out)
{
	out.own = 0;
	out.name = 2;
	out.pack = -1;
	out.ownTotal = -1;
	out.otherTotal = -1;
	out.goods = false;
	out.pieces = false;
	if (capability == TOUCH_CAP_TRADE_CHEST)
	{
		out.other = 4;
		out.ownTotal = 1;
		out.otherTotal = 3;
		out.owners = "CHARACTERS_SCROLL";
		return;
	}
	if (capability == TOUCH_CAP_TRADE_STORAGE)
	{
		out.other = 3;
		out.pack = 4;
		out.goods = true;
		out.owners = "SHIPS_SCROLL";
		return;
	}
	UiError("trade: no columns for " + capability);
}

// The good of a table row: the game keeps it beside the row it shows (GameInterface.TABLE_LIST.tr<n>.index).
int TouchLegTrade_Good(int index)
{
	string row = "tr" + (index + 1);
	if (!CheckAttribute(&GameInterface, "TABLE_LIST." + row + ".index"))
	{
		UiError("trade: the game's table has no good for its row " + row);
		return -1;
	}
	return sti(GameInterface.TABLE_LIST.(row).index);
}

string TouchLegTrade_Label(string node)
{
	if (!CheckAttribute(&UiModel, node)) return "";
	return UiModel.(node).label;
}

string TouchLegTrade_Cell(aref row, int column)
{
	if (column < 0) return "";
	string cell = "c" + column;
	if (!CheckAttribute(row, "cells." + cell)) return "";
	return row.cells.(cell).text;
}

// The amount window's field: QTY_EDIT, or QTY_EDITBOX in the windows that name it so.
string TouchLegTrade_Edit()
{
	if (CheckAttribute(&UiModel, "QTY_EDIT")) return "QTY_EDIT";
	if (CheckAttribute(&UiModel, "QTY_EDITBOX")) return "QTY_EDITBOX";
	return "";
}

// rule copy: store.c FillTable tells the trade type only by the colour of the goods name.
string TouchLegTrade_Type(aref row, int column)
{
	string cell = "c" + column;
	if (!CheckAttribute(row, "cells." + cell + ".color")) return "";
	string color = row.cells.(cell).color;
	if (color == "c4ffc4") return "export";
	if (color == "c4c4ff") return "import";
	if (color == "ffc462") return "demand";
	if (color == "ffc4c4") return "contraband";
	return "";
}

void TouchLegTrade_Image(aref images, aref out)
{
	aref first = GetAttributeN(images, 0);
	out.kind = first.kind;
	out.group = first.group;
	out.image = first.image;
}

// The picture of a row: its name's, else the row's first.
void TouchLegTrade_Picture(aref row, int column, aref out)
{
	aref images;
	string cell = "c" + column;
	if (CheckAttribute(row, "cells." + cell + ".images"))
	{
		makearef(images, row.cells.(cell).images);
		TouchLegTrade_Image(images, out);
		return;
	}
	if (CheckAttribute(row, "images"))
	{
		makearef(images, row.images);
		TouchLegTrade_Image(images, out);
		return;
	}
	aref cells;
	makearef(cells, row.cells);
	for (int i = 0; i < GetAttributesNum(cells); i++)
	{
		aref one = GetAttributeN(cells, i);
		if (CheckAttribute(one, "images"))
		{
			makearef(images, one.images);
			TouchLegTrade_Image(images, out);
			return;
		}
	}
}

// The game's goods text ends with the line "Вес 1 ц, пачка 20 шт." (the word is written into its scripts, the same in
// every language); the screen shows the weight and what the figures are for itself, so that line goes.
string TouchLegTrade_WithoutPackLine(string text, string units)
{
	if (units == "") return text;
	string mark = ", пачка " + units;
	string out = "";
	string line;
	int begin = 0;
	int size = strlen(text);
	while (begin <= size)
	{
		int end = findSubStr(text, TouchLib_Newline(), begin);
		if (end < 0) end = size;
		line = "";
		if (end > begin) line = strcut(text, begin, end - 1);
		if (findSubStr(line, mark, 0) < 0)
		{
			if (out != "") out += TouchLib_Newline();
			out += line;
		}
		begin = end + 1;
	}
	int last = strlen(out) - 1;
	while (last >= 0 && findSubStr(TouchLib_LineEnds(), strcut(out, last, last), 0) >= 0) last--;
	if (last < 0) return "";
	return strcut(out, 0, last);
}

void TouchLegTrade_Rows(aref columns, aref out)
{
	out.selected = -1;
	if (!CheckAttribute(&UiModel, "TABLE_LIST.rows")) return;
	aref rows;
	makearef(rows, UiModel.TABLE_LIST.rows);
	aref headers;
	makearef(headers, UiModel.TABLE_LIST.headers);
	out.headers.own = TouchLegTrade_Header(headers, sti(columns.own));
	out.headers.other = TouchLegTrade_Header(headers, sti(columns.other));
	out.headers.name = TouchLegTrade_Header(headers, sti(columns.name));
	aref at;
	aref image;
	string slot;
	int name = sti(columns.name);
	for (int k = 0; k < GetAttributesNum(rows); k++)
	{
		aref row = GetAttributeN(rows, k);
		slot = "r" + k;
		makearef(at, out.rows.(slot));
		at.key = row.id;
		at.index = row.index;
		at.name = TouchLegTrade_Cell(row, name);
		makearef(image, at.image);
		TouchLegTrade_Picture(row, name, image);
		at.ownText = TouchLegTrade_Cell(row, sti(columns.own));
		at.otherText = TouchLegTrade_Cell(row, sti(columns.other));
		at.own = UiAmount(at.ownText, "the own amount column", 0);
		at.other = UiAmount(at.otherText, "the other amount column", 0);
		at.activate = CheckAttribute(row, "offers.activate");
		at.select = CheckAttribute(row, "offers.select");
		at.reason = row.reason;
		at.selected = row.value == "1";
		at.type = TouchLegTrade_Type(row, name);
		at.pack = TouchLegTrade_Cell(row, sti(columns.pack));
		at.weight = TouchLegTrade_PieceWeight(columns, row, sti(at.own), sti(at.other));
		at.weightUnit = "";
		if (sti(columns.goods)) at.weightUnit = "cwt";
		if (row.value == "1") out.selected = k;
	}
}

string TouchLegTrade_Header(aref headers, int column)
{
	string cell = "c" + column;
	if (column < 0 || !CheckAttribute(headers, cell)) return "";
	return UiOneLine(headers.(cell));
}

// The weight of what a row's figures are for: a pack of goods as the game counts it, or one piece from a chest's
// total weight of the side that has more pieces (the finer figure; "-" is gold, which weighs nothing: itemsbox.c
// writes it in both weight columns).
string TouchLegTrade_PieceWeight(aref columns, aref row, int own, int other)
{
	if (sti(columns.goods))
	{
		int units = UiAmount(TouchLegTrade_Cell(row, sti(columns.pack)), "the pack column", 1);
		return "" + GetGoodWeightByType(TouchLegTrade_Good(sti(row.index)), units);
	}
	if (sti(columns.ownTotal) < 0) return "";
	int count = own;
	string cell = TouchLegTrade_Cell(row, sti(columns.ownTotal));
	if (other > own)
	{
		count = other;
		cell = TouchLegTrade_Cell(row, sti(columns.otherTotal));
	}
	if (cell == "-" || count <= 0) return "";
	float total = UiNumber(cell, "TABLE_LIST total weight", 0.0);
	if (total <= 0.0) return "";
	return Sprintf("%.2g", total / count);
}

void TouchLegTrade_Owners(aref columns, aref out)
{
	string node = columns.owners;
	if (!CheckAttribute(&UiModel, node + ".rows")) return;
	aref rows;
	aref at;
	makearef(rows, UiModel.(node).rows);
	string slot;
	for (int k = 0; k < GetAttributesNum(rows); k++)
	{
		aref row = GetAttributeN(rows, k);
		slot = "o" + k;
		makearef(at, out.owners.(slot));
		at.key = row.id;
		at.label = row.label;
		at.selected = row.value == "1";
		at.can = CheckAttribute(row, "offers.select");
	}
}

// The game's category tabs TABBTN_<n> with their captions TABSTR_<n>; the script takes the press on the caption over
// the picture where it has one.
string TouchLegTrade_TabNode(int position)
{
	if (CheckAttribute(&UiModel, "TABSTR_" + position + ".offers.press")) return "TABSTR_" + position;
	return "TABBTN_" + position;
}

void TouchLegTrade_Tabs(aref out)
{
	aref at;
	string slot;
	string button;
	string caption;
	int k = 0;
	for (int position = 1; position <= 8; position++)
	{
		button = "TABBTN_" + position;
		caption = "TABSTR_" + position;
		if (CheckAttribute(&UiModel, button) && TouchLegTrade_Label(caption) != "")
		{
			slot = "t" + k;
			makearef(at, out.tabs.(slot));
			at.position = position;
			at.label = UiModel.(caption).label;
			at.selected = UiModel.(button).value == "1";
			at.can = UiOffers(TouchLegTrade_TabNode(position), "press");
			k++;
		}
	}
}

string TouchLegTrade_Buttons[2] = {"DELBODY_BUTTON", "SKULL_BUTTON"};

void TouchLegTrade_Places(aref out)
{
	aref at;
	string slot;
	int k = 0;
	for (int i, string node: TouchLegTrade_Buttons)
	{
		if (CheckAttribute(&UiModel, node))
		{
			slot = "b" + k;
			makearef(at, out.buttons.(slot));
			at.label = UiModel.(node).label;
			at.can = UiOffers(node, "press");
			at.reason = UiModel.(node).reason;
			k++;
		}
	}
	if (CheckAttribute(&UiModel, "GETALL_BUTTON"))
	{
		out.getAll.label = UiModel.GETALL_BUTTON.label;
		out.getAll.can = UiOffers("GETALL_BUTTON", "press");
		out.getAll.reason = UiModel.GETALL_BUTTON.reason;
	}
	if (CheckAttribute(&UiModel, "ANDROID_TAKE_ALL")) out.takeAll.can = UiOffers("ANDROID_TAKE_ALL", "press");
}

void TouchAdpTrade_State(string capability, aref out)
{
	object columns;
	TouchLegTrade_Columns(capability, &columns);
	DeleteAttribute(out, "");
	out.title = TouchLegTrade_Label("TITLE");
	if (out.title == "") out.title = UiOneLine(TouchLegTrade_Label("STORECAPTION1"));
	out.pieces = columns.pieces;
	out.money = false;
	out.weighs = columns.goods;
	TouchLegTrade_Rows(&columns, out);
	int selected = sti(out.selected);
	string edit = TouchLegTrade_Edit();
	out.deal = selected >= 0 && edit != "" &&
	           (CheckAttribute(&UiModel, "QTY_OK_BUTTON") || CheckAttribute(&UiModel, "QTY_CANCEL_BUTTON"));
	if (sti(out.deal))
	{
		string slot = "r" + selected;
		out.editable = UiOffers(edit, "edit");
		out.ok = CheckAttribute(&UiModel, "QTY_OK_BUTTON");
		out.confirm = false;
		out.confirmReason = "";
		if (CheckAttribute(&UiModel, "QTY_OK_BUTTON"))
		{
			out.confirm = UiOffers("QTY_OK_BUTTON", "press");
			out.confirmReason = UiModel.QTY_OK_BUTTON.reason;
		}
		out.dealTitle = TouchLegTrade_Label("QTY_CAPTION");
		out.dealText = TouchLegTrade_WithoutPackLine(TouchLegTrade_Label("QTY_GOODS_INFO"), out.rows.(slot).pack);
	}
	out.cancel = false;
	if (CheckAttribute(&UiModel, "QTY_CANCEL_BUTTON")) out.cancel = UiOffers("QTY_CANCEL_BUTTON", "press");
	TouchLegTrade_Owners(&columns, out);
	TouchLegTrade_Tabs(out);
	out.carry = TouchLegTrade_Label("CAPACITY");
	out.place = TouchLegTrade_Label("STORE_CAPACITY");
	out.storage = TouchLegTrade_Label("STORAGE_INFO");
	out.info = TouchLegTrade_Label("INFO_TEXT");
	TouchLegTrade_Places(out);
}

float TouchAdpTrade_Weight(string capability, int row, int amount)
{
	aref at = TouchLegTrade_Row(row);
	return makefloat(GetGoodWeightByType(TouchLegTrade_Good(sti(at.index)), amount));
}

aref TouchLegTrade_Row(int k)
{
	aref rows;
	makearef(rows, UiModel.TABLE_LIST.rows);
	return GetAttributeN(rows, k);
}

void TouchAdpTrade_Open(int row)
{
	aref at = TouchLegTrade_Row(row);
	UiSendRow("TABLE_LIST", sti(at.index), "activate", at.id, "");
}

void TouchAdpTrade_Select(int row)
{
	aref at = TouchLegTrade_Row(row);
	UiSendRow("TABLE_LIST", sti(at.index), "select", at.id, "");
}

// The selected row's identity, which the window's moves are guarded by.
string TouchLegTrade_Selected()
{
	aref rows;
	makearef(rows, UiModel.TABLE_LIST.rows);
	for (int k = 0; k < GetAttributesNum(rows); k++)
	{
		aref row = GetAttributeN(rows, k);
		if (row.value == "1") return row.id;
	}
	UiError("trade: the amount window is open on no selected row");
	return "";
}

void TouchAdpTrade_Close()
{
	UiSendRow("QTY_CANCEL_BUTTON", -1, "press", "", TouchLegTrade_Selected());
}

// The game's edit box holds the amount to take, a negative one the amount to give; once it has turned to giving it
// reads every later number so, and zero leaves it: zero goes first, in the same flush.
void TouchAdpTrade_Move(int amount)
{
	string row = TouchLegTrade_Selected();
	UiSendAfter(TouchLegTrade_Edit(), -1, "edit", "" + amount, row, 0.0, "0");
	UiSendAfter("QTY_OK_BUTTON", -1, "press", "", row, 0.0, "");
}

void TouchAdpTrade_Owner(string capability, int owner)
{
	object columns;
	TouchLegTrade_Columns(capability, &columns);
	string node = columns.owners;
	aref rows;
	makearef(rows, UiModel.(node).rows);
	aref at = GetAttributeN(rows, owner);
	UiSendRow(node, sti(at.index), "select", at.id, "");
}

void TouchAdpTrade_Tab(int tab)
{
	int k = 0;
	for (int position = 1; position <= 8; position++)
	{
		if (CheckAttribute(&UiModel, "TABBTN_" + position) && TouchLegTrade_Label("TABSTR_" + position) != "")
		{
			if (k == tab)
			{
				UiSend(TouchLegTrade_TabNode(position), "press", "");
				return;
			}
			k++;
		}
	}
	UiError("trade: no tab " + tab);
}

void TouchAdpTrade_Press(int button)
{
	int k = 0;
	for (int i, string node: TouchLegTrade_Buttons)
	{
		if (CheckAttribute(&UiModel, node))
		{
			if (k == button)
			{
				UiSend(node, "press", "");
				return;
			}
			k++;
		}
	}
	UiError("trade: no place button " + button);
}

void TouchAdpTrade_GetAll()
{
	UiSend("GETALL_BUTTON", "press", "");
}

void TouchAdpTrade_TakeAll()
{
	UiSend("ANDROID_TAKE_ALL", "press", "");
}

int TouchAdpTrade_TakingAll()
{
	if (!CheckAttribute(&UiModel, "ANDROID_TAKE_ALL")) return false;
	return UiWaiting("ANDROID_TAKE_ALL", -1, "press");
}

void TouchAdpTrade_Exit()
{
	if (CheckAttribute(&UiModel, "EXIT_BTN"))
	{
		UiBack("EXIT_BTN");
		return;
	}
	UiBack("");
}
