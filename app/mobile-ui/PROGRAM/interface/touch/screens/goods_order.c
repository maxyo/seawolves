// SPDX-License-Identifier: GPL-3.0-only
// The purchase list (pack screen s-goods-order): per good of the list, what the ship carries and the standing order.
// A tapped row opens its amount; the amount set is the player's draft, which «Сохранить N» gives the game as the
// good's order and «Отмена» drops: a standing order is a setting, not a transfer.

#event_handler("UiBuild", "TouchGoodsOrder_Build");
#event_handler("UiTap", "TouchGoodsOrder_Tap");

// The screen's state while its window is open: the good opened (.open, its key, "" none), the draft {key, ship,
// value} and the adapter's state of the last build in .game (the taps act on what that build showed).
object TouchGoodsOrder;
object TouchGoodsOrder_View;

void TouchGoodsOrder_Build()
{
	aref ui = GetEventData();
	if (ui.screen != "goods_order") return;
	if (UiFirst())
	{
		DeleteAttribute(&TouchGoodsOrder, "");
		TouchGoodsOrder.open = "";
	}
	aref game;
	makearef(game, TouchGoodsOrder.game);
	DeleteAttribute(game, "");
	TouchAdpGoodsOrder_State(ui, game);
	// The game's own amount window, opened by the game: its good is the open one, set there.
	int window = sti(game.window);
	if (window >= 0)
	{
		aref shown = TouchLib_Item(game, "rows", window);
		TouchGoodsOrder.open = shown.key;
	}
	aref view;
	makearef(view, TouchGoodsOrder_View);
	DeleteAttribute(view, "");
	view.title = UiText("goods.title");
	TouchGoodsOrder_List(game, view);
	aref ships;
	makearef(ships, game.ships);
	TouchLib_Stepper(view, ships, sti(game.ship), sti(game.switch));
	TouchGoodsOrder_Contraband(game, view);
	TouchGoodsOrder_Primary(game, view);
	UiOn("header-back", "");
	UiShow(view);
}

// The draft of row `row` on the ship shown, else none.
bool TouchGoodsOrder_Drafted(aref game, aref row)
{
	if (!CheckAttribute(&TouchGoodsOrder, "draft")) return false;
	return TouchGoodsOrder.draft.key == row.key && sti(TouchGoodsOrder.draft.ship) == sti(game.ship);
}

// The amount a row shows: the draft set on it, else the game's order.
int TouchGoodsOrder_Amount(aref game, aref row)
{
	if (TouchGoodsOrder_Drafted(game, row)) return sti(TouchGoodsOrder.draft.value);
	return sti(row.order);
}

void TouchGoodsOrder_List(aref game, aref view)
{
	aref list;
	makearef(list, view.list);
	list.label = UiText("goods.good");
	list.cols.c0.label = UiText("goods.hold");
	list.cols.c0.icon = "capacity";
	list.cols.c1.label = UiText("goods.order");
	list.cols.c1.icon = "scroll";
	aref rows;
	makearef(rows, list.rows);
	bool window = sti(game.window) >= 0;
	for (int k = 0; k < TouchLib_Count(game, "rows"); k++)
	{
		aref row = TouchLib_Item(game, "rows", k);
		aref at = TouchLib_Add(rows, "r");
		int amount = TouchGoodsOrder_Amount(game, row);
		at.id = row.key;
		at.name = row.name;
		TouchLib_Copy(at, "image", row, "image");
		at.cells.c0 = row.hold;
		if (row.hold == "") at.cells.c0 = "—";
		at.cells.c1 = amount;
		at.selected = row.key == TouchGoodsOrder.open;
		at.disabled = !sti(row.can);
		at.reason = row.reason;
		if (sti(at.selected)) TouchGoodsOrder_Quantity(row, amount, window, at);
	}
	if (GetAttributesNum(rows) == 0) list.rows = "";
	UiOn("goods", "row");
	UiOn("goods", "change");
	UiOn("goods", "commit");
}

// The open good's amount. The game takes any order its window's field holds (only its arrows stop at a number of
// their own): the slider spans twice the larger of the order and what the ship carries, at least a thousand.
void TouchGoodsOrder_Quantity(aref row, int amount, bool window, aref at)
{
	int most = sti(row.order);
	if (row.hold != "" && sti(row.hold) > most) most = sti(row.hold);
	int top = (most * 2 + 99) / 100 * 100;
	if (top < 1000) top = 1000;
	object quantity = {value = amount, min = 0, max = top, label = UiText("goods.order"), unit = UiText("goods.pieces"),
	                   summary = "-", all = false, disabled = window || !sti(row.can), reason = row.reason};
	aref q;
	makearef(q, at.q);
	CopyAttributes(q, &quantity);
}

void TouchGoodsOrder_Contraband(aref game, aref view)
{
	view.contraband = "";
	if (!CheckAttribute(game, "contraband")) return;
	view.contraband.label = game.contraband.label;
	view.contraband.on = sti(game.contraband.on);
	view.contraband.disabled = !sti(game.contraband.can);
	view.contraband.reason = game.contraband.reason;
	UiOn("contraband", "change");
}

// «Сохранить N» while the open good's draft differs from the game's order, with «Отмена» beside it.
void TouchGoodsOrder_Primary(aref game, aref view)
{
	view.primary = "";
	view.actions = "";
	int k = TouchLib_Find(game, "rows", "key", TouchGoodsOrder.open);
	if (k < 0 || sti(game.window) >= 0) return;
	aref row = TouchLib_Item(game, "rows", k);
	if (!TouchGoodsOrder_Drafted(game, row)) return;
	int value = sti(TouchGoodsOrder.draft.value);
	if (value == sti(row.order)) return;
	object primary = {label = UiText("goods.save") + " " + value, sub = "", disabled = !sti(row.can),
	                  reason = row.reason, busy = UiPending("primary")};
	aref at;
	makearef(at, view.primary);
	CopyAttributes(at, &primary);
	UiOn("primary", "");
	object cancel = {can = true, reason = ""};
	aref actions;
	makearef(actions, view.actions);
	TouchLib_Button(actions, "deal-cancel", UiText("common.cancel"), "secondary", &cancel);
}

void TouchGoodsOrder_Tap()
{
	aref ui = GetEventData();
	if (ui.screen != "goods_order") return;
	string id = GetEventData(), verb = GetEventData(), value = GetEventData();
	aref game;
	makearef(game, TouchGoodsOrder.game);
	if (id == "header-back") { TouchGoodsOrder_Back(ui, game); return; }
	if (id == "header")
	{
		TouchGoodsOrder.open = "";
		DeleteAttribute(&TouchGoodsOrder, "draft");
		TouchAdpGoodsOrder_Ship(ui, TouchLib_StepTap(verb, sti(game.ship), TouchLib_Count(game, "ships"), "the ship"));
		UiHold();
		return;
	}
	if (id == "contraband") { TouchAdpGoodsOrder_Contraband(ui, value == "1"); UiHold(); return; }
	if (id == "deal-cancel") { DeleteAttribute(&TouchGoodsOrder, "draft"); UiRebuild(); return; }
	int k = TouchLib_Find(game, "rows", "key", TouchGoodsOrder.open);
	if (id == "primary")
	{
		if (UiPending("primary")) return; // saved, the game's answer not drawn yet
		if (k < 0 || !CheckAttribute(&TouchGoodsOrder, "draft")) UiError("goods order: a save with no amount set");
		TouchAdpGoodsOrder_Order(ui, k, sti(TouchGoodsOrder.draft.value));
		UiHold();
		return;
	}
	if (id != "goods") UiError("goods order: no tap of " + id);
	if (verb == "change" || verb == "commit")
	{
		if (k < 0) UiError("goods order: an amount set with no good open");
		TouchGoodsOrder.draft.key = TouchGoodsOrder.open;
		TouchGoodsOrder.draft.ship = game.ship;
		TouchGoodsOrder.draft.value = UiAmount(value, "the order");
		UiRebuild();
		return;
	}
	// The row by its identity: a row gone since the frame tapped ends the tap.
	if (TouchLib_Find(game, "rows", "key", value) < 0) return;
	DeleteAttribute(&TouchGoodsOrder, "draft");
	if (value == TouchGoodsOrder.open) TouchGoodsOrder.open = "";
	else TouchGoodsOrder.open = value;
	UiRebuild();
}

// Back closes the game's amount window it opened itself, then drops a changed amount, then leaves.
void TouchGoodsOrder_Back(aref ui, aref game)
{
	if (sti(game.window) >= 0) { TouchAdpGoodsOrder_Close(ui); UiHold(); return; }
	int k = TouchLib_Find(game, "rows", "key", TouchGoodsOrder.open);
	if (k >= 0 && CheckAttribute(&TouchGoodsOrder, "draft"))
	{
		aref row = TouchLib_Item(game, "rows", k);
		bool changed = TouchGoodsOrder_Drafted(game, row) && sti(TouchGoodsOrder.draft.value) != sti(row.order);
		DeleteAttribute(&TouchGoodsOrder, "draft");
		if (changed) { UiRebuild(); return; }
	}
	TouchAdpGoodsOrder_Leave(ui);
}
