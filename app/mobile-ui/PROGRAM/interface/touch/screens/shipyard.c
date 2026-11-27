// SPDX-License-Identifier: GPL-3.0-only
// Shipyard: one list on the left, our ships (each with what the yard pays for it, one free place) over the yard's
// offers; on the right the ship being looked at, its figures (an offer's beside those our chosen ship had) and what
// can be done with it; the game's windows (its question before a deal, the repair, the captain, the guns) as sheets
// (pack screen s-shipyard). A deal is the game's button: the game asks its question with the price, the sheet shows it.

#event_handler("UiBuild", "TouchShipyard_Build");
#event_handler("UiTap", "TouchShipyard_Tap");

// The adapter's state of the last build, the figures our ships had when they were on the card (own.<place>), the
// repair window's parts as it opened (floor.<part>) and the view.
object TouchShipyard;
object TouchShipyard_View;

// s: how long a move waits for the game's answer; past it the view shows what the game has.
float TouchShipyard_Wait = 0.5;

void TouchShipyard_Build()
{
	aref ui = GetEventData();
	if (ui.screen != "shipyard") return;
	if (UiFirst()) DeleteAttribute(&TouchShipyard, "");
	aref game;
	makearef(game, TouchShipyard.game);
	TouchAdpShipyard_State(ui, game);
	aref view;
	makearef(view, TouchShipyard_View);
	DeleteAttribute(view, "");
	view.title = game.title;
	if (view.title == "") view.title = UiText("yard.title");
	view.subject = "";
	view.stats = "";
	if (game.money != "")
	{
		aref stats;
		makearef(stats, view.stats);
		aref purse = TouchLib_Stat(stats, "", game.money, "money");
		purse.tone = "gold";
	}
	TouchShipyard_Fleet(game, view);
	TouchShipyard_Offers(game, view);
	view.detail = "";
	view.empty = "";
	if (CheckAttribute(game, "card")) TouchShipyard_Detail(game, view);
	else
	{
		view.empty = UiText("yard.empty_slot");
		int chosen = TouchShipyard_Chosen(game);
		if (chosen >= 0)
		{
			aref ship = TouchLib_Item(game, "fleet", chosen);
			if (!sti(ship.free)) view.empty = ship.label;
		}
	}
	view.sheet = "";
	if (CheckAttribute(game, "window")) TouchShipyard_Sheet(game, view);
	else DeleteAttribute(&TouchShipyard, "floor");
	UiOn("header-back", "");
	UiShow(view);
}

int TouchShipyard_Chosen(aref game)
{
	for (int k = 0; k < TouchLib_Count(game, "fleet"); k++)
	{
		aref ship = TouchLib_Item(game, "fleet", k);
		if (sti(ship.selected)) return k;
	}
	return -1;
}

// Our ships and one free place: the chosen one where it is free, else the first.
void TouchShipyard_Fleet(aref game, aref view)
{
	int chosen = TouchShipyard_Chosen(game);
	int place = -1;
	for (int k = 0; k < TouchLib_Count(game, "fleet"); k++)
	{
		aref ship = TouchLib_Item(game, "fleet", k);
		if (sti(ship.free) && (place < 0 || k == chosen)) place = k;
	}
	aref fleet;
	makearef(fleet, view.fleet);
	for (k = 0; k < TouchLib_Count(game, "fleet"); k++)
	{
		ship = TouchLib_Item(game, "fleet", k);
		bool free = sti(ship.free);
		if (free && k != place) continue;
		string id = "fleet-" + k;
		object row = {id = id, title = ship.label, meta = ship.meta, value = ship.value, image = "", icon = "ship",
		              selected = ship.selected, disabled = !sti(ship.can) || CheckAttribute(game, "window")};
		if (free)
		{
			row.title = UiText("yard.empty_slot");
			row.icon = "plus";
		}
		aref at = TouchLib_Add(fleet, "f");
		CopyAttributes(at, &row);
		aref image;
		makearef(image, ship.image);
		if (GetAttributesNum(image) > 0)
		{
			aref to;
			makearef(to, at.image);
			CopyAttributes(to, image);
		}
		UiOn(id, "");
	}
	if (GetAttributesNum(fleet) == 0) view.fleet = "";
}

void TouchShipyard_Offers(aref game, aref view)
{
	aref offers;
	makearef(offers, view.offers);
	for (int k = 0; k < TouchLib_Count(game, "offers"); k++)
	{
		aref offer = TouchLib_Item(game, "offers", k);
		string id = "offer-" + k;
		object row = {id = id, title = offer.label, meta = "", value = TouchShipyard_Price(offer.value), image = "",
		              selected = offer.selected, disabled = !sti(offer.can) || CheckAttribute(game, "window"),
		              reason = offer.reason};
		if (offer.meta != "") row.meta = UiText("yard.class") + " " + offer.meta;
		aref at = TouchLib_Add(offers, "o");
		CopyAttributes(at, &row);
		aref image;
		makearef(image, offer.image);
		if (GetAttributesNum(image) > 0)
		{
			aref to;
			makearef(to, at.image);
			CopyAttributes(to, image);
		}
		UiOn(id, "");
	}
	view.noOffers = "";
	if (GetAttributesNum(offers) == 0)
	{
		view.offers = "";
		view.noOffers = UiText("yard.no_ships");
	}
}

// A price as the game writes it, its digits grouped where it is a bare number.
string TouchShipyard_Price(string text)
{
	if (!UiHasAmount(text) || TouchLib_FindDigit(text, 0, false) >= 0) return text;
	return TouchLib_Thousands(UiAmount(text, "the price"));
}

// The card: its figures with icons, an offer's beside those our chosen ship had on the card, and its deals.
void TouchShipyard_Detail(aref game, aref view)
{
	aref card;
	makearef(card, game.card);
	aref detail;
	makearef(detail, view.detail);
	detail.title = card.title;
	detail.sub = "";
	if (card.rank != "") detail.sub = UiText("yard.class") + " " + card.rank;
	detail.text = card.text;
	detail.against = "";
	detail.image = "";
	aref image;
	makearef(image, card.image);
	if (GetAttributesNum(image) > 0)
	{
		aref to;
		makearef(to, detail.image);
		CopyAttributes(to, image);
	}
	int chosen = TouchShipyard_Chosen(game);
	string place = "p" + chosen;
	bool offer = sti(card.offer);
	aref ship;
	if (chosen >= 0)
	{
		ship = TouchLib_Item(game, "fleet", chosen);
		if (!sti(ship.owned)) DeleteAttribute(&TouchShipyard, "own." + place);
		if (offer && sti(ship.owned)) detail.against = ship.label;
	}
	if (!offer && chosen >= 0) DeleteAttribute(&TouchShipyard, "own." + place);
	object icons = {Hull = "hull", Sails = "sail-full", Speed = "speed", Maneuver = "maneuver",
	                AgainstWind = "upwind", Capacity = "capacity", Crew = "crew", sCannons = "caliber"};
	aref facts;
	makearef(facts, detail.facts);
	for (int k = 0; k < TouchLib_Count(card, "facts"); k++)
	{
		aref fact = TouchLib_Item(card, "facts", k);
		string key = fact.key;
		aref at = TouchLib_Add(facts, "f");
		at.k = fact.label;
		at.v = UiDecimal(fact.value);
		at.icon = "ship";
		if (CheckAttribute(&icons, key)) at.icon = icons.(key);
		if (!offer && chosen >= 0) TouchShipyard.own.(place).(key) = fact.value;
		if (offer && CheckAttribute(&TouchShipyard, "own." + place + "." + key))
			at.own = UiDecimal(TouchShipyard.own.(place).(key));
	}
	if (GetAttributesNum(facts) == 0) detail.facts = "";
	TouchShipyard_Deals(game, detail);
}

// What can be done with the card's ship; buying names what the yard pays for our ship it takes in beside the price.
void TouchShipyard_Deals(aref game, aref detail)
{
	aref actions;
	makearef(actions, detail.actions);
	for (int k = 0; k < TouchLib_Count(game, "deals"); k++)
	{
		aref deal = TouchLib_Item(game, "deals", k);
		string id = deal.key;
		object action = {id = id, label = deal.label, sub = TouchShipyard_Price(deal.sub), variant = "secondary",
		                 hold = false, disabled = !sti(deal.can) || CheckAttribute(game, "window"),
		                 reason = deal.reason, busy = UiPending(id)};
		if (id == "sell") action.variant = "danger";
		if (id == "buy")
		{
			action.id = "primary";
			action.variant = "primary";
			action.busy = UiPending("primary");
			if (game.card.tradeIn != "") action.sub = action.sub + " − " + game.card.tradeIn;
		}
		aref at = TouchLib_Add(actions, "a");
		CopyAttributes(at, &action);
		UiOn(action.id, "");
	}
	if (GetAttributesNum(actions) == 0) detail.actions = "";
}

void TouchShipyard_Sheet(aref game, aref view)
{
	aref window;
	makearef(window, game.window);
	aref sheet;
	makearef(sheet, view.sheet);
	sheet.kind = window.kind;
	sheet.title = window.title;
	sheet.closable = true;
	sheet.width = 420;
	aref actions;
	makearef(actions, sheet.actions);
	if (window.kind == "confirm")
	{
		sheet.closable = false;
		sheet.text = window.text;
		sheet.width = 400;
		TouchShipyard_Button(actions, "sheet-cancel", window.cancel.label, "", "quiet", window, "cancel");
		TouchShipyard_Button(actions, "sheet-ok", window.ok.label, "", "primary", window, "ok");
	}
	if (window.kind == "repair") TouchShipyard_Repair(window, sheet, actions);
	if (window.kind == "captain") TouchShipyard_Captain(window, sheet, actions);
	if (window.kind == "guns")
	{
		sheet.kind = "cannons";
		sheet.subtitle = window.text;
		sheet.width = 480;
		TouchShipyard_Side(sheet, "bow", "yard.bow", window.bow);
		TouchShipyard_Side(sheet, "stern", "yard.stern", window.stern);
		TouchShipyard_Side(sheet, "port", "yard.port", window.port);
		TouchShipyard_Side(sheet, "starboard", "yard.starboard", window.starboard);
		// The deck plan lies in the middle band of its square picture (the game's window stretches it to 584 x 412).
		if (CheckAttribute(window, "plan.kind"))
		{
			if (window.plan.kind != "texture") UiError("shipyard: the deck plan is not a texture of the game");
			sheet.plan.texture = window.plan.image;
			sheet.plan.uv = "0,0.31,1,0.75";
		}
	}
	if (GetAttributesNum(actions) == 0) sheet.actions = "";
	UiOn("sheet-ok", "");
	UiOn("sheet-cancel", "");
	UiOn("sheet-close", "");
	UiModal("sheet", "sheet-close");
}

// A button of the sheet for the window's press `which` (ok, cancel, all).
void TouchShipyard_Button(aref actions, string id, string label, string sub, string variant, aref window, string which)
{
	aref press;
	makearef(press, window.(which));
	object button = {id = id, label = label, sub = sub, variant = variant, disabled = !sti(press.can),
	                 reason = press.reason};
	aref at = TouchLib_Add(actions, "a");
	CopyAttributes(at, &button);
}

void TouchShipyard_Side(aref sheet, string side, string key, string value)
{
	sheet.(side).label = UiText(key);
	sheet.(side).value = value;
}

// The repair: each part on the whole 0…100 % from what it was when the window opened; the game's arrows stop where
// its purse does, and «Все» and the price are the game's (REPAIR_ALL, RepairMoneyShow).
void TouchShipyard_Repair(aref window, aref sheet, aref actions)
{
	sheet.width = 460;
	bool ordered = false;
	aref parts;
	makearef(parts, sheet.parts);
	for (int k = 0; k < TouchLib_Count(window, "parts"); k++)
	{
		aref part = TouchLib_Item(window, "parts", k);
		string key = part.key;
		if (!CheckAttribute(&TouchShipyard, "floor." + key)) TouchShipyard.floor.(key) = part.amount;
		int floor = sti(TouchShipyard.floor.(key));
		if (sti(part.amount) > floor) ordered = true;
		object q = {value = part.amount, min = 0, max = 100, floor = floor, unit = "%", label = part.label,
		            disabled = false};
		q.steps.s0 = 1;
		q.steps.s1 = 5;
		aref at = TouchLib_Add(parts, "p");
		at.id = key;
		aref to;
		makearef(to, at.q);
		CopyAttributes(to, &q);
		UiOn(key, "change");
		UiOn(key, "commit");
	}
	TouchShipyard_Button(actions, "sheet-all", UiText("common.all"), "", "secondary", window, "all");
	// named as the self repair names it (the game says «Ок»)
	TouchShipyard_Button(actions, "sheet-ok", UiText("repair.do"), TouchShipyard_Price(window.price), "primary",
	                     window, "ok");
	if (!ordered) actions.a1.disabled = true;
	UiOn("sheet-all", "");
}

void TouchShipyard_Captain(aref window, aref sheet, aref actions)
{
	sheet.width = 640; // c-captain-picker: the candidates beside the card
	sheet.name = window.name;
	sheet.empty = "";
	aref candidates;
	makearef(candidates, sheet.candidates);
	for (int k = 0; k < TouchLib_Count(window, "candidates"); k++)
	{
		aref person = TouchLib_Item(window, "candidates", k);
		string id = "candidate-" + k;
		object row = {id = id, title = person.label, image = "", selected = person.selected,
		              disabled = !sti(person.can), reason = person.reason};
		aref at = TouchLib_Add(candidates, "c");
		CopyAttributes(at, &row);
		aref image;
		makearef(image, person.image);
		if (GetAttributesNum(image) > 0)
		{
			aref to;
			makearef(to, at.image);
			CopyAttributes(to, image);
		}
		UiOn(id, "");
	}
	if (GetAttributesNum(candidates) == 0)
	{
		sheet.candidates = "";
		sheet.empty = UiText("yard.no_captains");
	}
	sheet.facts = "";
	aref facts;
	makearef(facts, sheet.facts);
	for (k = 0; k < TouchLib_Count(window, "facts"); k++)
	{
		aref fact = TouchLib_Item(window, "facts", k);
		aref f = TouchLib_Add(facts, "f");
		f.k = fact.label;
		f.v = fact.value;
	}
	sheet.skills = "";
	aref skills, own;
	makearef(skills, sheet.skills);
	if (CheckAttribute(window, "skills"))
	{
		makearef(own, window.skills);
		TouchLib_ShipSkills(own, skills);
	}
	if (CheckAttribute(window, "ok.label") && window.ok.label != "")
		TouchShipyard_Button(actions, "sheet-ok", window.ok.label, "", "primary", window, "ok");
}

void TouchShipyard_Tap()
{
	aref ui = GetEventData();
	if (ui.screen != "shipyard") return;
	string id = GetEventData(), verb = GetEventData(), value = GetEventData();
	aref game;
	makearef(game, TouchShipyard.game);
	bool open = CheckAttribute(game, "window");
	if (id == "header-back")
	{
		if (open) TouchAdpShipyard_Close(ui);
		else TouchAdpShipyard_Leave(ui);
		if (open) UiHold(TouchShipyard_Wait);
		return;
	}
	if (id == "sheet-ok") { TouchAdpShipyard_Ok(ui); UiHold(TouchShipyard_Wait); return; }
	if (id == "sheet-cancel" || id == "sheet-close") { TouchAdpShipyard_Close(ui); UiHold(TouchShipyard_Wait); return; }
	if (id == "sheet-all") { TouchAdpShipyard_RepairAll(ui); UiHold(TouchShipyard_Wait); return; }
	if (id == "repair" || id == "guns" || id == "sell")
	{
		TouchAdpShipyard_Deal(ui, id);
		UiHold(TouchShipyard_Wait);
		return;
	}
	if (id == "primary") { TouchAdpShipyard_Deal(ui, "buy"); UiHold(TouchShipyard_Wait); return; }
	if (id == "hull" || id == "sails")
	{
		int part = 0;
		if (id == "sails") part = 1;
		aref at = TouchLib_Item(game, "window.parts", part);
		int amount = UiAmount(value, "the part's amount");
		if (amount == sti(at.amount)) { UiRebuild(); return; }
		TouchAdpShipyard_Repair(ui, part, amount);
		UiHold(TouchShipyard_Wait);
		return;
	}
	int k;
	if (TouchLib_StartsWith(id, "fleet-"))
	{
		k = TouchLib_TapIndex(TouchLib_Cut(id, 6, strlen(id)), TouchLib_Count(game, "fleet"), "the ship");
		aref ship = TouchLib_Item(game, "fleet", k);
		if (sti(ship.selected)) return;
		TouchAdpShipyard_Fleet(ui, k);
		UiHold(TouchShipyard_Wait);
		return;
	}
	if (TouchLib_StartsWith(id, "offer-"))
	{
		k = TouchLib_TapIndex(TouchLib_Cut(id, 6, strlen(id)), TouchLib_Count(game, "offers"), "the offer");
		aref offer = TouchLib_Item(game, "offers", k);
		if (sti(offer.selected)) return;
		TouchAdpShipyard_Offer(ui, k);
		UiHold(TouchShipyard_Wait);
		return;
	}
	if (TouchLib_StartsWith(id, "candidate-"))
	{
		k = TouchLib_TapIndex(TouchLib_Cut(id, 10, strlen(id)), TouchLib_Count(game, "window.candidates"),
		                      "the captain");
		aref person = TouchLib_Item(game, "window.candidates", k);
		if (sti(person.selected)) return;
		TouchAdpShipyard_Candidate(ui, k);
		UiHold(TouchShipyard_Wait);
		return;
	}
	UiError("shipyard: no tap of " + id);
}
