// SPDX-License-Identifier: GPL-3.0-only
// The trade windows of the game (chests and bodies, the storage, the store): one goods table TABLE_LIST with the columns of each
// window, the amount window QTY_*, the side switch and the place's buttons. Implements contracts/trade.c, which it
// does not include: a segment cannot define what it declares extern.

// The model and the columns of the build or tap running now (its binding's data: ours, theirs, the name, the prices
// of a pack bought and sold, the pack, a pack's weight, each side's total weight, -1 none; goods: rows are goods in
// packs; owners: the side switch's node).
aref TouchLegTrade_M;
aref TouchLegTrade_C;

void TouchLegTrade_Use(aref ui)
{
	makearef(TouchLegTrade_M, ui.model);
	makearef(TouchLegTrade_C, ui.data);
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
	if (!CheckAttribute(TouchLegTrade_M, node)) return "";
	return TouchLegTrade_M.(node).label;
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
	if (CheckAttribute(TouchLegTrade_M, "QTY_EDIT")) return "QTY_EDIT";
	if (CheckAttribute(TouchLegTrade_M, "QTY_EDITBOX")) return "QTY_EDITBOX";
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
	if (!CheckAttribute(TouchLegTrade_M, "TABLE_LIST.rows")) return;
	aref rows;
	makearef(rows, TouchLegTrade_M.TABLE_LIST.rows);
	aref headers;
	makearef(headers, TouchLegTrade_M.TABLE_LIST.headers);
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
		at.buyText = TouchLegTrade_Cell(row, sti(columns.buy));
		at.sellText = TouchLegTrade_Cell(row, sti(columns.sell));
		at.buy = TouchLegTrade_Price(at.buyText);
		at.sell = TouchLegTrade_Price(at.sellText);
		at.pack = TouchLegTrade_Cell(row, sti(columns.pack));
		at.weight = TouchLegTrade_PieceWeight(columns, row, sti(at.own), sti(at.other));
		at.weightUnit = "";
		if (sti(columns.goods)) at.weightUnit = "cwt";
		if (row.value == "1") out.selected = k;
	}
}

// A price cell: "-" where the game does not trade the row this way (contraband is not sold back), 0 then.
int TouchLegTrade_Price(string cell)
{
	if (!UiHasAmount(cell)) return 0;
	return UiAmount(cell, "the price column", 0);
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
	if (sti(columns.packWeight) >= 0) return UiOneLine(TouchLegTrade_Cell(row, sti(columns.packWeight)));
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
	if (!CheckAttribute(TouchLegTrade_M, node + ".rows")) return;
	aref rows;
	aref at;
	makearef(rows, TouchLegTrade_M.(node).rows);
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
	if (CheckAttribute(TouchLegTrade_M, "TABSTR_" + position + ".offers.press")) return "TABSTR_" + position;
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
		if (CheckAttribute(TouchLegTrade_M, button) && TouchLegTrade_Label(caption) != "")
		{
			slot = "t" + k;
			makearef(at, out.tabs.(slot));
			at.position = position;
			at.label = TouchLegTrade_M.(caption).label;
			at.selected = TouchLegTrade_M.(button).value == "1";
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
		if (CheckAttribute(TouchLegTrade_M, node))
		{
			slot = "b" + k;
			makearef(at, out.buttons.(slot));
			at.label = TouchLegTrade_M.(node).label;
			at.can = UiOffers(node, "press");
			at.reason = TouchLegTrade_M.(node).reason;
			k++;
		}
	}
	if (CheckAttribute(TouchLegTrade_M, "GETALL_BUTTON"))
	{
		out.getAll.label = TouchLegTrade_M.GETALL_BUTTON.label;
		out.getAll.can = UiOffers("GETALL_BUTTON", "press");
		out.getAll.reason = TouchLegTrade_M.GETALL_BUTTON.reason;
	}
	if (CheckAttribute(TouchLegTrade_M, "ANDROID_TAKE_ALL")) out.takeAll.can = UiOffers("ANDROID_TAKE_ALL", "press");
}

void TouchAdpTrade_State(aref ui, aref out)
{
	TouchLegTrade_Use(ui);
	aref columns = TouchLegTrade_C;
	DeleteAttribute(out, "");
	out.title = TouchLegTrade_Label("TITLE");
	if (out.title == "") out.title = UiOneLine(TouchLegTrade_Label("STORECAPTION1"));
	out.pieces = columns.pieces;
	out.money = sti(columns.buy) >= 0;
	out.weighs = columns.goods;
	TouchLegTrade_Rows(columns, out);
	int selected = sti(out.selected);
	string edit = TouchLegTrade_Edit();
	out.deal = selected >= 0 && edit != "" &&
	           (CheckAttribute(TouchLegTrade_M, "QTY_OK_BUTTON") || CheckAttribute(TouchLegTrade_M, "QTY_CANCEL_BUTTON"));
	if (sti(out.deal))
	{
		string slot = "r" + selected;
		out.editable = UiOffers(edit, "edit");
		out.ok = CheckAttribute(TouchLegTrade_M, "QTY_OK_BUTTON");
		out.confirm = false;
		out.confirmReason = "";
		if (CheckAttribute(TouchLegTrade_M, "QTY_OK_BUTTON"))
		{
			out.confirm = UiOffers("QTY_OK_BUTTON", "press");
			out.confirmReason = TouchLegTrade_M.QTY_OK_BUTTON.reason;
		}
		out.dealTitle = TouchLegTrade_Label("QTY_CAPTION");
		out.dealText = TouchLegTrade_WithoutPackLine(TouchLegTrade_Label("QTY_GOODS_INFO"), out.rows.(slot).pack);
	}
	out.cancel = false;
	if (CheckAttribute(TouchLegTrade_M, "QTY_CANCEL_BUTTON")) out.cancel = UiOffers("QTY_CANCEL_BUTTON", "press");
	TouchLegTrade_Owners(columns, out);
	TouchLegTrade_Tabs(out);
	out.carry = TouchLegTrade_Label("CAPACITY");
	out.place = TouchLegTrade_Label("STORE_CAPACITY");
	out.storage = TouchLegTrade_Label("STORAGE_INFO");
	out.info = TouchLegTrade_Label("INFO_TEXT");
	TouchLegTrade_Hold(out);
	TouchLegTrade_Purse(out);
	TouchLegTrade_Places(out);
}

// What we carry of what we can, from the game's "Трюм:\n189 / 199": out.load, out.capacity (-1: not told).
void TouchLegTrade_Hold(aref out)
{
	out.load = 0.0;
	out.capacity = -1.0;
	object figure;
	if (!TouchLib_Figure(TouchLegTrade_Label("CAPACITY"), &figure)) return;
	string value = figure.value;
	int slash = findSubStr(value, "/", 0);
	if (slash < 0) return;
	out.load = UiNumber(TouchLib_Cut(value, 0, slash), "CAPACITY", 0.0);
	out.capacity = UiNumber(TouchLib_Cut(value, slash + 1, strlen(value)), "CAPACITY", 0.0);
}

// Our money from the game's "Наши деньги 1 500 песо": out.purse (its figure as written, "" without), out.balance (-1:
// not told) and out.doubloons (the merchant trades for doubloons; the engine marks the balance then).
void TouchLegTrade_Purse(aref out)
{
	out.purse = "";
	out.balance = -1;
	out.doubloons = false;
	if (!CheckAttribute(TouchLegTrade_M, "OUR_GOLD")) return;
	string gold = TouchLegTrade_M.OUR_GOLD.label;
	out.balance = UiAmount(gold, "OUR_GOLD", -1);
	if (CheckAttribute(TouchLegTrade_M, "OUR_GOLD.context.currency"))
		out.doubloons = TouchLegTrade_M.OUR_GOLD.context.currency == "doubloons";
	int digit = TouchLib_FindDigit(gold, 0, true);
	if (digit < 0) return;
	int end = digit;
	while (end < strlen(gold) && findSubStr("0123456789 ", strcut(gold, end, end), 0) >= 0)
	{
		end++;
	}
	while (end > digit && strcut(gold, end - 1, end - 1) == " ")
	{
		end--;
	}
	out.purse = TouchLib_Cut(gold, digit, end);
}

// rule copy: store.c ChangeQTY_EDIT charges makeint(price * pieces / units + 0.5) for a part of a pack's price.
int TouchAdpTrade_Total(aref ui, int row, int amount)
{
	TouchLegTrade_Use(ui);
	aref columns = TouchLegTrade_C;
	aref at = TouchLegTrade_Row(row);
	string cell = TouchLegTrade_Cell(at, sti(columns.sell));
	if (amount > 0) cell = TouchLegTrade_Cell(at, sti(columns.buy));
	int units = 1;
	if (sti(columns.pack) >= 0) units = UiAmount(TouchLegTrade_Cell(at, sti(columns.pack)), "the pack column", 1);
	if (units < 1) units = 1;
	return makeint(makefloat(TouchLegTrade_Price(cell)) * abs(amount) / units + 0.5);
}

// rule copy: store.c ChangeQTY_EDIT lets a purchase through while the hold takes its weight (GetGoodWeightByType) and
// the purse its total, and cuts it down past that; both grow with the amount, so the largest that passes is halved to.
void TouchAdpTrade_Reach(aref ui, int row, int stock, aref out)
{
	TouchLegTrade_Use(ui);
	object hold;
	TouchLegTrade_Hold(&hold);
	TouchLegTrade_Purse(&hold);
	out.most = TouchLegTrade_Largest(ui, row, stock, &hold, true);
	out.room = TouchLegTrade_Largest(ui, row, stock, &hold, false);
}

int TouchLegTrade_Largest(aref ui, int row, int stock, aref hold, bool paid)
{
	int low = 0;
	int high = stock;
	if (high < 0) high = 0;
	while (low < high)
	{
		int middle = low + (high - low + 1) / 2;
		if (TouchLegTrade_Passes(ui, row, middle, hold, paid)) low = middle;
		else high = middle - 1;
	}
	return low;
}

bool TouchLegTrade_Passes(aref ui, int row, int amount, aref hold, bool paid)
{
	float capacity = stf(hold.capacity);
	if (capacity >= 0.0 && stf(hold.load) + TouchAdpTrade_Weight(ui, row, amount) > capacity) return false;
	int balance = sti(hold.balance);
	if (!paid || balance < 0) return true;
	return TouchAdpTrade_Total(ui, row, amount) <= balance;
}

float TouchAdpTrade_Weight(aref ui, int row, int amount)
{
	TouchLegTrade_Use(ui);
	aref at = TouchLegTrade_Row(row);
	return makefloat(GetGoodWeightByType(TouchLegTrade_Good(sti(at.index)), amount));
}

aref TouchLegTrade_Row(int k)
{
	aref rows;
	makearef(rows, TouchLegTrade_M.TABLE_LIST.rows);
	return GetAttributeN(rows, k);
}

void TouchAdpTrade_Open(aref ui, int row)
{
	TouchLegTrade_Use(ui);
	aref at = TouchLegTrade_Row(row);
	UiSendRow("TABLE_LIST", sti(at.index), "activate", at.id, "");
}

void TouchAdpTrade_Select(aref ui, int row)
{
	TouchLegTrade_Use(ui);
	aref at = TouchLegTrade_Row(row);
	UiSendRow("TABLE_LIST", sti(at.index), "select", at.id, "");
}

// The selected row's identity, which the window's moves are guarded by.
string TouchLegTrade_Selected()
{
	aref rows;
	makearef(rows, TouchLegTrade_M.TABLE_LIST.rows);
	for (int k = 0; k < GetAttributesNum(rows); k++)
	{
		aref row = GetAttributeN(rows, k);
		if (row.value == "1") return row.id;
	}
	UiError("trade: the amount window is open on no selected row");
	return "";
}

void TouchAdpTrade_Close(aref ui)
{
	TouchLegTrade_Use(ui);
	UiSendRow("QTY_CANCEL_BUTTON", -1, "press", "", TouchLegTrade_Selected());
}

// The game's edit box holds the amount to take, a negative one the amount to give; once it has turned to giving it
// reads every later number so, and zero leaves it: zero goes first, in the same flush.
void TouchAdpTrade_Move(aref ui, int amount)
{
	TouchLegTrade_Use(ui);
	string row = TouchLegTrade_Selected();
	UiSendAfter(TouchLegTrade_Edit(), -1, "edit", "" + amount, row, 0.0, "0");
	UiSendAfter("QTY_OK_BUTTON", -1, "press", "", row, 0.0, "");
}

void TouchAdpTrade_Owner(aref ui, int owner)
{
	TouchLegTrade_Use(ui);
	string node = TouchLegTrade_C.owners;
	aref rows;
	makearef(rows, TouchLegTrade_M.(node).rows);
	aref at = GetAttributeN(rows, owner);
	UiSendRow(node, sti(at.index), "select", at.id, "");
}

void TouchAdpTrade_Tab(aref ui, int tab)
{
	TouchLegTrade_Use(ui);
	int k = 0;
	for (int position = 1; position <= 8; position++)
	{
		if (CheckAttribute(TouchLegTrade_M, "TABBTN_" + position) && TouchLegTrade_Label("TABSTR_" + position) != "")
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

void TouchAdpTrade_Press(aref ui, int button)
{
	TouchLegTrade_Use(ui);
	int k = 0;
	for (int i, string node: TouchLegTrade_Buttons)
	{
		if (CheckAttribute(TouchLegTrade_M, node))
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

void TouchAdpTrade_GetAll(aref ui)
{
	TouchLegTrade_Use(ui);
	UiSend("GETALL_BUTTON", "press", "");
}

void TouchAdpTrade_TakeAll(aref ui)
{
	TouchLegTrade_Use(ui);
	UiSend("ANDROID_TAKE_ALL", "press", "");
}

int TouchAdpTrade_TakingAll(aref ui)
{
	TouchLegTrade_Use(ui);
	if (!CheckAttribute(TouchLegTrade_M, "ANDROID_TAKE_ALL")) return false;
	return UiWaiting("ANDROID_TAKE_ALL", -1, "press");
}

void TouchAdpTrade_Exit(aref ui)
{
	TouchLegTrade_Use(ui);
	if (CheckAttribute(TouchLegTrade_M, "EXIT_BTN"))
	{
		UiBack("EXIT_BTN");
		return;
	}
	UiBack("");
}
