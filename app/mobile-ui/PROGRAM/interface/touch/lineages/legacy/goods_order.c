// SPDX-License-Identifier: GPL-3.0-only
// The purchase list from the interface goodstransfer: the treasurer's standing orders per ship of the squadron.
// Implements contracts/goods_order.c, which it does not include: a segment cannot define what it declares extern.
// The game draws its goods as strings over a picture and opens a good's amount window by the mouse position over its
// icon; the engine lists them as ANDROID_GOODS_ORDER rows (the good's identity, its name, its picture) and sets an
// order through that window (`order <good>=<amount>`: the window opened, the amount entered, its OK pressed).

void TouchAdpGoodsOrder_State(aref ui, aref out)
{
	aref model = TouchLegLib_Model(ui);
	if (!CheckAttribute(model, "ANDROID_GOODS_ORDER.rows")) UiError("goods order: the engine lists no goods");
	ref hero = GetMainCharacter();
	// The ship the list is for: the window's companion slot (a global of the interface).
	int slot = UiGameInt("iCurCompanion");
	int index = GetCompanionIndex(hero, slot);
	if (index < 0) UiError("goods order: no ship in companion slot " + slot);
	ref captain = &Characters[index];
	string ship = captain.id;
	// The amount window the game has open: its good (a global of the interface).
	string open = "";
	if (CheckAttribute(model, "TG_CANCEL_BUTTON")) open = Goods[UiGameInt("iCurGoodIndex")].name;
	out.window = -1;
	aref rows;
	makearef(rows, out.rows);
	for (int k = 0; k < TouchLib_Count(model, "ANDROID_GOODS_ORDER.rows"); k++)
	{
		aref good = TouchLib_Item(model, "ANDROID_GOODS_ORDER.rows", k);
		string id = good.id;
		aref at = TouchLib_Add(rows, "r");
		at.key = id;
		at.name = good.label;
		aref image;
		makearef(image, at.image);
		TouchLegLib_Image(good, image);
		// The game's record of the order; without one it shows and buys none (GoodsTransfer.c).
		at.order = 0;
		if (CheckAttribute(hero, "TransferGoods." + ship + "." + id)) at.order = sti(hero.TransferGoods.(ship).(id));
		at.hold = "";
		if (CheckAttribute(captain, "Ship.Cargo.Goods." + id)) at.hold = captain.Ship.Cargo.Goods.(id);
		at.can = CheckAttribute(good, "offers.order");
		at.reason = good.reason;
		if (id == open) out.window = k;
	}
	if (open != "" && sti(out.window) < 0) UiError("goods order: the window of " + open + " is open on no row");
	// The game names the ship it shows (its CurCompanion string); its arrows step through the squadron's slots.
	out.ship = slot;
	for (int n = 0; n < GetCompanionQuantity(hero); n++)
	{
		string entry = "s" + n;
		out.ships.(entry).label = "";
	}
	string shown = "s" + slot;
	if (!CheckAttribute(out, "ships." + shown)) UiError("goods order: companion slot " + slot + " past the squadron");
	out.ships.(shown).label = GameInterface.Strings.CurCompanion;
	out.switch = UiOffers("BTN_CHOOSESHIP_LEFT", "press") || UiOffers("BTN_CHOOSESHIP_RIGHT", "press");
	if (!CheckAttribute(model, "CHECK_BUYCONTRABAND.rows.r0")) return;
	aref check;
	makearef(check, model.CHECK_BUYCONTRABAND.rows.r0);
	out.contraband.label = check.label;
	out.contraband.on = check.value == "1";
	out.contraband.can = sti(check.offers.toggle);
	out.contraband.reason = check.reason;
}

void TouchAdpGoodsOrder_Order(aref ui, int row, int amount)
{
	aref good = TouchLib_Item(TouchLegLib_Model(ui), "ANDROID_GOODS_ORDER.rows", row);
	object action = {node = "ANDROID_GOODS_ORDER", row = good.index, verb = "order", value = good.id + "=" + amount};
	UiSendTo(&action);
}

// The game's arrows move one slot at a time and skip a ship it may not use.
void TouchAdpGoodsOrder_Ship(aref ui, int ship)
{
	string arrow = "BTN_CHOOSESHIP_RIGHT";
	if (ship < UiGameInt("iCurCompanion")) arrow = "BTN_CHOOSESHIP_LEFT";
	UiSend(arrow, "press", "");
}

void TouchAdpGoodsOrder_Contraband(aref ui, bool on)
{
	aref model = TouchLegLib_Model(ui);
	string value = "0";
	if (on) value = "1";
	object action = {node = "CHECK_BUYCONTRABAND", row = model.CHECK_BUYCONTRABAND.rows.r0.index, verb = "toggle",
	                 value = value};
	UiSendTo(&action);
}

void TouchAdpGoodsOrder_Close(aref ui)
{
	UiSend("TG_CANCEL_BUTTON", "press", "");
}

void TouchAdpGoodsOrder_Leave(aref ui)
{
	UiBack(TouchLegLib_Exit(TouchLegLib_Model(ui), "EXIT_BTN"));
}
