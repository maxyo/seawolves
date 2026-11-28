// SPDX-License-Identifier: GPL-3.0-only
// Trade: goods on two sides, and the deal of one row between them (pack screen s-trade). The list never moves; the
// open deal shows in the dock with our side's amount after it, a draft until the player confirms it.

#event_handler("UiBuild", "TouchTrade_Build");
#event_handler("UiTap", "TouchTrade_Tap");

// The screen's state while its window is open: its filter, its sort, whether the deal of the selected row was asked
// for (.opened), the adapter's state of the last build in .game (the taps act on what that build showed) and the
// header's figures as they were with no amount set in .figures.
object TouchTrade;
object TouchTrade_View;

// The sort keys of the list's columns.
string TouchTrade_Keys[4] = {"own", "other", "buy", "sell"};

void TouchTrade_Build()
{
	aref ui = GetEventData();
	if (ui.screen != "trade") return;
	if (UiFirst() || TouchTrade.capability != ui.capability)
	{
		object fresh = {capability = ui.capability, filter = 0, takeAll = false, opened = false};
		DeleteAttribute(&TouchTrade, "");
		CopyAttributes(&TouchTrade, &fresh);
		TouchTrade.sort.by = -1;
		TouchTrade.sort.descending = true;
	}
	aref game;
	makearef(game, TouchTrade.game);
	TouchAdpTrade_State(ui, game);
	TouchTrade_Opening(ui, game);
	aref look;
	makearef(look, ui.look);
	aref view;
	makearef(view, TouchTrade_View);
	DeleteAttribute(view, "");
	view.title = game.title;
	view.deal = "";
	if (view.title == "") view.title = UiText(look.title);
	object captions;
	TouchTrade_Captions(game, look, &captions);
	object deal;
	TouchTrade_Deal(ui, game, &deal);
	TouchTrade_List(game, look, &captions, &deal, view);
	// Whose side: the ship or character selector, none where the window has no selector.
	TouchTrade.current = TouchLib_Current(game, "sides", "trade: the sides");
	aref sides;
	makearef(sides, game.sides);
	TouchLib_Stepper(view, sides, sti(TouchTrade.current), sti(game.switch));
	TouchTrade_Stats(game, look, view);
	TouchTrade_Info(game, view);
	TouchTrade_Place(game, view);
	TouchTrade_Primary(game, &deal, view);
	UiOn("header-back", "");
	UiShow(view);
}

// A money trade opens the deal of its selected row, once: a window may select its first row a frame after its list
// came. The engine's take-all says what was too heavy once answered.
void TouchTrade_Opening(aref ui, aref game)
{
	if (sti(game.money) && !sti(TouchTrade.opened))
		for (int k = 0; k < TouchLib_Count(game, "rows"); k++)
		{
			aref row = TouchLib_Item(game, "rows", k);
			if (sti(row.selected))
			{
				TouchTrade.opened = true;
				if (sti(row.can) && !CheckAttribute(game, "deal"))
				{
					TouchAdpTrade_Deal(ui, k);
					UiHold();
				}
			}
		}
	if (!sti(TouchTrade.takeAll) || UiAnswer() == "waiting") return;
	TouchTrade.takeAll = false;
	if (UiAnswer() == "applied" && TouchTrade_AnyThere(game)) UiToast(UiText("trade.too_heavy"));
}

bool TouchTrade_AnyThere(aref game)
{
	for (int k = 0; k < TouchLib_Count(game, "rows"); k++)
	{
		aref row = TouchLib_Item(game, "rows", k);
		if (sti(row.other) > 0) return true;
	}
	return false;
}

// The two amount columns: the look's captions, else the game's, else ours.
void TouchTrade_Captions(aref game, aref look, aref out)
{
	out.own = game.headers.own;
	if (out.own == "") out.own = UiText("trade.yours");
	out.other = game.headers.other;
	if (out.other == "") out.other = UiText("trade.theirs");
	out.ownIcon = "capacity";
	if (sti(look.small)) out.ownIcon = "hero";
	out.otherIcon = look.baseIcon;
	if (game.headers.own == "" && game.headers.other == "") return; // the game names both sides alike: ours
	if (look.ownCaption != "")
	{
		out.own = UiText(look.ownCaption);
		out.ownIcon = look.ownIcon;
	}
	if (look.otherCaption != "")
	{
		out.other = UiText(look.otherCaption);
		out.otherIcon = look.otherIcon;
	}
}

// The open deal as the player sets it (UiDeal: value, change, button): the control spans what the two sides hold
// (a side the game does not trade with this row, its price a dash, gives nothing) and follows the amount the game's
// window answers to each move; the total, weight and note are the game's for that amount.
void TouchTrade_Deal(aref ui, aref game, aref deal)
{
	if (!CheckAttribute(game, "deal")) return;
	int k = sti(game.deal.row);
	aref row = TouchLib_Item(game, "rows", k);
	bool money = sti(game.money);
	object spec = {key = row.key, element = "goods", origin = row.own, other = row.other, low = 0,
	               high = sti(row.own) + sti(row.other), answer = sti(row.own) + sti(game.deal.amount),
	               cutReason = UiText("trade.limit"), more = "trade.take", less = "trade.give",
	               reason = game.deal.reason, disabled = !sti(game.deal.confirm) || !sti(game.deal.edit),
	               commit = "deal-commit", offer = "deal-offer"};
	if (money)
	{
		spec.more = "trade.buy";
		spec.less = "trade.sell";
		if (!CheckAttribute(row, "sell")) spec.low = row.own;
		if (!CheckAttribute(row, "buy")) spec.high = row.own;
	}
	UiDeal(&spec, deal);
	deal.low = spec.low;
	deal.high = spec.high;
	deal.total = game.deal.total;
	deal.weight = game.deal.weight;
	deal.note = game.deal.note;
}

// One slider for both directions: what we hold is the origin, the zone to its left gives, the zone to its right
// takes, each in its colour.
void TouchTrade_Quantity(aref game, aref look, aref deal, string label, aref out)
{
	object quantity = {value = deal.value, min = deal.low, max = deal.high, zones = true, origin = 0,
	                   lessLabel = UiText("trade.give"), moreLabel = UiText("trade.take"), label = label,
	                   allLabel = UiText("common.all"), summary = "-", moreReason = "", reason = "",
	                   disabled = !sti(game.deal.edit), all = true, stack = true, surface = "wood"};
	aref row = TouchLib_Item(game, "rows", sti(game.deal.row));
	quantity.origin = row.own;
	if (sti(game.money))
	{
		quantity.lessLabel = UiText("trade.sell");
		quantity.moreLabel = UiText("trade.buy");
	}
	CopyAttributes(out, &quantity);
	if (!sti(look.small)) return;
	out.steps.s0 = 1;
	out.steps.s1 = 10;
}

// The goods list: its head (the name, the sortable amount and price columns, the filters or the game's tabs) and the
// rows in the chosen order and filter; the open row carries its facts and the deal's control.
void TouchTrade_List(aref game, aref look, aref captions, aref deal, aref view)
{
	aref list;
	makearef(list, view.list);
	aref sorting;
	makearef(sorting, TouchTrade.sort);
	bool money = sti(game.money);
	list.label = game.headers.name;
	TouchLib_Column(list, captions.own, captions.ownIcon, "brass", sorting);
	TouchLib_Column(list, captions.other, captions.otherIcon, "brass", sorting);
	if (money)
	{
		TouchLib_Column(list, UiText("trade.buy"), "price-buy", "green", sorting);
		TouchLib_Column(list, UiText("trade.sell"), "price-sell", "danger", sorting);
	}
	list.quick = "";
	if (sti(look.quick)) list.quick = "hand";
	TouchTrade_Filters(game, look, captions, list);
	string key = "";
	if (sti(sorting.by) >= 0) key = TouchTrade_Keys[sti(sorting.by)];
	object order;
	aref rows;
	makearef(rows, game.rows);
	TouchLib_Order(rows, key, sti(sorting.descending), &order);
	aref shown;
	makearef(shown, list.rows);
	for (int n = 0; n < GetAttributesNum(&order); n++)
	{
		aref row = GetAttributeN(&order, n);
		if (TouchTrade_Shown(game, row)) TouchTrade_Row(game, look, captions, deal, row, TouchLib_Add(shown, "r"));
	}
	if (GetAttributesNum(shown) == 0) list.rows = "";
	UiOn("goods", "sort");
	UiOn("goods", "row");
	if (sti(look.quick)) UiOn("goods", "quick");
}

bool TouchTrade_Shown(aref game, aref row)
{
	int filter = sti(TouchTrade.filter);
	if (filter == 1) return sti(row.own) > 0;
	if (filter != 2) return true;
	if (!sti(game.money)) return sti(row.other) > 0;
	if (!CheckAttribute(row, "type")) return false;
	return sti(row.type.profit);
}

void TouchTrade_Row(aref game, aref look, aref captions, aref deal, aref row, aref at)
{
	int k = sti(row.k);
	at.id = row.key;
	at.name = row.name;
	TouchLib_Copy(at, "image", row, "image");
	at.meta = "";
	at.metaTone = "";
	if (CheckAttribute(row, "type"))
	{
		at.meta = row.type.word;
		at.metaTone = row.type.tone;
	}
	at.sub = at.meta;
	if (row.about != "")
	{
		if (at.sub != "") at.sub = at.sub + " · ";
		at.sub = at.sub + row.about;
	}
	at.quick = sti(look.quick) && sti(row.other) > 0 && sti(row.can);
	at.cells.c0 = row.ownText;
	at.cells.c1 = row.otherText;
	if (sti(game.money))
	{
		at.cells.c2.text = row.buyText;
		at.cells.c2.bold = true;
		at.cells.c3.text = row.sellText;
		at.cells.c3.bold = true;
	}
	at.selected = sti(row.selected);
	at.disabled = !sti(row.can);
	at.reason = row.reason;
	if (!sti(row.selected)) return;
	aref facts;
	makearef(facts, at.facts);
	TouchTrade_Facts(game, row, facts);
	if (!CheckAttribute(game, "deal")) return;
	if (sti(game.deal.row) != k) return;
	aref quantity;
	makearef(quantity, TouchTrade_View.deal);
	TouchTrade_Quantity(game, look, deal, captions.own, quantity);
	at.text = game.deal.text;
	UiOn("goods", "info");
}

// The facts of the selected row under its name: its prices, its weight and what the figures are for («за 20 шт.»:
// goods sold by the piece have their figures for a pack).
void TouchTrade_Facts(aref game, aref row, aref facts)
{
	if (sti(game.money))
	{
		TouchTrade_Price(row, "buy", "price-buy", "green", facts);
		TouchTrade_Price(row, "sell", "price-sell", "danger", facts);
	}
	if (row.weight != "") TouchLib_Fact(facts, "weight", UiDecimal(row.weight), "");
	if (GetAttributesNum(facts) > 0 && sti(row.pack) > 1)
		TouchLib_Fact(facts, "", UiFill(UiText("trade.per_units"), row.pack), "dim");
}

// A price fact: a dash where the game does not trade the row this way.
void TouchTrade_Price(aref row, string price, string icon, string iconTone, aref facts)
{
	aref at = TouchLib_Fact(facts, icon, "—", "dim");
	at.iconTone = iconTone;
	if (!CheckAttribute(row, price)) return;
	at.v = TouchLib_Thousands(sti(row.(price)));
	at.tone = "gold";
}

// The game's own category tabs replace our filters.
void TouchTrade_Filters(aref game, aref look, aref captions, aref list)
{
	UiOn("goods", "filter");
	for (int k = 0; k < TouchLib_Count(game, "tabs"); k++)
	{
		aref tab = TouchLib_Item(game, "tabs", k);
		aref at = TouchLib_Filter(list, tab.label, sti(tab.selected));
		at.id = tab.id;
		at.disabled = !sti(tab.can);
	}
	if (CheckAttribute(game, "tabs")) return;
	int filter = sti(TouchTrade.filter);
	TouchLib_Filter(list, UiText("common.all"), filter == 0);
	TouchLib_Filter(list, captions.own, filter == 1);
	if (sti(look.small)) return;
	string other = captions.other;
	if (sti(game.money))
	{
		if (!sti(game.types)) return;
		other = UiText("trade.profitable");
	}
	TouchLib_Filter(list, other, filter == 2);
}

// The header's figures, the adapter's: what we carry, the place's load, the purse. While a deal is open the load
// figures show what they were with no amount set and, as their preview, the game's figures now, from the moment it
// opens, so the header does not reflow at the first step. The purse is the game's: it changes when the deal is made.
void TouchTrade_Stats(aref game, aref look, aref view)
{
	aref stats;
	makearef(stats, view.stats);
	bool dealing = CheckAttribute(game, "deal");
	bool moved = false;
	if (dealing) moved = sti(game.deal.amount) != 0;
	for (int k = 0; k < TouchLib_Count(game, "figures"); k++)
	{
		aref figure = TouchLib_Item(game, "figures", k);
		string kind = figure.kind;
		if (!moved) TouchTrade.figures.(kind) = figure.value;
		if (kind == "purse") TouchTrade_Purse(figure, stats);
		else
		{
			string icon = "";
			string label = figure.label;
			if (kind == "carry")
			{
				icon = look.carryIcon;
				label = "";
			}
			aref at = TouchLib_Stat(stats, label, UiDecimal(figure.value), icon);
			if (dealing && CheckAttribute(&TouchTrade, "figures." + kind))
			{
				at.value = UiDecimal(TouchTrade.figures.(kind));
				at.preview = UiDecimal(figure.value);
				at.previewTone = "neutral";
			}
		}
	}
	if (GetAttributesNum(stats) == 0) view.stats = "";
}

// The purse takes the second place of a full header.
void TouchTrade_Purse(aref figure, aref stats)
{
	if (GetAttributesNum(stats) == 2) DeleteAttribute(stats, "s1");
	object purse = {value = figure.value, tone = "gold"};
	if (figure.currency != "") purse.label = UiText(figure.currency);
	else purse.icon = "money";
	aref at = TouchLib_Add(stats, "s");
	CopyAttributes(at, &purse);
}

// The description of the open deal is in the dock (a tap reads it whole); without one the dock offers the place's.
void TouchTrade_Info(aref game, aref view)
{
	view.infoLabel = UiText("common.description");
	view.dealing = CheckAttribute(game, "deal");
	view.info = false;
	if (CheckAttribute(game, "deal")) return;
	string info = game.info;
	if (game.storage != "")
	{
		if (info != "") info += TouchLib_Newline();
		info += game.storage;
	}
	TouchTrade.info = info;
	view.info = info != "";
	UiOn("info", "");
}

// The place the goods are taken from and the room they fill (a looted town).
void TouchTrade_Place(aref game, aref view)
{
	if (!CheckAttribute(game, "place")) return;
	view.place.title = game.place.title;
	view.place.facts.f0.k = UiText("trade.free_room");
	view.place.facts.f0.v = game.place.room + " " + UiText("transfer.cwt");
}

// The bar's button: the deal named in our words with its price and weight, else the place's plan of the most valuable,
// else what the place offers.
void TouchTrade_Primary(aref game, aref deal, aref view)
{
	view.primary = "";
	view.actions = "";
	aref primary;
	makearef(primary, view.primary);
	if (CheckAttribute(game, "deal"))
	{
		if (!CheckAttribute(deal, "button")) return;
		aref made;
		makearef(made, deal.button);
		CopyAttributes(primary, made);
		primary.sub = TouchTrade_Sub(game, deal);
		return;
	}
	if (CheckAttribute(game, "best"))
	{
		object best = {label = game.best.label, sub = "", disabled = !sti(game.best.can), reason = game.best.reason,
		               busy = UiPending("primary")};
		if (best.label == "") best.label = UiText("trade.take_best");
		if (game.best.weight != "") best.sub = game.best.weight + " " + UiText("transfer.cwt");
		CopyAttributes(primary, &best);
		UiOn("primary", "");
		return;
	}
	aref buttons;
	makearef(buttons, game.buttons);
	TouchLib_Actions(view, buttons);
	if (!CheckAttribute(game, "takeAll")) return;
	object all = {label = game.takeAll.label, sub = "", disabled = !sti(game.takeAll.can),
	              reason = game.takeAll.reason, busy = UiPending("primary")};
	if (all.label == "")
	{
		if (!TouchTrade_AnyThere(game)) return;
		all.label = UiText("trade.take_all");
		all.busy = false;
		all.reason = "";
	}
	CopyAttributes(primary, &all);
	UiOn("primary", "");
}

// The deal's money, weight and note as the game writes them.
string TouchTrade_Sub(aref game, aref deal)
{
	string sub = "";
	if (sti(game.money)) sub = TouchLib_Thousands(sti(deal.total));
	string weight = deal.weight;
	if (weight != "")
	{
		if (sub != "") sub += " · ";
		sub += UiDecimal(weight);
		if (!sti(game.pieces)) sub += " " + UiText("transfer.cwt");
	}
	string note = deal.note;
	if (note == "") return sub;
	if (sub != "") sub += " · ";
	return sub + note;
}

void TouchTrade_Tap()
{
	aref ui = GetEventData();
	if (ui.screen != "trade") return;
	string id = GetEventData(), verb = GetEventData(), value = GetEventData();
	aref game;
	makearef(game, TouchTrade.game);
	int open = -1;
	if (CheckAttribute(game, "deal")) open = sti(game.deal.row);
	if (id == "header-back") { TouchAdpTrade_Leave(ui); return; }
	if (id == "info") { UiRead("", TouchTrade.info); return; }
	if (id == "deal-offer") { TouchAdpTrade_Offer(ui, sti(value)); UiHold(); return; }
	if (id == "deal-commit") { TouchAdpTrade_Commit(ui, sti(value), sti(game.money)); UiHold(); return; }
	if (id == "header")
	{
		int sides = TouchLib_Count(game, "sides");
		TouchAdpTrade_Side(ui, TouchLib_StepTap(verb, sti(TouchTrade.current), sides, "the side"));
		UiHold();
		return;
	}
	if (id == "primary" && CheckAttribute(game, "best"))
	{
		if (!sti(game.best.can)) return;
		TouchAdpTrade_TakeBest(ui);
		UiHold();
		return;
	}
	if (id == "primary")
	{
		if (!CheckAttribute(game, "takeAll")) UiError("trade: a primary tapped without best or take-all");
		TouchTrade.takeAll = game.takeAll.label == "";
		TouchAdpTrade_TakeAll(ui);
		UiHold();
		return;
	}
	if (TouchLib_Action(id) >= 0) { TouchAdpTrade_Press(ui, TouchLib_Action(id)); return; }
	if (id != "goods") UiError("trade: no tap of " + id);
	if (verb == "info") { UiRead(game.deal.title, game.deal.text); return; }
	if (verb == "sort")
	{
		aref sorting;
		makearef(sorting, TouchTrade.sort);
		TouchLib_SortTap(sorting, TouchLib_TapIndex(value, 4, "the goods column"));
		UiRebuild();
		return;
	}
	if (verb == "filter") { TouchTrade_Filter(ui, game, value); return; }
	// The row by its identity: the list may have changed since the frame the player tapped; a row gone ends the tap.
	int k = TouchLib_Find(game, "rows", "key", value);
	if (k < 0) return;
	aref row = TouchLib_Item(game, "rows", k);
	if (verb == "quick") TouchAdpTrade_Take(ui, k);
	if (verb == "row" && (k == open || !sti(row.can))) { TouchAdpTrade_Close(ui); return; }
	if (verb == "row") TouchAdpTrade_Deal(ui, k);
	UiHold();
}

void TouchTrade_Filter(aref ui, aref game, string value)
{
	if (!CheckAttribute(game, "tabs"))
	{
		TouchTrade.filter = TouchLib_TapIndex(value, 3, "the goods filter");
		UiRebuild();
		return;
	}
	int tab = TouchLib_Find(game, "tabs", "id", value);
	if (tab < 0) return; // the category tapped is no longer offered
	aref at = TouchLib_Item(game, "tabs", tab);
	if (sti(at.can)) TouchAdpTrade_Category(ui, tab);
}
