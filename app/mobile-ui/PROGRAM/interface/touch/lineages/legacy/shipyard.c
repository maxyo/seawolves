// SPDX-License-Identifier: GPL-3.0-only
// The shipyard from the interface shipyard: our ships' scroll SHIPS_SCROLL (what shipyard.c FillShipsScroll writes of
// each place: its captain, class and price), the yard's TABLE_SHIPYARD, the card's figures TABLE_OTHER, the deals
// BUTTON_BUY / BUTTON_SELL / BUTTON_REPAIR, and the windows the game opens over them: its question MSG_WINDOW (with
// what a deal costs, a trade-in's difference included), REPAIR_WINDOW, the captain's OFFICERS_WINDOW and the guns'
// CANNONS_WINDOW. A window is open while its nodes are in the model. Implements contracts/shipyard.c.

void TouchAdpShipyard_State(aref ui, aref out)
{
	aref model = TouchLegLib_Model(ui);
	DeleteAttribute(out, "");
	out.title = TouchLib_Trim(TouchLegLib_Label(model, "TITLE"));
	out.money = TouchLib_Trim(TouchLegLib_Label(model, "MONEY_TEXT"));
	aref fleet;
	makearef(fleet, out.fleet);
	string tradeIn = "";
	for (int k = 0; k < TouchLib_Count(model, "SHIPS_SCROLL.rows"); k++)
	{
		aref ship = TouchLib_Add(fleet, "f");
		TouchLegShipyard_Place(TouchLib_Item(model, "SHIPS_SCROLL.rows", k), ship);
		if (sti(ship.selected) && sti(ship.owned)) tradeIn = ship.value;
	}
	aref offers;
	makearef(offers, out.offers);
	int chosen = -1;
	for (k = 0; k < TouchLib_Count(model, "TABLE_SHIPYARD.rows"); k++)
	{
		aref offer = TouchLib_Add(offers, "o");
		TouchLegShipyard_Offer(TouchLib_Item(model, "TABLE_SHIPYARD.rows", k), offer);
		if (sti(offer.selected)) chosen = k;
	}
	aref deals;
	makearef(deals, out.deals);
	// ProcessFrame clears the yard's choice when our scroll is chosen: a chosen offer is what the card shows.
	if (chosen >= 0)
	{
		aref shown = TouchLib_Item(out, "offers", chosen);
		TouchLegShipyard_Card(model, shown.name, true, out);
		out.card.tradeIn = tradeIn;
		TouchLegShipyard_Button(model, "buy", "BUTTON_BUY", shown.value, deals);
	}
	else if (TouchLegShipyard_Owned(fleet))
	{
		string named = "";
		if (CheckAttribute(model, "EDIT_BOX")) named = model.EDIT_BOX.value;
		TouchLegShipyard_Card(model, named, false, out);
		TouchLegShipyard_Button(model, "repair", "BUTTON_REPAIR", "", deals);
		TouchLegShipyard_Guns(model, deals);
		TouchLegShipyard_Button(model, "sell", "BUTTON_SELL", tradeIn, deals);
	}
	TouchLegShipyard_Window(model, out);
}

// A place of our ships' scroll: a ship of ours, a captain without one ("Нет корабля") or a free place.
void TouchLegShipyard_Place(aref row, aref at)
{
	string pic = "pic" + (sti(row.index) + 1);
	if (!CheckAttribute(&GameInterface, "SHIPS_SCROLL." + pic + ".character"))
		UiError("shipyard: the game's scroll has no captain for its " + pic);
	aref place;
	makearef(place, GameInterface.SHIPS_SCROLL.(pic));
	bool free = place.character == "-1";
	bool owned = !free;
	if (CheckAttribute(place, "str2")) owned = owned && place.str2 != "NoneBoat";
	object ship = {key = row.id, label = row.label, meta = "", value = "", free = free, owned = owned,
	               selected = row.value == "1", can = CheckAttribute(row, "offers.select")};
	if (owned)
	{
		ship.meta = TouchLegShipyard_Literal(place.str1);
		ship.value = TouchLegShipyard_Literal(place.str3);
	}
	CopyAttributes(at, &ship);
	aref image;
	makearef(image, at.image);
	if (!free) TouchLegLib_Image(row, image);
}

// The scroll's own texts are "#"-marked as written, not keys of its strings.
string TouchLegShipyard_Literal(string text)
{
	if (TouchLib_StartsWith(text, "#")) return TouchLib_Cut(text, 1, strlen(text));
	return text;
}

bool TouchLegShipyard_Owned(aref fleet)
{
	for (int k = 0; k < GetAttributesNum(fleet); k++)
	{
		aref ship = GetAttributeN(fleet, k);
		if (sti(ship.selected)) return sti(ship.owned);
	}
	return false;
}

// An offer of the yard: its type and name ("Тартана\n\nПальмье", the game's line breaks as written), class and
// price.
void TouchLegShipyard_Offer(aref row, aref at)
{
	string named = TouchLegLib_Cell(row, 0);
	string type = named;
	string name = "";
	int gap = findSubStr(named, "\n", 0);
	if (gap >= 0)
	{
		type = TouchLib_Trim(TouchLib_Cut(named, 0, gap));
		name = TouchLib_Trim(TouchLib_Cut(named, gap, strlen(named)));
		while (TouchLib_StartsWith(name, "\n")) name = TouchLib_Trim(TouchLib_Cut(name, 2, strlen(name)));
	}
	object offer = {key = row.id, type = type, name = name, meta = TouchLegLib_Cell(row, 1),
	                value = TouchLegLib_Cell(row, 4), selected = row.value == "1",
	                can = CheckAttribute(row, "offers.select"), reason = row.reason};
	offer.label = type;
	if (name != "") offer.label = type + " «" + name + "»";
	CopyAttributes(at, &offer);
	aref image;
	makearef(image, at.image);
	TouchLegLib_Picture(row, 0, image);
}

// The card: the ship FillShipParam shows, named by the offer or, ours, by the name box.
void TouchLegShipyard_Card(aref model, string name, bool isOffer, aref out)
{
	aref card;
	makearef(card, out.card);
	string type = TouchLib_Trim(TouchLegLib_Label(model, "FRAME_INFO_CAPTION"));
	card.offer = isOffer;
	card.title = type;
	if (name != "") card.title = type + " «" + name + "»";
	card.rank = TouchLib_Trim(TouchLegLib_Label(model, "SHIP_RANK"));
	card.text = TouchLegLib_Label(model, "INFO_TEXT");
	card.tradeIn = "";
	aref image;
	makearef(image, card.image);
	TouchLegLib_NodeImage(model, "SHIP_BIG_PICTURE", image);
	aref facts;
	makearef(facts, card.facts);
	for (int k = 0; k < TouchLib_Count(model, "TABLE_OTHER.rows"); k++)
	{
		aref row = TouchLib_Item(model, "TABLE_OTHER.rows", k);
		if (row.id != "CannonType" && TouchLegLib_Cell(row, 1) != "")
		{
			object fact = {key = row.id, label = TouchLegLib_Cell(row, 1), value = TouchLegLib_Cell(row, 2)};
			aref at = TouchLib_Add(facts, "f");
			CopyAttributes(at, &fact);
		}
	}
}

void TouchLegShipyard_Button(aref model, string key, string node, string sub, aref deals)
{
	if (!CheckAttribute(model, node)) return;
	aref at = TouchLib_Add(deals, "d");
	at.key = key;
	at.sub = sub;
	TouchLegLib_Button(model, node, at);
}

// The guns: the figures' row CannonType, which opens the guns' window (ShowOtherClick).
void TouchLegShipyard_Guns(aref model, aref deals)
{
	int k = TouchLegLib_RowOf(model, "TABLE_OTHER", "CannonType");
	if (k < 0) return;
	aref row = TouchLib_Item(model, "TABLE_OTHER.rows", k);
	object deal = {key = "guns", label = TouchLegLib_Cell(row, 1), sub = TouchLegLib_Cell(row, 2),
	               can = CheckAttribute(row, "offers.activate"), reason = row.reason};
	aref at = TouchLib_Add(deals, "d");
	CopyAttributes(at, &deal);
}

// The window of the game open over the shipyard, by its nodes, with its ok and cancel.
string TouchLegShipyard_Open(aref model)
{
	if (CheckAttribute(model, "MSG_OK")) return "confirm";
	if (CheckAttribute(model, "REPAIR_OK")) return "repair";
	if (CheckAttribute(model, "ACCEPT_ADD_OFFICER")) return "captain";
	if (CheckAttribute(model, "CANNONS_CANCEL")) return "guns";
	return "";
}

string TouchLegShipyard_OkNode(string kind)
{
	if (kind == "confirm") return "MSG_OK";
	if (kind == "repair") return "REPAIR_OK";
	if (kind == "captain") return "ACCEPT_ADD_OFFICER";
	return "";
}

string TouchLegShipyard_CancelNode(string kind)
{
	if (kind == "confirm") return "MSG_CANCEL";
	if (kind == "repair") return "REPAIR_CANCEL";
	if (kind == "captain") return "CANCEL_ADD_OFFICER";
	if (kind == "guns") return "CANNONS_CANCEL";
	UiError("shipyard: no cancel of the window " + kind);
	return "";
}

void TouchLegShipyard_Window(aref model, aref out)
{
	string kind = TouchLegShipyard_Open(model);
	if (kind == "") return;
	aref window;
	makearef(window, out.window);
	window.kind = kind;
	window.text = "";
	aref ok;
	makearef(ok, window.ok);
	TouchLegLib_Button(model, TouchLegShipyard_OkNode(kind), ok);
	aref cancel;
	makearef(cancel, window.cancel);
	TouchLegLib_Button(model, TouchLegShipyard_CancelNode(kind), cancel);
	if (kind == "confirm")
	{
		window.title = TouchLib_Trim(TouchLegLib_Label(model, "MSG_WINDOW_CAPTION"));
		window.text = TouchLib_Trim(TouchLegLib_Label(model, "MSG_WINDOW_TEXT"));
	}
	if (kind == "repair")
	{
		window.title = TouchLib_Trim(TouchLegLib_Label(model, "REPAIR_WINDOW_CAPTION"));
		window.price = TouchLib_Trim(TouchLegLib_Label(model, "REPAIR_WINDOW_TEXT"));
		aref parts;
		makearef(parts, window.parts);
		TouchLegShipyard_Part(model, "hull", "H", "Hull", parts);
		TouchLegShipyard_Part(model, "sails", "S", "Sails", parts);
		aref all;
		makearef(all, window.all);
		TouchLegLib_Button(model, "REPAIR_ALL", all);
	}
	if (kind == "captain") TouchLegShipyard_Captain(model, window);
	if (kind == "guns")
	{
		window.title = TouchLib_Trim(TouchLegLib_Label(model, "CANNONS_CAPTION"));
		window.text = TouchLib_Trim(TouchLegLib_Label(model, "CANNONS_TEXT"));
		aref plan;
		makearef(plan, window.plan);
		TouchLegLib_NodeImage(model, "CANNONS_SHIP", plan);
		window.bow = TouchLib_Trim(TouchLegLib_Label(model, "CANNONS_QTY_F"));
		window.stern = TouchLib_Trim(TouchLegLib_Label(model, "CANNONS_QTY_B"));
		window.port = TouchLib_Trim(TouchLegLib_Label(model, "CANNONS_QTY_L"));
		window.starboard = TouchLib_Trim(TouchLegLib_Label(model, "CANNONS_QTY_R"));
	}
}

// A part of the repair as RepairStatShow writes it ("55%": what it is and what the order adds), named by its figure.
void TouchLegShipyard_Part(aref model, string key, string node, string figure, aref parts)
{
	string qty = "REPAIR_QTY_" + node;
	if (!CheckAttribute(model, qty)) UiError("shipyard: the repair window has no " + qty);
	object part = {key = key, label = "", amount = UiAmount(model.(qty).label, qty)};
	for (int k = 0; k < TouchLib_Count(model, "TABLE_OTHER.rows"); k++)
	{
		aref row = TouchLib_Item(model, "TABLE_OTHER.rows", k);
		if (row.id == figure) part.label = TouchLegLib_Cell(row, 1);
	}
	aref at = TouchLib_Add(parts, "p");
	CopyAttributes(at, &part);
}

// The captain of a ship bought for a free place: the passengers (FillPassengerScroll) and the chosen one's card.
void TouchLegShipyard_Captain(aref model, aref window)
{
	window.title = TouchLib_Trim(TouchLegLib_Label(model, "OFFICERS_WINDOW_CAPTION"));
	window.name = "";
	aref candidates;
	makearef(candidates, window.candidates);
	for (int k = 0; k < TouchLib_Count(model, "PASSENGERSLIST.rows"); k++)
	{
		aref row = TouchLib_Item(model, "PASSENGERSLIST.rows", k);
		object person = {key = row.id, label = row.label, selected = row.value == "1",
		                 can = CheckAttribute(row, "offers.select"), reason = row.reason};
		aref at = TouchLib_Add(candidates, "c");
		CopyAttributes(at, &person);
		aref image;
		makearef(image, at.image);
		TouchLegLib_Image(row, image);
	}
	if (GetAttributesNum(candidates) == 0) return;
	window.name = TouchLib_Trim(TouchLegLib_Label(model, "OFFICER_NAME"));
	aref facts;
	makearef(facts, window.facts);
	for (k = 0; k < TouchLib_Count(model, "TABLE_SMALLOTHER.rows"); k++)
	{
		aref fact = TouchLib_Item(model, "TABLE_SMALLOTHER.rows", k);
		if (TouchLegLib_Cell(fact, 1) != "")
		{
			aref to = TouchLib_Add(facts, "f");
			to.label = TouchLegLib_Cell(fact, 1);
			to.value = TouchLegLib_Cell(fact, 2);
		}
	}
	aref skills;
	makearef(skills, window.skills);
	TouchLegLib_ShipSkills(model, "TABLE_SMALLSKILL", skills);
}

void TouchAdpShipyard_Fleet(aref ui, int ship)
{
	TouchLegLib_Choose(TouchLegLib_Model(ui), "SHIPS_SCROLL", ship);
}

void TouchAdpShipyard_Offer(aref ui, int offer)
{
	TouchLegLib_Choose(TouchLegLib_Model(ui), "TABLE_SHIPYARD", offer);
}

void TouchAdpShipyard_Deal(aref ui, string key)
{
	aref model = TouchLegLib_Model(ui);
	if (key == "guns")
	{
		int k = TouchLegLib_RowOf(model, "TABLE_OTHER", "CannonType");
		if (k < 0) UiError("shipyard: no guns' row to open");
		TouchLegLib_Act(model, "TABLE_OTHER", k, "activate");
		return;
	}
	object nodes = {buy = "BUTTON_BUY", sell = "BUTTON_SELL", repair = "BUTTON_REPAIR"};
	if (!CheckAttribute(&nodes, key)) UiError("shipyard: no deal " + key);
	UiSend(nodes.(key), "press", "");
}

// The arrows of a part move its order by one a press (shipyard.c ProcessCommandExecute: a click is ±1) and
// ClickRepairArror stops them where the purse or the part ends; the build shows where it stopped.
void TouchAdpShipyard_Repair(aref ui, int part, int amount)
{
	object state;
	TouchAdpShipyard_State(ui, &state);
	if (!CheckAttribute(&state, "window.parts")) UiError("shipyard: no repair window is open");
	aref at = TouchLib_Item(&state, "window.parts", part);
	int shown = sti(at.amount);
	string node = "H";
	if (at.key == "sails") node = "S";
	string arrow = "REPAIR_RIGHT_" + node;
	if (amount < shown) arrow = "REPAIR_LEFT_" + node;
	for (int i = 0; i < abs(amount - shown); i++) UiSend(arrow, "press", "");
}

void TouchAdpShipyard_RepairAll(aref ui)
{
	UiSend("REPAIR_ALL", "press", "");
}

void TouchAdpShipyard_Candidate(aref ui, int candidate)
{
	TouchLegLib_Choose(TouchLegLib_Model(ui), "PASSENGERSLIST", candidate);
}

void TouchAdpShipyard_Ok(aref ui)
{
	string node = TouchLegShipyard_OkNode(TouchLegShipyard_Open(TouchLegLib_Model(ui)));
	if (node == "") UiError("shipyard: the open window has no ok");
	UiSend(node, "press", "");
}

void TouchAdpShipyard_Close(aref ui)
{
	string kind = TouchLegShipyard_Open(TouchLegLib_Model(ui));
	if (kind == "") UiError("shipyard: no window of the game is open");
	UiSend(TouchLegShipyard_CancelNode(kind), "press", "");
}

void TouchAdpShipyard_Leave(aref ui)
{
	UiBack(TouchLegLib_Exit(TouchLegLib_Model(ui), "EXIT_BTN"));
}
