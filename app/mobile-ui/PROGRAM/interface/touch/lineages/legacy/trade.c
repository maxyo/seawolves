// SPDX-License-Identifier: GPL-3.0-only
// The trade windows (chests and bodies, the storage, the store): the goods table TABLE_LIST with the columns of the
// binding's data, the amount window QTY_*, the side switch and the place's buttons. Implements contracts/trade.c.

// The game's step a deal is at while the screen waits for the answer: "open" (row key's window comes a frame after
// its selection; `side`: a switch waiting for it), "quick" (then all of their stack is taken), "again" (a confirmed
// deal closed key's window: it opens again), "side" (the switch binds a frame after the selector moved to `side`),
// "settle" (its rows come the frame after; the goods `name` that were open open again).
object TouchLegTrade;

void TouchLegTrade_Step(string step, string key, string name, int side)
{
	object at = {step = step, key = key, name = name, side = side};
	CopyAttributes(&TouchLegTrade, &at);
}

// The good of a table row: the game keeps it beside the row it shows (GameInterface.TABLE_LIST.tr<n>.index).
int TouchLegTrade_Good(aref row)
{
	string tr = "tr" + (sti(row.index) + 1);
	if (!CheckAttribute(&GameInterface, "TABLE_LIST." + tr + ".index"))
		UiError("trade: the game's table has no good for its row " + tr);
	return sti(GameInterface.TABLE_LIST.(tr).index);
}

aref TouchLegTrade_Row(aref ui, int k)
{
	return TouchLib_Item(ui, "model.TABLE_LIST.rows", k);
}

string TouchLegTrade_Key(aref ui, int k)
{
	aref row = TouchLegTrade_Row(ui, k);
	return row.id;
}

// rule copy: store.c FillTable tells the trade type only by the colour of the goods name.
void TouchLegTrade_Type(aref row, int column, aref out)
{
	string cell = "c" + column;
	if (!CheckAttribute(row, "cells." + cell + ".color")) return;
	string color = row.cells.(cell).color;
	object type = {word = "", tone = "", profit = true};
	if (color == "c4ffc4") { type.word = UiText("trade.export"); type.tone = "ok"; }
	if (color == "c4c4ff") { type.word = UiText("trade.import"); type.tone = "gold"; }
	if (color == "ffc462") { type.word = UiText("trade.demand"); type.tone = "gold"; }
	if (color == "ffc4c4") { type.word = UiText("trade.contraband_goods"); type.tone = "danger"; type.profit = false; }
	if (type.word == "") return;
	aref at;
	makearef(at, out.type);
	CopyAttributes(at, &type);
}

// The game's goods text ends with the line "Вес 1 ц, пачка 20 шт." (the word is written into its scripts, the same in
// every language); the screen shows the weight and what the figures are for itself, so that line goes.
string TouchLegTrade_WithoutPackLine(string text, int units)
{
	if (units <= 1) return text;
	string mark = ", пачка " + units;
	string out = "";
	int begin = 0;
	int size = strlen(text);
	while (begin <= size)
	{
		int end = findSubStr(text, TouchLib_Newline(), begin);
		if (end < 0) end = size;
		string line = TouchLib_Cut(text, begin, end);
		if (findSubStr(line, mark, 0) < 0)
		{
			if (out != "") out += TouchLib_Newline();
			out += line;
		}
		begin = end + 1;
	}
	int last = strlen(out) - 1;
	while (last >= 0 && findSubStr(TouchLib_LineEnds(), strcut(out, last, last), 0) >= 0) last--;
	return TouchLib_Cut(out, 0, last + 1);
}

// The weight of what a row's figures are for: a pack of goods as the game counts it, or one piece from a chest's
// total weight of the side that has more pieces (the finer figure; "-" is gold, which weighs nothing: itemsbox.c
// writes it in both weight columns).
string TouchLegTrade_PieceWeight(aref data, aref row, aref at)
{
	if (sti(data.packWeight) >= 0) return UiOneLine(TouchLegLib_Cell(row, sti(data.packWeight)));
	if (sti(data.goods)) return "" + GetGoodWeightByType(TouchLegTrade_Good(row), sti(at.pack));
	if (sti(data.ownTotal) < 0) return "";
	int count = sti(at.own);
	string cell = TouchLegLib_Cell(row, sti(data.ownTotal));
	if (sti(at.other) > count)
	{
		count = sti(at.other);
		cell = TouchLegLib_Cell(row, sti(data.otherTotal));
	}
	if (cell == "-" || count <= 0) return "";
	return Sprintf("%.2g", UiNumber(cell, "the total weight column") / count);
}

void TouchLegTrade_Rows(aref data, aref model, aref out)
{
	if (!CheckAttribute(model, "TABLE_LIST.rows")) return;
	aref rows;
	makearef(rows, model.TABLE_LIST.rows);
	aref headers;
	makearef(headers, model.TABLE_LIST.headers);
	// both sides named alike (or ours a part of theirs) is no name of a side
	string own = UiOneLine(headers.("c" + data.own));
	string other = UiOneLine(headers.("c" + data.other));
	if (own == "" || own == other) other = "";
	if (other == "" || TouchLib_StartsWith(other, own)) own = "";
	out.headers.own = own;
	out.headers.other = other;
	out.headers.name = UiOneLine(headers.("c" + data.name));
	aref list;
	makearef(list, out.rows);
	for (int k = 0; k < GetAttributesNum(rows); k++)
	{
		aref row = GetAttributeN(rows, k);
		aref at = TouchLib_Add(list, "r");
		at.key = row.id;
		at.name = TouchLegLib_Cell(row, sti(data.name));
		at.ownText = TouchLegLib_Cell(row, sti(data.own));
		at.otherText = TouchLegLib_Cell(row, sti(data.other));
		at.own = TouchLegLib_Amount(row, sti(data.own), "the own amount column");
		at.other = TouchLegLib_Amount(row, sti(data.other), "the other amount column");
		at.can = CheckAttribute(row, "offers.activate") || CheckAttribute(row, "offers.select");
		at.reason = row.reason;
		at.selected = row.value == "1";
		at.pack = 1;
		if (sti(data.pack) >= 0) at.pack = UiAmount(TouchLegLib_Cell(row, sti(data.pack)), "the pack column");
		at.weight = TouchLegTrade_PieceWeight(data, row, at);
		if (sti(data.goods)) at.weight = at.weight + " " + UiText("transfer.cwt");
		aref image;
		makearef(image, at.image);
		TouchLegLib_Picture(row, sti(data.name), image);
		TouchLegTrade_Type(row, sti(data.name), at);
		at.buyText = TouchLegLib_Cell(row, sti(data.buy));
		at.sellText = TouchLegLib_Cell(row, sti(data.sell));
		if (UiHasAmount(at.buyText)) at.buy = UiAmount(at.buyText, "the price column");
		if (UiHasAmount(at.sellText)) at.sell = UiAmount(at.sellText, "the price column");
	}
}

// Our short words of the game's category tabs by position (the binding's data).
aref TouchLegTrade_Words(aref ui)
{
	aref words;
	makearef(words, ui.data.tabs);
	return words;
}

string TouchLegTrade_Buttons[2] = {"DELBODY_BUTTON", "SKULL_BUTTON"};

// The place's buttons, and its take-all: a chest's is hidden by the game, and the engine runs it in one script call.
void TouchLegTrade_Places(aref model, aref out)
{
	aref buttons;
	makearef(buttons, out.buttons);
	for (int i, string node: TouchLegTrade_Buttons)
		if (CheckAttribute(model, node))
		{
			object button = {node = node, label = model.(node).label, can = UiOffers(node, "press"),
			                 reason = model.(node).reason};
			aref at = TouchLib_Add(buttons, "b");
			CopyAttributes(at, &button);
		}
	string all = "GETALL_BUTTON";
	if (!CheckAttribute(model, all)) all = "ANDROID_TAKE_ALL";
	if (!CheckAttribute(model, all)) return;
	object take = {node = all, label = "", can = UiOffers(all, "press"), reason = model.(all).reason};
	if (all == "GETALL_BUTTON") take.label = model.(all).label;
	aref at;
	makearef(at, out.takeAll);
	CopyAttributes(at, &take);
}

// What we carry of what we can, from the game's "Трюм:\n189 / 199": out.load, out.capacity (-1: not told).
void TouchLegTrade_Hold(aref model, aref out)
{
	out.load = 0.0;
	out.capacity = -1.0;
	object figure;
	if (!TouchLib_Figure(TouchLegLib_Label(model, "CAPACITY"), &figure)) return;
	string value = figure.value;
	int slash = findSubStr(value, "/", 0);
	if (slash < 0) return;
	out.load = UiNumber(TouchLib_Cut(value, 0, slash), "CAPACITY");
	out.capacity = UiNumber(TouchLib_Cut(value, slash + 1, strlen(value)), "CAPACITY");
}

// Our money from the game's "Наши деньги 1 500 песо": out.purse (its figure as written, "" without), out.balance (-1:
// not told) and out.currency (the string key of what the merchant takes, "" money).
void TouchLegTrade_Purse(aref model, aref out)
{
	out.purse = "";
	out.balance = -1;
	out.currency = "";
	if (!CheckAttribute(model, "OUR_GOLD")) return;
	string gold = model.OUR_GOLD.label;
	out.balance = UiAmount(gold, "OUR_GOLD");
	if (CheckAttribute(model, "OUR_GOLD.context.currency"))
		if (model.OUR_GOLD.context.currency == "doubloons") out.currency = "trade.doubloons";
	int digit = TouchLib_FindDigit(gold, 0, true);
	int end = digit;
	while (end < strlen(gold) && findSubStr("0123456789 ", strcut(gold, end, end), 0) >= 0) end++;
	while (end > digit && strcut(gold, end - 1, end - 1) == " ") end--;
	out.purse = TouchLib_Cut(gold, digit, end);
}

void TouchLegTrade_Deal(aref model, int open, aref out)
{
	aref row = TouchLib_Item(out, "rows", open);
	object deal = {row = open, edit = UiOffers(TouchLegLib_Edit(model), "edit"), confirm = false, reason = "",
	               title = TouchLegLib_Label(model, "QTY_CAPTION"),
	               text = TouchLegTrade_WithoutPackLine(TouchLegLib_Label(model, "QTY_GOODS_INFO"), sti(row.pack))};
	if (CheckAttribute(model, "QTY_OK_BUTTON"))
	{
		deal.confirm = UiOffers("QTY_OK_BUTTON", "press");
		deal.reason = model.QTY_OK_BUTTON.reason;
	}
	aref at;
	makearef(at, out.deal);
	CopyAttributes(at, &deal);
}

void TouchAdpTrade_State(aref ui, aref out)
{
	aref data;
	makearef(data, ui.data);
	aref model = TouchLegLib_Model(ui);
	DeleteAttribute(out, "");
	out.title = TouchLegLib_Label(model, "TITLE");
	if (out.title == "") out.title = UiOneLine(TouchLegLib_Label(model, "STORECAPTION1"));
	out.money = sti(data.buy) >= 0;
	out.weighs = data.goods;
	out.pieces = data.pieces;
	TouchLegTrade_Rows(data, model, out);
	int open = TouchLegLib_Window(model);
	if (open >= 0) TouchLegTrade_Deal(model, open, out);
	// a side switch closes an open deal with the game's cancel first
	aref sides;
	makearef(sides, out.sides);
	TouchLegLib_Choices(model, data.owners, sides);
	out.switch = open < 0;
	if (open >= 0) out.switch = UiOffers("QTY_CANCEL_BUTTON", "press");
	TouchLegLib_Tabs(model, TouchLegTrade_Words(ui), out);
	out.carry = TouchLegLib_Label(model, "CAPACITY");
	out.place = TouchLegLib_Label(model, "STORE_CAPACITY");
	out.storage = TouchLegLib_Label(model, "STORAGE_INFO");
	out.info = TouchLegLib_Label(model, "INFO_TEXT");
	TouchLegTrade_Hold(model, out);
	TouchLegTrade_Purse(model, out);
	TouchLegTrade_Places(model, out);
	if (TouchLegTrade_Follow(ui, out)) UiAwait();
}

// The steps of the deal asked for, in this model; true while its answer needs another game frame.
bool TouchLegTrade_Follow(aref ui, aref out)
{
	string answer = UiAnswer();
	if (answer == "" || answer == "refused") TouchLegTrade_Step("", "", "", -1);
	string step = TouchLegTrade.step;
	string asked = TouchLegTrade.key;
	string name = TouchLegTrade.name;
	int side = sti(TouchLegTrade.side);
	if (step == "" || answer == "waiting") return false;
	int open = -1;
	if (CheckAttribute(out, "deal")) open = sti(out.deal.row);
	aref row;
	if (step == "side")
	{
		row = TouchLib_Item(out, "sides", side);
		if (sti(row.selected)) TouchLegTrade_Step("settle", "", name, -1);
		return true;
	}
	if (step == "open" || step == "quick")
	{
		if (open < 0) return true;
		// the window came (the game's choice of row stands where it opened another)
		TouchLegTrade_Step("", "", "", -1);
		if (side >= 0) return TouchLegTrade_Switch(ui, side); // a side chosen while it was on its way
		row = TouchLib_Item(out, "rows", open);
		if (step == "open" || row.key != asked || !sti(out.deal.edit) || !sti(out.deal.confirm)) return false;
		aref model = TouchLegLib_Model(ui);
		TouchLegLib_Confirm(model, sti(row.other), asked);
		return true;
	}
	// "again": the row of the confirmed deal, still selected; "settle": the goods of the old side's deal, else the row
	// the game selected (in the model after the bound one, or the bound one when the game changed nothing more)
	TouchLegTrade_Step("", "", "", -1);
	if (open >= 0 || (step == "settle" && name == "")) return false;
	int chosen = -1;
	for (int k = 0; k < TouchLib_Count(out, "rows"); k++)
	{
		row = TouchLib_Item(out, "rows", k);
		if (step == "again" && row.key == asked && sti(row.selected)) chosen = k;
		if (step == "settle" && (row.name == name || (chosen < 0 && sti(row.selected)))) chosen = k;
	}
	if (chosen < 0) return false;
	return TouchLegTrade_Activate(ui, chosen) && answer != "late";
}

// A row opened as the player's tap opens it (step "open" for its window); true when a window comes.
bool TouchLegTrade_Activate(aref ui, int k)
{
	if (!TouchLegLib_Open(TouchLegLib_Model(ui), k)) return false;
	TouchLegTrade_Step("open", TouchLegTrade_Key(ui, k), "", -1);
	return true;
}

// The open window closed with the game's cancel; the name of its goods, "" for none open.
string TouchLegTrade_Cancel(aref ui)
{
	int open = TouchLegLib_Cancel(TouchLegLib_Model(ui));
	if (open < 0) return "";
	return TouchLegLib_Cell(TouchLegTrade_Row(ui, open), sti(ui.data.name));
}

// A row tapped while the game switches sides (its rows are still the old side's) opens by its goods once it has.
void TouchAdpTrade_Deal(aref ui, int row)
{
	if (TouchLegTrade.step == "side" || TouchLegTrade.step == "settle")
	{
		TouchLegTrade.name = TouchLegLib_Cell(TouchLegTrade_Row(ui, row), sti(ui.data.name));
		return;
	}
	TouchLegTrade_Cancel(ui);
	TouchLegTrade_Activate(ui, row);
}

void TouchAdpTrade_Close(aref ui)
{
	TouchLegTrade_Step("", "", "", -1);
	TouchLegTrade_Cancel(ui);
}

void TouchAdpTrade_Commit(aref ui, int amount, bool stay)
{
	aref model = TouchLegLib_Model(ui);
	int open = TouchLegLib_Window(model);
	if (open < 0) UiError("trade: a deal is confirmed with no amount window open");
	aref row = TouchLegTrade_Row(ui, open);
	string key = row.id;
	TouchLegLib_Confirm(model, amount, key);
	if (stay) TouchLegTrade_Step("again", key, "", -1);
}

void TouchAdpTrade_Take(aref ui, int row)
{
	aref model = TouchLegLib_Model(ui);
	aref at = TouchLegTrade_Row(ui, row);
	if (TouchLegLib_Window(model) == row)
	{
		int all = TouchLegLib_Amount(at, sti(ui.data.other), "the other amount column");
		TouchLegLib_Confirm(model, all, at.id);
		return;
	}
	TouchLegTrade_Cancel(ui);
	if (TouchLegTrade_Activate(ui, row)) TouchLegTrade.step = "quick";
}

// A side chosen while a row's window is on its way waits for it: the game hides its window on a switch but keeps it
// armed, and the next opened row would confirm the old amount.
void TouchAdpTrade_Side(aref ui, int side)
{
	if (TouchLegTrade.step == "open" || TouchLegTrade.step == "quick")
	{
		TouchLegTrade.side = side;
		return;
	}
	TouchLegTrade_Switch(ui, side);
}

// The deal open, or a money trade's selected row, opens again on the new side.
bool TouchLegTrade_Switch(aref ui, int side)
{
	string name = TouchLegTrade_Cancel(ui);
	aref model = TouchLegLib_Model(ui);
	aref rows;
	makearef(rows, model.TABLE_LIST.rows);
	for (int k = 0; k < GetAttributesNum(rows); k++)
	{
		aref row = GetAttributeN(rows, k);
		if (name == "" && sti(ui.data.buy) >= 0 && row.value == "1") name = TouchLegLib_Cell(row, sti(ui.data.name));
	}
	TouchLegLib_Choose(model, ui.data.owners, side);
	TouchLegTrade_Step("side", "", name, side);
	return true;
}

void TouchAdpTrade_Category(aref ui, int tab)
{
	object state;
	TouchLegLib_Tabs(TouchLegLib_Model(ui), TouchLegTrade_Words(ui), &state);
	aref at = TouchLib_Item(&state, "tabs", tab);
	UiSend(at.node, "press", "");
}

void TouchAdpTrade_Press(aref ui, int button)
{
	object state;
	aref model = TouchLegLib_Model(ui);
	TouchLegTrade_Places(model, &state);
	aref at = TouchLib_Item(&state, "buttons", button);
	UiSend(at.node, "press", "");
}

void TouchAdpTrade_TakeAll(aref ui)
{
	object state;
	aref model = TouchLegLib_Model(ui);
	TouchLegTrade_Places(model, &state);
	UiSend(state.takeAll.node, "press", "");
}

// The game's exit closes its amount window when one is open and leaves only when none is.
void TouchAdpTrade_Leave(aref ui)
{
	TouchLegTrade_Step("", "", "", -1);
	string exit = "";
	if (CheckAttribute(ui, "model.EXIT_BTN")) exit = "EXIT_BTN";
	aref model = TouchLegLib_Model(ui);
	if (TouchLegLib_Window(model) >= 0) UiBack(exit);
	UiBack(exit);
}

void TouchAdpTrade_Terms(aref ui, int row, int amount, aref out)
{
	out.total = 0;
	if (sti(ui.data.buy) >= 0) out.total = TouchLegTrade_Total(ui, row, amount);
	out.weight = 0.0;
	if (sti(ui.data.goods)) out.weight = TouchLegTrade_Weight(ui, row, abs(amount));
	out.note = "";
}

// rule copy: store.c ChangeQTY_EDIT charges makeint(price * pieces / units + 0.5) for a part of a pack's price.
int TouchLegTrade_Total(aref ui, int row, int amount)
{
	aref at = TouchLegTrade_Row(ui, row);
	int column = sti(ui.data.sell);
	if (amount > 0) column = sti(ui.data.buy);
	int units = 1;
	if (sti(ui.data.pack) >= 0) units = UiAmount(TouchLegLib_Cell(at, sti(ui.data.pack)), "the pack column");
	if (units < 1) UiError("trade: a pack of " + units + " pieces");
	return makeint(makefloat(UiAmount(TouchLegLib_Cell(at, column), "the price column")) * abs(amount) / units + 0.5);
}

float TouchLegTrade_Weight(aref ui, int row, int amount)
{
	return makefloat(GetGoodWeightByType(TouchLegTrade_Good(TouchLegTrade_Row(ui, row)), amount));
}

// rule copy: store.c ChangeQTY_EDIT lets a purchase through while the hold takes its weight (GetGoodWeightByType) and
// the purse its total, and cuts it down past that; both grow with the amount, so the largest that passes is halved to.
void TouchAdpTrade_Reach(aref ui, int row, aref out)
{
	aref model = TouchLegLib_Model(ui);
	aref at = TouchLegTrade_Row(ui, row);
	int other = TouchLegLib_Amount(at, sti(ui.data.other), "the other amount column");
	out.give = TouchLegLib_Amount(at, sti(ui.data.own), "the own amount column");
	out.giveWhy = "";
	out.take = other;
	out.takeWhy = "";
	if (sti(ui.data.buy) < 0) return;
	if (!UiHasAmount(TouchLegLib_Cell(at, sti(ui.data.sell)))) out.give = 0;
	if (!UiHasAmount(TouchLegLib_Cell(at, sti(ui.data.buy))))
	{
		out.take = 0;
		return;
	}
	object hold;
	TouchLegTrade_Hold(model, &hold);
	TouchLegTrade_Purse(model, &hold);
	int most = TouchLegTrade_Largest(ui, row, other, &hold, true);
	if (most >= other) return;
	out.take = most;
	out.takeWhy = "trade.no_room";
	if (sti(ui.data.pieces)) out.takeWhy = "trade.no_carry";
	if (TouchLegTrade_Largest(ui, row, other, &hold, false) > most) out.takeWhy = "trade.no_money";
}

int TouchLegTrade_Largest(aref ui, int row, int stock, aref hold, bool paid)
{
	int low = 0;
	int high = stock;
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
	if (capacity >= 0.0 && stf(hold.load) + TouchLegTrade_Weight(ui, row, amount) > capacity) return false;
	if (!paid || sti(hold.balance) < 0) return true;
	return TouchLegTrade_Total(ui, row, amount) <= sti(hold.balance);
}
