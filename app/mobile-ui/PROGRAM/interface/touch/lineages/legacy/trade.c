// SPDX-License-Identifier: GPL-3.0-only
// The trade windows (chests and bodies, the storage, the store, the smugglers, the cannons, the items trade): the goods
// table TABLE_LIST with the columns of the binding's data, the amount window QTY_*, the side switch and the place's
// buttons. Implements contracts/trade.c.

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

// The good or item of a table row: the game keeps it beside the row it shows (GameInterface.TABLE_LIST.tr<n>.index).
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

// A row's trade type: its store's TradeType of the row's good (store.c's refStore, a global of the open interface),
// named by the binding's data.types.t<type>.{word (a string key), tone, profit}; a type it does not name is plain.
void TouchLegTrade_Type(aref types, aref row, aref out)
{
	string good = Goods[TouchLegTrade_Good(row)].name;
	string slot = "t" + UiGameInt("refStore.Goods." + good + ".TradeType");
	if (!CheckAttribute(types, slot)) return;
	aref type;
	makearef(type, types.(slot));
	out.type.word = UiText(type.word);
	out.type.tone = type.tone;
	out.type.profit = type.profit;
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

// The weight of what a row's figures are for: a pack of goods as the game counts it, or one of the game's items (the
// row's index is its Items[] entry; "" for one that weighs nothing, such as gold, whose weight the game writes as "-").
string TouchLegTrade_PieceWeight(aref data, aref row, aref at)
{
	if (sti(data.packWeight) >= 0) return UiOneLine(TouchLegLib_Cell(row, sti(data.packWeight)));
	if (sti(data.goods)) return "" + GetGoodWeightByType(TouchLegTrade_Good(row), sti(at.pack));
	if (!sti(data.items)) return "";
	int item = TouchLegTrade_Good(row);
	if (!CheckAttribute(&Items[item], "Weight")) UiError("trade: the game's item " + item + " has no weight");
	float weight = stf(Items[item].Weight);
	if (weight <= 0.0) return "";
	return Sprintf("%.2g", weight);
}

void TouchLegTrade_Rows(aref data, aref model, aref out)
{
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
	// an empty table is one widget without rows: its heads still name the sides
	if (!CheckAttribute(model, "TABLE_LIST.rows")) return;
	aref rows;
	makearef(rows, model.TABLE_LIST.rows);
	aref list;
	makearef(list, out.rows);
	aref types;
	makearef(types, data.types);
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
		if (GetAttributesNum(types) > 0) TouchLegTrade_Type(types, row, at);
		at.about = TouchLegTrade_About(data, headers, row);
		at.buyText = TouchLegLib_Cell(row, sti(data.buy));
		at.sellText = TouchLegLib_Cell(row, sti(data.sell));
		if (UiHasAmount(at.buyText)) at.buy = UiAmount(at.buyText, "the price column");
		if (UiHasAmount(at.sellText)) at.sell = UiAmount(at.sellText, "the price column");
	}
}

// The game's further columns of a row as "caption value" (the cannons' range, damage and reload): the binding's
// data.details, none without it.
string TouchLegTrade_About(aref data, aref headers, aref row)
{
	string about = "";
	if (!CheckAttribute(data, "details")) return about;
	aref details;
	makearef(details, data.details);
	for (int i = 0; i < GetAttributesNum(details); i++)
	{
		aref detail = GetAttributeN(details, i);
		int column = sti(GetAttributeValue(detail));
		string cell = TouchLegLib_Cell(row, column);
		if (cell == "") continue;
		if (about != "") about += " · ";
		about += UiOneLine(headers.("c" + column)) + " " + cell;
	}
	return about;
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
			aref at = TouchLib_Add(buttons, "b");
			at.node = node;
			TouchLegLib_Button(model, node, at);
		}
	string all = "GETALL_BUTTON";
	if (!CheckAttribute(model, all)) all = "ANDROID_TAKE_ALL";
	if (!CheckAttribute(model, all)) return;
	aref take;
	makearef(take, out.takeAll);
	take.node = all;
	TouchLegLib_Button(model, all, take);
	if (all != "GETALL_BUTTON") take.label = "";
}

// The header's figures as the game writes them: what we carry ("Трюм:\n189 / 199", during a deal the game's figure
// after it), the place's load, else the storage's named by the place ("Склад"), and our money. figures.f<k>.{kind
// ("carry", "place", "purse"), label, value, currency (purse: the string key of what is paid, "" money)}.
void TouchLegTrade_Figures(aref model, aref out)
{
	aref figures;
	makearef(figures, out.figures);
	object figure;
	if (TouchLib_Figure(TouchLegLib_Label(model, "CAPACITY"), &figure))
		TouchLegTrade_Figure(figures, "carry", "", figure.value);
	string place = UiOneLine(TouchLegLib_Label(model, "STORE_CAPACITY"));
	if (TouchLib_Figure(place, &figure)) TouchLegTrade_Figure(figures, "place", figure.label, figure.value);
	else
	{
		if (TouchLib_Figure(TouchLegLib_Label(model, "STORAGE_INFO"), &figure))
		{
			if (place == "") place = figure.label;
			TouchLegTrade_Figure(figures, "place", place, figure.value);
		}
	}
	object purse;
	if (!TouchLegLib_Purse(model, &purse)) return;
	aref at = TouchLegTrade_Figure(figures, "purse", "", purse.value);
	at.currency = purse.currency;
}

aref TouchLegTrade_Figure(aref figures, string kind, string label, string value)
{
	aref at = TouchLib_Add(figures, "f");
	at.kind = kind;
	at.label = label;
	at.value = value;
	return at;
}

void TouchLegTrade_Deal(aref data, aref model, int open, aref out)
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
	TouchLegTrade_Answer(data, model, row, at);
}

// What the game's window holds now, as its ChangeQTY_EDIT has clamped and counted it: our side after the deal
// (QTY_INFO_SHIP_QTY), and its result (QTY_Result, empty while nothing is set): in a store "Деньги 25, вес 5 ц", in a
// window without money (a looted town) "Вес 5 ц".
void TouchLegTrade_Answer(aref data, aref model, aref row, aref deal)
{
	if (!CheckAttribute(model, "QTY_INFO_SHIP_QTY")) UiError("trade: the amount window has no QTY_INFO_SHIP_QTY");
	deal.amount = UiAmount(model.QTY_INFO_SHIP_QTY.label, "QTY_INFO_SHIP_QTY") - sti(row.own);
	deal.total = 0;
	deal.weight = "";
	deal.note = "";
	string result = TouchLegLib_Label(model, "QTY_Result");
	if (!UiHasAmount(result)) return;
	int from = 0;
	if (sti(data.buy) >= 0)
	{
		deal.total = UiAmount(result, "QTY_Result");
		from = findSubStr(result, ",", 0);
		if (from < 0) UiError("trade: no weight in the game's deal result '" + result + "'");
	}
	int digit = TouchLib_FindDigit(result, from, true);
	if (digit < 0) UiError("trade: no weight in the game's deal result '" + result + "'");
	int end = digit;
	while (end < strlen(result) && findSubStr("0123456789.", strcut(result, end, end), 0) >= 0) end++;
	deal.weight = TouchLib_Cut(result, digit, end);
}

// Looting a town (ColonyCapture.c): «take the most valuable» fills the hold with the goods worth most per cwt first.
// The order is ours (the ANDROID_LOOT cost of a pack by the game's pack weight); the amounts are the game's: of each
// good what its GetGoodQuantityByWeight fits in the room left, weighing what its GetGoodWeightByType says, the room
// being the hold's figures. The plan goes to the game's own amount window, good by good (ANDROID_LOOT, xinterface),
// which clamps each. best.{label ("" ours), weight (what the plan fills), can, reason}; place.{title (the town: the
// game's caption up to its colon), room}.
object TouchLegTrade_Best; // .plan: the last State's, "<tr>=<amount>,…"

void TouchLegTrade_Loot(aref ui, aref model, aref out)
{
	TouchLegTrade_Best.plan = "";
	object best = {label = "", weight = "", can = false, reason = UiText("trade.no_room")};
	int room = TouchLegTrade_Room(model);
	if (room >= 0)
	{
		string title = out.title;
		int colon = findSubStr(title, ":", 0);
		if (colon >= 0) title = TouchLib_Cut(title, 0, colon);
		out.place.title = title;
		out.place.room = room;
	}
	object costs;
	if (CheckAttribute(model, "ANDROID_LOOT.rows"))
	{
		aref facts;
		makearef(facts, model.ANDROID_LOOT.rows);
		for (int f = 0; f < GetAttributesNum(facts); f++)
		{
			aref fact = GetAttributeN(facts, f);
			costs.(fact.id) = fact.context.cost;
		}
	}
	else
	{
		if (TouchLib_Count(out, "rows") > 0) UiError("trade: the town's goods have no ANDROID_LOOT values");
		best.reason = UiText("items.unavailable");
	}
	object ranked;
	for (int k = 0; k < TouchLib_Count(out, "rows"); k++)
	{
		aref row = TouchLib_Item(out, "rows", k);
		aref cells = TouchLegTrade_Row(ui, k);
		string tr = "tr" + (sti(cells.index) + 1);
		float pack = UiNumber(TouchLegLib_Cell(cells, sti(ui.data.packWeight)), "the pack weight column");
		if (sti(row.other) <= 0 || pack <= 0.0 || !CheckAttribute(&costs, tr)) continue;
		aref at = TouchLib_Add(&ranked, "r");
		at.k = k;
		at.worth = makeint(UiNumber(costs.(tr), "an ANDROID_LOOT cost") * 100.0 / pack);
	}
	UiOrder(&ranked, "worth", true);
	int weight = 0;
	for (int n = 0; n < GetAttributesNum(&ranked); n++)
	{
		aref pick = GetAttributeN(&ranked, n);
		row = TouchLib_Item(out, "rows", sti(pick.k));
		cells = TouchLegTrade_Row(ui, sti(pick.k));
		int good = TouchLegTrade_Good(cells);
		int amount = GetGoodQuantityByWeight(good, room);
		if (amount > sti(row.other)) amount = sti(row.other);
		if (amount <= 0) continue; // not a pack of it fits: a lighter good further down may
		int moved = GetGoodWeightByType(good, amount);
		room -= moved;
		weight += moved;
		if (TouchLegTrade_Best.plan != "") TouchLegTrade_Best.plan = TouchLegTrade_Best.plan + ",";
		TouchLegTrade_Best.plan = TouchLegTrade_Best.plan + (sti(cells.index) + 1) + "=" + amount;
	}
	if (TouchLegTrade_Best.plan != "")
	{
		best.weight = weight;
		best.can = true; // the facts are read only: the engine takes the plan by their node
		best.reason = "";
	}
	aref at;
	makearef(at, out.best);
	CopyAttributes(at, &best);
}

// What the hold still takes by the game's figures "Трюм:\n201 / 240"; -1 unknown.
int TouchLegTrade_Room(aref model)
{
	object figure;
	if (!TouchLib_Figure(TouchLegLib_Label(model, "CAPACITY"), &figure)) return -1;
	string value = figure.value;
	int slash = findSubStr(value, "/", 0);
	if (slash < 0) return -1;
	int room = UiAmount(TouchLib_Cut(value, slash + 1, strlen(value)), "the hold's capacity") -
	           UiAmount(TouchLib_Cut(value, 0, slash), "the hold's load");
	if (room < 0) return 0;
	return room;
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
	aref types;
	makearef(types, data.types);
	out.types = GetAttributesNum(types) > 0;
	TouchLegTrade_Rows(data, model, out);
	int open = TouchLegLib_Window(model);
	if (open >= 0) TouchLegTrade_Deal(data, model, open, out);
	// a side switch closes an open deal with the game's cancel first
	aref sides;
	makearef(sides, out.sides);
	TouchLegLib_Choices(model, data.owners, sides);
	out.switch = open < 0;
	if (open >= 0) out.switch = UiOffers("QTY_CANCEL_BUTTON", "press");
	TouchLegLib_Tabs(model, TouchLegTrade_Words(ui), out);
	out.storage = TouchLegLib_Label(model, "STORAGE_INFO");
	out.info = TouchLegLib_Label(model, "INFO_TEXT");
	TouchLegTrade_Figures(model, out);
	TouchLegTrade_Places(model, out);
	if (CheckAttribute(data, "loot")) TouchLegTrade_Loot(ui, model, out);
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

// The game's window takes the amount and answers it clamped (State's deal.amount).
void TouchAdpTrade_Offer(aref ui, int amount)
{
	aref model = TouchLegLib_Model(ui);
	int open = TouchLegLib_Window(model);
	if (open < 0) UiError("trade: an amount is offered with no amount window open");
	aref row = TouchLegTrade_Row(ui, open);
	TouchLegLib_Offer(model, amount, row.id);
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

// The plan of the last State, in one action the engine runs on the game's window.
void TouchAdpTrade_TakeBest(aref ui)
{
	if (TouchLegTrade_Best.plan == "") UiError("trade: the most valuable is taken with no plan");
	UiSend("ANDROID_LOOT", "take", TouchLegTrade_Best.plan);
}

// The game's exit closes its amount window when one is open and leaves only when none is.
void TouchAdpTrade_Leave(aref ui)
{
	TouchLegTrade_Step("", "", "", -1);
	aref model = TouchLegLib_Model(ui);
	string leave = TouchLegLib_Exit(model, "EXIT_BTN");
	if (TouchLegLib_Window(model) >= 0) UiBack(leave);
	UiBack(leave);
}
