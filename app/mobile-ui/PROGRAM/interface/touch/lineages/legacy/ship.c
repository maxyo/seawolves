// SPDX-License-Identifier: GPL-3.0-only
// The ship from the interface ship: the squadron's SHIPS_SCROLL, the shown captain's ship (ship.c xi_refCharacter):
// its name EDIT_BOX, the figures TABLE_OTHER, the crew TABLE_CREW and CREW_MORALE_TEXT, the hold TABLE_LIST, the food
// and upkeep lines, and the windows the game opens over them: the good's QTY_WINDOW (ChangeQTY_EDIT takes the amount
// out of the hold while it is set, iShipQty keeps what there was), the question REMOVE_OFFICER_WINDOW (the morale's
// price, the exchange), the shares' PARTITION_WINDOW and the guns' CANNONS_WINDOW. A window is open while its nodes
// are in the model. Implements contracts/ship.c.

// The sides of CANNONS_WINDOW: ours, the letter of their nodes, the game's name of the side.
string TouchLegShip_Sides[4] = {"bow", "port", "starboard", "stern"};
string TouchLegShip_Letters[4] = {"F", "L", "R", "B"};
string TouchLegShip_Borts[4] = {"cannonf", "cannonl", "cannonr", "cannonb"};
string TouchLegShip_Closers[7] = {"QTY_CANCEL_BUTTON", "CANNONS_CANCEL", "CANNONS_EXIT_BTN", "PARTITION_CANCEL",
                                  "PARTITION_EXIT_BTN", "REMOVE_CANCEL_OFFICER", "REMOVE_EXIT_BTN"};

// The captain whose ship the window shows.
ref TouchLegShip_Captain()
{
	return GetCharacter(UiGameInt("xi_refCharacter.index"));
}

void TouchAdpShip_State(aref ui, aref out)
{
	aref model = TouchLegLib_Model(ui);
	DeleteAttribute(out, "");
	out.title = TouchLib_Trim(UiOneLine(TouchLegLib_Label(model, "TITLE")));
	out.sections = TouchLegLib_Sections(model);
	aref ships;
	makearef(ships, out.ships);
	TouchLegLib_Choices(model, "SHIPS_SCROLL", ships);
	ref chr = TouchLegShip_Captain();
	// OnShipScrollChange fills the figures only for a captain with a ship.
	if (TouchLib_Count(model, "TABLE_OTHER.rows") > 0)
	{
		TouchLegShip_Ship(model, chr, out);
		aref quality;
		makearef(quality, out.quality);
		TouchLegLib_Crew(model, "TABLE_CREW", "CREW_MORALE_TEXT", chr, quality);
		TouchLegShip_Button(model, "CREW_MORALE_BUTTON", out, "morale");
	}
	TouchLegShip_Button(model, "CREW_PARTITION", out, "shares");
	TouchLegShip_Facts(model, out);
	TouchLegShip_Goods(model, out);
	TouchLegShip_Drop(model, out);
	TouchLegShip_Question(model, out);
	TouchLegShip_Cannons(model, chr, out);
}

void TouchLegShip_Ship(aref model, ref chr, aref out)
{
	ref hero = GetMainCharacter();
	ref real = GetRealShip(sti(chr.Ship.Type));
	object ship = {name = "", type = XI_ConvertString(real.BaseName),
	               rank = TouchLib_Trim(TouchLegLib_Label(model, "SHIP_RANK")),
	               captain = TouchLib_Trim(TouchLegLib_Label(model, "HERO_NAME")), companion = chr.id != hero.id,
	               rename = false, exchange = false};
	if (CheckAttribute(model, "EDIT_BOX"))
	{
		ship.name = TouchLib_Trim(model.EDIT_BOX.value);
		// CheckForRename gives the field focus only for a ship of the hero's own company.
		ship.rename = GetShipRemovable(chr) && UiOffers("EDIT_BOX", "edit");
	}
	// rule copy: ship.c ShipChange's outer condition. The game keeps it only inside that handler, which opens no
	// window when it fails, so the button cannot be offered from the game's word; every other refusal is the game's
	// own question. To go with a script fix that lets the game say whether it offers the exchange.
	if (!CheckAttribute(hero, "SystemInfo.ShowShip") && chr.id != hero.id && sti(hero.Ship.Type) != SHIP_NOTUSED)
		ship.exchange = TouchLegShip_Current(model) >= 0;
	aref at;
	makearef(at, out.ship);
	CopyAttributes(at, &ship);
	aref image;
	makearef(image, out.ship.image);
	if (!TouchLegLib_NodeImage(model, "SHIP_BIG_PICTURE", image)) DeleteAttribute(out, "ship.image");
	aref figures;
	makearef(figures, out.figures);
	object keys = {Hull = "hull", Sails = "sails", Speed = "speed", Maneuver = "turn", AgainstWind = "upwind",
	               Capacity = "capacity"};
	for (int k = 0; k < TouchLib_Count(model, "TABLE_OTHER.rows"); k++)
	{
		aref row = TouchLib_Item(model, "TABLE_OTHER.rows", k);
		string id = row.id;
		string text = TouchLegLib_Cell(row, 2);
		if (CheckAttribute(&keys, id))
		{
			aref figure = TouchLib_Add(figures, "f");
			figure.key = keys.(id);
			figure.label = TouchLib_Trim(TouchLegLib_Cell(row, 1));
			TouchLegShip_Pair(text, id, figure);
		}
		if (id == "Crew")
		{
			// "count : least / full"
			out.crew.count = UiAmount(text, "the crew");
			int colon = findSubStr(text, ":", 0);
			if (colon < 0) UiError("ship: the crew row '" + text + "' has no limits");
			aref limits;
			makearef(limits, out.crew.limits);
			TouchLegShip_Pair(TouchLib_Cut(text, colon + 1, strlen(text)), id, limits);
			out.crew.least = limits.now;
			out.crew.full = limits.most;
			DeleteAttribute(out, "crew.limits");
		}
		if (id == "CannonType")
		{
			out.guns.label = TouchLib_Trim(TouchLegLib_Cell(row, 1));
			out.guns.text = TouchLib_Trim(text);
			out.guns.can = CheckAttribute(row, "offers.activate");
		}
		if (id == "sCannons")
		{
			int slash = findSubStr(text, "/", 0);
			if (slash < 0) slash = strlen(text);
			out.guns.caliber = TouchLib_Trim(TouchLib_Cut(text, 0, slash));
		}
	}
}

// "now / most" (most -1 without the slash) of the game's text.
void TouchLegShip_Pair(string text, string what, aref out)
{
	out.now = UiAmount(text, what);
	out.most = -1;
	int slash = findSubStr(text, "/", 0);
	if (slash >= 0) out.most = UiAmount(TouchLib_Cut(text, slash + 1, strlen(text)), what);
}

// k of the squadron's ship shown, -1 when its row offers no exchange (ShipChange is the row's activate).
int TouchLegShip_Current(aref model)
{
	for (int k = 0; k < TouchLib_Count(model, "SHIPS_SCROLL.rows"); k++)
	{
		aref row = TouchLib_Item(model, "SHIPS_SCROLL.rows", k);
		if (row.value == "1" && CheckAttribute(row, "offers.activate")) return k;
	}
	return -1;
}

void TouchLegShip_Button(aref model, string node, aref out, string key)
{
	if (!CheckAttribute(model, node)) return;
	object button = {label = TouchLib_Trim(model.(node).label), can = UiOffers(node, "press"),
	                 reason = model.(node).reason};
	aref at;
	makearef(at, out.(key));
	CopyAttributes(at, &button);
}

void TouchLegShip_Facts(aref model, aref out)
{
	aref facts;
	makearef(facts, out.facts);
	aref fact;
	string food = UiOneLine(TouchLegLib_Label(model, "FOOD_SHIP"));
	if (food != "")
	{
		fact = TouchLib_Add(facts, "f");
		fact.icon = "food";
		fact.title = food;
		fact.meta = UiOneLine(TouchLegLib_Label(model, "FOOD"));
	}
	string money = UiOneLine(TouchLegLib_Label(model, "MONEY_SHIP"));
	if (money == "") return;
	fact = TouchLib_Add(facts, "f");
	fact.icon = "money";
	fact.title = money;
	fact.meta = "";
	int colon = findSubStr(money, ":", 0);
	if (colon < 0) return;
	fact.title = TouchLib_AfterColon(money);
	fact.meta = TouchLib_Trim(TouchLib_Cut(money, 0, colon));
}

// TABLE_LIST (FillGoodsTable): name, quantity, weight, pack, pack weight.
void TouchLegShip_Goods(aref model, aref out)
{
	object heads = {name = "", count = "", weight = "", pack = "", packWeight = ""};
	if (CheckAttribute(model, "TABLE_LIST.headers.c4"))
	{
		heads.name = UiOneLine(model.TABLE_LIST.headers.c0);
		heads.count = UiOneLine(model.TABLE_LIST.headers.c1);
		heads.weight = UiOneLine(model.TABLE_LIST.headers.c2);
		heads.pack = UiOneLine(model.TABLE_LIST.headers.c3);
		heads.packWeight = UiOneLine(model.TABLE_LIST.headers.c4);
	}
	aref to;
	makearef(to, out.heads);
	CopyAttributes(to, &heads);
	aref goods;
	makearef(goods, out.goods);
	for (int k = 0; k < TouchLib_Count(model, "TABLE_LIST.rows"); k++)
	{
		aref row = TouchLib_Item(model, "TABLE_LIST.rows", k);
		object good = {key = row.id, label = TouchLib_Trim(TouchLegLib_Cell(row, 0)),
		               count = TouchLib_Trim(TouchLegLib_Cell(row, 1)), weight = TouchLib_Trim(TouchLegLib_Cell(row, 2)),
		               pack = TouchLib_Trim(TouchLegLib_Cell(row, 3)), packWeight = TouchLib_Trim(TouchLegLib_Cell(row, 4)),
		               can = CheckAttribute(row, "offers.activate")};
		aref at = TouchLib_Add(goods, "g");
		CopyAttributes(at, &good);
		aref image;
		makearef(image, at.image);
		TouchLegLib_Picture(row, 0, image);
		if (!CheckAttribute(at, "image.kind")) DeleteAttribute(at, "image");
	}
}

void TouchLegShip_Drop(aref model, aref out)
{
	if (!CheckAttribute(model, "QTY_OK_BUTTON") && !CheckAttribute(model, "QTY_CANCEL_BUTTON")) return;
	out.drop.label = TouchLib_Trim(UiOneLine(TouchLegLib_Label(model, "QTY_CAPTION")));
	out.drop.total = UiGameInt("iShipQty");
	out.drop.dropping = 0;
	out.drop.can = false;
	if (CheckAttribute(model, "QTY_EDIT"))
	{
		out.drop.can = UiOffers("QTY_EDIT", "edit");
		// An empty field (the player cleared it) drops nothing.
		if (UiHasAmount(model.QTY_EDIT.value)) out.drop.dropping = UiAmount(model.QTY_EDIT.value, "QTY_EDIT");
	}
	out.drop.ok.can = false;
	out.drop.ok.reason = "";
	if (CheckAttribute(model, "QTY_OK_BUTTON"))
	{
		out.drop.ok.can = UiOffers("QTY_OK_BUTTON", "press");
		out.drop.ok.reason = model.QTY_OK_BUTTON.reason;
	}
	aref drop;
	makearef(drop, out.drop);
	TouchLegShip_Button(model, "QTY_BOMB_BUTTON", drop, "mine");
}

void TouchLegShip_Question(aref model, aref out)
{
	string accept = "";
	if (CheckAttribute(model, "REMOVE_ACCEPT_OFFICER") || CheckAttribute(model, "REMOVE_CANCEL_OFFICER"))
	{
		out.question.kind = "question";
		out.question.title = TouchLib_Trim(UiOneLine(TouchLegLib_Label(model, "REMOVE_WINDOW_CAPTION")));
		out.question.text = TouchLib_Trim(UiOneLine(TouchLegLib_Label(model, "REMOVE_WINDOW_TEXT")));
		accept = "REMOVE_ACCEPT_OFFICER";
	}
	if (CheckAttribute(model, "PARTITION_OK") || CheckAttribute(model, "PARTITION_CANCEL"))
	{
		out.question.kind = "shares";
		out.question.title = TouchLib_Trim(UiOneLine(TouchLegLib_Label(model, "PARTITION_WINDOW_CAPTION")));
		out.question.text = "";
		aref lines;
		makearef(lines, out.question.lines);
		TouchLegLib_Lines(model, "PARTITION_WINDOW_TEXT", lines, "l");
		accept = "PARTITION_OK";
	}
	if (accept == "") return;
	out.question.accept.label = "";
	out.question.accept.can = false;
	out.question.accept.reason = "";
	if (!CheckAttribute(model, accept)) return;
	out.question.accept.label = TouchLib_Trim(model.(accept).label);
	out.question.accept.can = UiOffers(accept, "press");
	out.question.accept.reason = model.(accept).reason;
}

void TouchLegShip_Cannons(aref model, ref chr, aref out)
{
	if (!CheckAttribute(model, "CANNONS_OK") && !CheckAttribute(model, "CANNONS_CANCEL")) return;
	aref cannons;
	makearef(cannons, out.cannons);
	cannons.title = TouchLib_Trim(UiOneLine(TouchLegLib_Label(model, "CANNONS_CAPTION")));
	cannons.fitted = TouchLib_Trim(UiOneLine(TouchLegLib_Label(model, "CANNONS_TEXT")));
	cannons.stock.label = "";
	cannons.stock.count = "";
	if (CheckAttribute(model, "CANNONS_TABLE.headers.c1"))
	{
		cannons.stock.label = UiOneLine(model.CANNONS_TABLE.headers.c0);
		cannons.stock.count = UiOneLine(model.CANNONS_TABLE.headers.c1);
	}
	aref calibres;
	makearef(calibres, cannons.calibres);
	for (int k = 0; k < TouchLib_Count(model, "CANNONS_TABLE.rows"); k++)
		TouchLegShip_Calibre(model, TouchLib_Item(model, "CANNONS_TABLE.rows", k), TouchLib_Add(calibres, "c"));
	aref sides;
	makearef(sides, cannons.sides);
	for (int s = 0; s < 4; s++)
	{
		string letter = TouchLegShip_Letters[s];
		string qty = "CANNONS_QTY_" + letter;
		bool can = false;
		if (CheckAttribute(model, "CANNONS_LEFT_" + letter)) can = UiOffers("CANNONS_LEFT_" + letter, "press");
		if (CheckAttribute(model, "CANNONS_RIGHT_" + letter))
			can = can || UiOffers("CANNONS_RIGHT_" + letter, "press");
		object side = {key = TouchLegShip_Sides[s], now = UiAmount(TouchLegLib_Label(model, qty), qty),
		               most = GetBortCannonsQtyMax(chr, TouchLegShip_Borts[s]), can = can};
		aref at = TouchLib_Add(sides, "s");
		CopyAttributes(at, &side);
	}
	TouchLegShip_Button(model, "CANNONS_SET_ALL", cannons, "fit");
	TouchLegShip_Button(model, "CANNONS_REMOVE_ALL", cannons, "remove");
	TouchLegShip_Button(model, "CANNONS_OK", cannons, "ok");
}

// A row of the guns in the hold (FillCannonsTable): the gun, how many, and its range, damage and reload under the
// game's headings.
void TouchLegShip_Calibre(aref model, aref row, aref at)
{
	string sub = "";
	for (int column = 2; column <= 4; column++)
	{
		string value = TouchLib_Trim(TouchLegLib_Cell(row, column));
		if (value == "") continue;
		string head = "c" + column;
		string heading = "";
		if (CheckAttribute(model, "CANNONS_TABLE.headers." + head)) heading = UiOneLine(model.CANNONS_TABLE.headers.(head));
		if (sub != "") sub += " · ";
		if (heading != "") sub += heading + " ";
		sub += value;
	}
	object calibre = {label = TouchLib_Trim(TouchLegLib_Cell(row, 0)), sub = sub,
	                  count = TouchLib_Trim(TouchLegLib_Cell(row, 1)), selected = row.value == "1",
	                  can = CheckAttribute(row, "offers.select")};
	CopyAttributes(at, &calibre);
	aref image;
	makearef(image, at.image);
	TouchLegLib_Picture(row, 0, image);
	if (!CheckAttribute(at, "image.kind")) DeleteAttribute(at, "image");
}

void TouchAdpShip_Ship(aref ui, int ship)
{
	TouchLegLib_Choose(TouchLegLib_Model(ui), "SHIPS_SCROLL", ship);
}

void TouchAdpShip_Rename(aref ui, string title)
{
	UiEditText("EDIT_BOX", title);
}

void TouchAdpShip_Exchange(aref ui)
{
	aref model = TouchLegLib_Model(ui);
	int k = TouchLegShip_Current(model);
	if (k < 0) UiError("ship: the ship shown offers no exchange");
	aref row = TouchLib_Item(model, "SHIPS_SCROLL.rows", k);
	object action = {node = "SHIPS_SCROLL", row = row.index, verb = "activate", value = row.id};
	UiSendTo(&action);
}

void TouchAdpShip_Guns(aref ui)
{
	aref model = TouchLegLib_Model(ui);
	for (int k = 0; k < TouchLib_Count(model, "TABLE_OTHER.rows"); k++)
	{
		aref row = TouchLib_Item(model, "TABLE_OTHER.rows", k);
		if (row.id == "CannonType")
		{
			object action = {node = "TABLE_OTHER", row = row.index, verb = "activate", value = row.id};
			UiSendTo(&action);
			return;
		}
	}
	UiError("ship: no guns row to open");
}

void TouchAdpShip_Morale(aref ui)
{
	UiSend("CREW_MORALE_BUTTON", "press", "");
}

void TouchAdpShip_Shares(aref ui)
{
	UiSend("CREW_PARTITION", "press", "");
}

void TouchAdpShip_Accept(aref ui)
{
	if (CheckAttribute(ui, "model.PARTITION_OK")) { UiSend("PARTITION_OK", "press", ""); return; }
	UiSend("REMOVE_ACCEPT_OFFICER", "press", "");
}

// The open window's own way out: its cancel, else its ✕.
void TouchAdpShip_Close(aref ui)
{
	aref model = TouchLegLib_Model(ui);
	for (int k = 0; k < 7; k++)
	{
		string node = TouchLegShip_Closers[k];
		if (CheckAttribute(model, node))
		{
			if (UiOffers(node, "press")) { UiSend(node, "press", ""); return; }
		}
	}
	UiError("ship: no window to close");
}

void TouchAdpShip_Good(aref ui, int good)
{
	aref row = TouchLib_Item(TouchLegLib_Model(ui), "TABLE_LIST.rows", good);
	object action = {node = "TABLE_LIST", row = row.index, verb = "activate", value = row.id};
	UiSendTo(&action);
}

void TouchAdpShip_Drop(aref ui, int amount)
{
	UiSend("QTY_EDIT", "edit", "" + amount);
}

void TouchAdpShip_Overboard(aref ui)
{
	UiSend("QTY_OK_BUTTON", "press", "");
}

void TouchAdpShip_Mine(aref ui)
{
	UiSend("QTY_BOMB_BUTTON", "press", "");
}

void TouchAdpShip_Calibre(aref ui, int calibre)
{
	TouchLegLib_Choose(TouchLegLib_Model(ui), "CANNONS_TABLE", calibre);
}

// The arrows of a side move its guns by one per press (ship.c ChangeCannonNum); SetCannonsToBort stops them where the
// ports or the hold's guns end, and the build shows where it stopped.
void TouchAdpShip_Side(aref ui, int side, int guns)
{
	aref model = TouchLegLib_Model(ui);
	string letter = TouchLegShip_Letters[side];
	string qty = "CANNONS_QTY_" + letter;
	int now = UiAmount(TouchLegLib_Label(model, qty), qty);
	string arrow = "CANNONS_RIGHT_" + letter;
	if (guns < now) arrow = "CANNONS_LEFT_" + letter;
	for (int i = 0; i < abs(guns - now); i++) UiSend(arrow, "press", "");
}

void TouchAdpShip_Fit(aref ui)
{
	UiSend("CANNONS_SET_ALL", "press", "");
}

void TouchAdpShip_RemoveGuns(aref ui)
{
	UiSend("CANNONS_REMOVE_ALL", "press", "");
}

void TouchAdpShip_GunsOk(aref ui)
{
	UiSend("CANNONS_OK", "press", "");
}

void TouchAdpShip_Section(aref ui, string section)
{
	TouchLegLib_Section(TouchLegLib_Model(ui), section);
}

void TouchAdpShip_Leave(aref ui)
{
	string leave = "";
	if (CheckAttribute(ui, "model.EXIT_BTN")) leave = "EXIT_BTN";
	UiBack(leave);
}
