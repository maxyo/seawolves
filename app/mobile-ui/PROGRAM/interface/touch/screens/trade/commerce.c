// SPDX-License-Identifier: GPL-3.0-only
// Commerce: the one buy / sell / transfer layout (pack screen s-trade) of the trade windows. The goods are one list
// that never moves (filter chips and sortable columns in its head); the row whose amount window the game has open
// shows in the dock beside it with the amount on our side after the deal. While the player sets an amount nothing
// goes to the game: the amount is an engine draft, and only the confirmation goes out, the amount and the game's OK
// in one flush. Another row, or the amount back where it was, drops the draft.

#event_handler("UiBuild", "TouchCommerce_Build");
#event_handler("UiTap", "TouchCommerce_Tap");

// The screen's state while its window is open (TouchCommerce_Reset), with the game's state of the last build in
// .game: the taps act on what that build showed.
object TouchCommerce;
object TouchCommerce_View;
aref TouchCommerce_Ui; // the screen's object of the build or tap running now

void TouchCommerce_Reset(string capability)
{
	DeleteAttribute(&TouchCommerce, "");
	TouchCommerce.capability = capability;
	TouchCommerce.shown = false;
	TouchCommerce.hold = "";
	TouchCommerce.holdAt = 0.0;
	TouchCommerce.holdRevision = 0;
	TouchCommerce.holdFloor = 0;
	TouchCommerce.reopen = "";
	TouchCommerce.reopenRevision = 0;
	TouchCommerce.openedFor = "";
	TouchCommerce.opened = false;
	TouchCommerce.switching = false;
	TouchCommerce.switchFloor = 0;
	TouchCommerce.settling = false;
	TouchCommerce.settleRevision = 0;
	TouchCommerce.settleAt = 0.0;
	TouchCommerce.follow = "";
	TouchCommerce.following = false;
	TouchCommerce.switchTo = -1;
	TouchCommerce.filter = 0;
	TouchCommerce.sortBy = -1;
	TouchCommerce.descending = true;
	TouchCommerce.quick = "";
	TouchCommerce.quickAt = 0.0;
	TouchCommerce.takeAll = 0;
}

aref TouchCommerce_Row(aref game, int k)
{
	aref rows;
	makearef(rows, game.rows);
	return GetAttributeN(rows, k);
}

int TouchCommerce_Count(aref game, string list)
{
	if (!CheckAttribute(game, list)) return 0;
	aref at;
	makearef(at, game.(list));
	return GetAttributesNum(at);
}

string TouchCommerce_SelectedKey(aref game)
{
	if (sti(game.selected) < 0) return "";
	aref row = TouchCommerce_Row(game, sti(game.selected));
	return row.key;
}

// How long a tapped row may wait for its window (s).
float TouchCommerce_Hold()
{
	return 0.5;
}

// A tap on a row as the player's: the game selects it in one loop and opens its window in the next (Follow).
void TouchCommerce_OpenRow(aref game, int k)
{
	aref row = TouchCommerce_Row(game, k);
	TouchCommerce.hold = row.key;
	TouchCommerce.holdAt = UiNow();
	TouchCommerce.holdRevision = UiRevision();
	TouchCommerce.holdFloor = UiRequestFloor();
	TouchAdpTrade_Open(TouchCommerce_Ui, k);
	UiRebuildAfter(TouchCommerce_Hold());
}

// Another side, chosen with the header's arrows. An open deal is closed first with the game's cancel: the game hides
// its amount window on a switch but keeps it armed, and the next opened row would confirm the old amount. Its row is
// opened again for the new side (Follow).
void TouchCommerce_SwitchOwner(aref game, int owner)
{
	TouchCommerce.quick = "";
	TouchCommerce.reopen = "";
	TouchCommerce.following = false;
	if (sti(game.deal))
	{
		if (!sti(game.cancel))
		{
			UiError(TouchCommerce.capability + ": the open deal cannot be closed; the owner is not switched");
			return;
		}
		TouchAdpTrade_Close(TouchCommerce_Ui);
		aref row = TouchCommerce_Row(game, sti(game.selected));
		TouchCommerce.follow = row.name;
		TouchCommerce.following = true;
	}
	TouchCommerce.switching = true; // until the model shows the new side, the rows shown are the old side's
	TouchCommerce.switchFloor = UiRequestFloor();
	TouchAdpTrade_Owner(TouchCommerce_Ui, owner);
}

// The game's steps after a tap, a confirmed deal or a switch of side: the rows' windows they open and whether this
// model is one of those steps, not to be drawn (true: the last view stays).
bool TouchCommerce_Follow(aref game)
{
	aref row;
	int selected = sti(game.selected);
	bool deal = sti(game.deal);
	bool money = sti(game.money);
	string key = TouchCommerce_SelectedKey(game);
	int revision = UiRevision();
	float now = UiNow();
	bool holding = false;
	// A tapped row: the frame between its selection and its window is not shown, so the selection and its deal appear
	// together; a row whose window does not come is shown selected after the hold.
	if (TouchCommerce.hold != "")
	{
		bool arrived = deal && (key == TouchCommerce.hold || revision > sti(TouchCommerce.holdRevision));
		bool refused = !arrived && UiAnsweredSince(sti(TouchCommerce.holdFloor)) == 2;
		bool still = !arrived && !refused && sti(TouchCommerce.shown) &&
		             now - stf(TouchCommerce.holdAt) < TouchCommerce_Hold();
		if (still)
		{
			holding = true;
			UiRebuildAfter(stf(TouchCommerce.holdAt) + TouchCommerce_Hold() - now);
		}
		if (!still && !arrived && !refused)
			UiError(TouchCommerce.capability + ": the game opened no window for " + TouchCommerce.hold +
			        " in time; shown selected");
		if (!still) TouchCommerce.hold = "";
	}
	string owner = "";
	int owners = TouchCommerce_Count(game, "owners");
	aref side;
	for (int k = 0; k < owners; k++)
	{
		side = GetAttributeN(TouchCommerce_Owners(game), k);
		if (sti(side.selected)) owner = side.key;
	}
	// A side chosen while a row's window was on its way goes out once that window has come.
	if (sti(TouchCommerce.switchTo) >= 0 && TouchCommerce.hold == "" && TouchCommerce.quick == "")
	{
		int to = sti(TouchCommerce.switchTo);
		TouchCommerce.switchTo = -1;
		bool can = to < owners;
		if (can)
		{
			side = GetAttributeN(TouchCommerce_Owners(game), to);
			can = sti(side.can);
		}
		if (!can)
			UiError(TouchCommerce.capability + ": the side " + to + " chosen while a window was on its way is gone");
		if (can && !sti(side.selected)) TouchCommerce_SwitchOwner(game, to);
	}
	// A confirmed deal closes the game's window and leaves its row selected: its deal is opened again, so the dock
	// keeps its amount for the next deal of the same row.
	if (TouchCommerce.reopen != "" && !deal && revision > sti(TouchCommerce.reopenRevision))
	{
		if (money && key == TouchCommerce.reopen && TouchCommerce.hold == "")
		{
			row = TouchCommerce_Row(game, selected);
			if (sti(row.activate))
			{
				TouchCommerce_OpenRow(game, selected);
				holding = sti(TouchCommerce.shown);
			}
		}
		TouchCommerce.reopen = "";
	}
	// Another side: the game binds it a frame after the selector moved, so its deal is asked for on the next model (or
	// after the hold); the row whose deal was open at the switch is opened again when the new side has it.
	if (sti(TouchCommerce.switching) && UiAnsweredSince(sti(TouchCommerce.switchFloor)) == 2)
	{
		TouchCommerce.switching = false;
		TouchCommerce.following = false;
	}
	if (selected >= 0 && (!sti(TouchCommerce.opened) || TouchCommerce.openedFor != owner))
	{
		bool switched = sti(TouchCommerce.opened);
		TouchCommerce.openedFor = owner;
		TouchCommerce.opened = true;
		TouchCommerce.switching = false;
		if (switched)
		{
			TouchCommerce.settleRevision = revision;
			TouchCommerce.settleAt = now;
			TouchCommerce.settling = true;
		}
		else
		{
			row = TouchCommerce_Row(game, selected);
			if (money && !deal && sti(row.activate)) TouchCommerce_OpenRow(game, selected);
		}
	}
	if (sti(TouchCommerce.settling) && selected >= 0)
	{
		if (revision > sti(TouchCommerce.settleRevision) || now - stf(TouchCommerce.settleAt) >= TouchCommerce_Hold())
		{
			TouchCommerce.settling = false;
			int open = selected;
			if (sti(TouchCommerce.following))
				for (int r = 0; r < TouchCommerce_Count(game, "rows"); r++)
				{
					row = TouchCommerce_Row(game, r);
					if (row.name == TouchCommerce.follow) open = r;
				}
			bool reopen = money || sti(TouchCommerce.following);
			TouchCommerce.following = false;
			row = TouchCommerce_Row(game, open);
			if (reopen && !deal && TouchCommerce.hold == "" && sti(row.activate)) TouchCommerce_OpenRow(game, open);
		}
		else
			UiRebuildAfter(stf(TouchCommerce.settleAt) + TouchCommerce_Hold() - now);
	}
	return holding;
}

aref TouchCommerce_Owners(aref game)
{
	aref owners;
	makearef(owners, game.owners);
	return owners;
}

// The two amount columns: ours where the layout has them, else the game's captions; «У вас» / «Здесь» where the game
// names both sides alike.
void TouchCommerce_Captions(aref game, aref layout, aref out)
{
	out.own = game.headers.own;
	out.other = game.headers.other;
	out.ownIcon = "capacity";
	if (sti(layout.small)) out.ownIcon = "hero";
	out.otherIcon = layout.baseIcon;
	if (out.own == "" || out.own == out.other)
	{
		out.own = UiText("trade.yours");
		out.other = UiText("trade.theirs");
		return;
	}
	if (TouchLib_StartsWith(out.other, out.own)) out.own = UiText("trade.yours");
	if (layout.ownCaption != "")
	{
		out.own = UiText(layout.ownCaption);
		out.ownIcon = layout.ownIcon;
	}
	if (layout.otherCaption != "")
	{
		out.other = UiText(layout.otherCaption);
		out.otherIcon = layout.otherIcon;
	}
}

// The open deal as the player sets it: the draft's ends and amount. What both sides hold is taken when the game opens
// the row's window (the pivot and the other end), and the amount starts at the pivot.
void TouchCommerce_Deal(aref game, aref out)
{
	int selected = sti(game.selected);
	aref row;
	out.ours = 0;
	out.theirs = 0;
	if (selected >= 0)
	{
		row = TouchCommerce_Row(game, selected);
		out.ours = row.own;
		out.theirs = row.other;
	}
	if (sti(game.deal)) UiDraftFollow("deal", row.key, sti(out.ours), sti(out.theirs));
	else UiDraftClose("deal");
	out.origin = UiDraftOrigin("deal");
	out.other = UiDraftOther("deal");
	bool money = sti(game.money) && selected >= 0;
	bool take = !money || row.buyText != "-";
	bool give = !money || row.sellText != "-";
	// the scale's left end: everything we hold given away; what we hold is the pivot
	out.low = 0;
	if (!give) out.low = out.origin;
	out.high = out.origin;
	if (take) out.high = sti(out.origin) + sti(out.other);
	out.most = out.high;
	out.moreReason = "";
	if (money) TouchCommerce_Reach(game, row, take, out);
	out.nothing = sti(game.deal) && money && sti(out.most) <= sti(out.low) && out.moreReason != "";
	out.shown = out.ours;
	if (sti(game.deal)) out.shown = UiDraftValue("deal", sti(out.low), sti(out.most));
	out.amount = sti(out.shown) - sti(out.origin);
	out.total = 0;
	if (money) out.total = TouchAdpTrade_Total(TouchCommerce_Ui, selected, sti(out.amount));
	out.weight = 0.0;
	if (sti(game.weighs) && selected >= 0)
		out.weight = TouchAdpTrade_Weight(TouchCommerce_Ui, selected, abs(sti(out.amount)));
}

// The most a money deal can reach (the stock, the purse and the hold), and why the amount cannot go further up
// although the other side has more: a tap or a drag past the end says it.
void TouchCommerce_Reach(aref game, aref row, bool take, aref deal)
{
	object reach;
	TouchAdpTrade_Reach(TouchCommerce_Ui, sti(game.selected), sti(deal.other), &reach);
	int most = sti(deal.origin) + sti(reach.most);
	if (most < sti(deal.high)) deal.most = most;
	if (!sti(game.deal) || !take || sti(deal.most) >= sti(deal.high)) return;
	deal.moreReason = UiText("trade.no_room");
	if (sti(game.pieces)) deal.moreReason = UiText("trade.no_carry");
	if (sti(reach.room) > sti(deal.most) - sti(deal.origin)) deal.moreReason = UiText("trade.no_money");
}

// The quick take waits for the game to open the row's window, then takes all of it.
void TouchCommerce_TakeQuick(aref game, aref deal)
{
	if (TouchCommerce.quick == "") return;
	float now = UiNow();
	if (sti(game.deal) && TouchCommerce_SelectedKey(game) == TouchCommerce.quick && sti(game.editable) &&
	    sti(game.ok))
	{
		TouchAdpTrade_Move(TouchCommerce_Ui, sti(deal.theirs));
		TouchCommerce.quick = "";
		return;
	}
	if (now - stf(TouchCommerce.quickAt) > 3.0)
	{
		UiError(TouchCommerce.capability + ": the game did not open the window of " + TouchCommerce.quick +
		        "; nothing was taken");
		TouchCommerce.quick = "";
		return;
	}
	UiRebuildAfter(stf(TouchCommerce.quickAt) + 3.0 - now);
}

bool TouchCommerce_AnyThere(aref game)
{
	for (int k = 0; k < TouchCommerce_Count(game, "rows"); k++)
	{
		aref row = TouchCommerce_Row(game, k);
		if (sti(row.other) > 0) return true;
	}
	return false;
}

// The engine's take-all of a chest answered: the game's own ran and stopped at what the hero can carry; anything
// still there is too heavy.
void TouchCommerce_AnswerTakeAll(aref game)
{
	int sent = sti(TouchCommerce.takeAll);
	if (sent == 0 || UiRevision() <= sent || TouchAdpTrade_TakingAll(TouchCommerce_Ui)) return;
	TouchCommerce.takeAll = 0;
	if (TouchCommerce_AnyThere(game)) UiToast(UiText("trade.too_heavy"));
}

// The sort key of row k: what we hold, what they hold, its prices.
int TouchCommerce_Key(aref game, int k)
{
	aref row = TouchCommerce_Row(game, k);
	int column = sti(TouchCommerce.sortBy);
	if (column == 0) return sti(row.own);
	if (column == 2) return sti(row.buy);
	if (column == 3) return sti(row.sell);
	return sti(row.other);
}

// The order of the rows as out.o<n> = k: the game's, or by the column whose caption the player tapped (stable).
void TouchCommerce_Order(aref game, aref out)
{
	int count = TouchCommerce_Count(game, "rows");
	string slot;
	string before;
	for (int k = 0; k < count; k++)
	{
		slot = "o" + k;
		out.(slot) = k;
	}
	int column = sti(TouchCommerce.sortBy);
	if (column < 0 || column > 3 || (column > 1 && !sti(game.money))) return;
	bool descending = sti(TouchCommerce.descending);
	for (int i = 1; i < count; i++)
	{
		slot = "o" + i;
		int moved = sti(out.(slot));
		int key = TouchCommerce_Key(game, moved);
		int at = i;
		bool earlier = true;
		while (at > 0 && earlier)
		{
			before = "o" + (at - 1);
			int other = TouchCommerce_Key(game, sti(out.(before)));
			earlier = (descending && key > other) || (!descending && key < other);
			if (earlier)
			{
				slot = "o" + at;
				out.(slot) = out.(before);
				at--;
			}
		}
		slot = "o" + at;
		out.(slot) = moved;
	}
}

// The trade type of a row as our word beside its name and its tone.
void TouchCommerce_Type(string type, aref out)
{
	out.word = "";
	out.tone = "";
	if (type == "export") { out.word = UiText("trade.export"); out.tone = "ok"; }
	if (type == "import") { out.word = UiText("trade.import"); out.tone = "gold"; }
	if (type == "demand") { out.word = UiText("trade.demand"); out.tone = "gold"; }
	if (type == "contraband") { out.word = UiText("trade.contraband_goods"); out.tone = "danger"; }
}

// The facts of the open row in the dock under its name: its prices, its weight and what the figures are for («за 20
// шт.»: the game sells goods by the piece and writes their figures for a pack).
void TouchCommerce_Facts(aref game, aref row, aref out)
{
	aref at;
	string slot;
	int n = 0;
	if (sti(game.money))
	{
		TouchCommerce_Price(row.buyText, sti(row.buy), "price-buy", out, 0);
		TouchCommerce_Price(row.sellText, sti(row.sell), "price-sell", out, 1);
		n = 2;
	}
	if (row.weight != "")
	{
		slot = "f" + n;
		makearef(at, out.(slot));
		at.icon = "weight";
		at.v = row.weight;
		if (row.weightUnit == "cwt") at.v = row.weight + " " + UiText("transfer.cwt");
		at.v = UiDecimal(at.v);
		n++;
	}
	if (n > 0 && row.pack != "" && row.pack != "1")
	{
		slot = "f" + n;
		makearef(at, out.(slot));
		at.v = UiFill(UiText("trade.per_units"), row.pack);
		at.tone = "dim";
	}
}

// A price fact: a dash where the game does not trade the row this way.
void TouchCommerce_Price(string text, int price, string icon, aref out, int n)
{
	aref at;
	string slot = "f" + n;
	makearef(at, out.(slot));
	at.icon = icon;
	at.iconTone = TouchCommerce_IconTone(icon);
	at.v = "—";
	at.tone = "dim";
	if (!UiHasAmount(text)) return;
	at.v = TouchLib_Thousands(price);
	at.tone = "gold";
}

// The prices' icons wear the colours of the quantity's zones: taking (buying) green, giving (selling) red.
string TouchCommerce_IconTone(string icon)
{
	if (icon == "price-buy") return "green";
	if (icon == "price-sell") return "danger";
	return "brass";
}

// One slider for both directions: what we hold is the origin, the zone to its left gives, the zone to its right
// takes, each in its colour. The right end is the most this deal can take, fixed while it is open.
void TouchCommerce_Quantity(aref game, aref layout, aref deal, aref captions, aref out)
{
	bool money = sti(game.money);
	out.value = deal.shown;
	out.min = deal.low;
	out.max = deal.most;
	if (sti(deal.low) > sti(deal.most)) out.max = deal.low;
	out.zones = true;
	out.origin = deal.origin;
	out.lessLabel = UiText("trade.give");
	out.moreLabel = UiText("trade.take");
	if (money)
	{
		out.lessLabel = UiText("trade.sell");
		out.moreLabel = UiText("trade.buy");
	}
	out.label = captions.own;
	out.allLabel = UiText("common.all");
	out.summary = "-"; // a money deal's price is on its button (one place)
	out.moreReason = deal.moreReason;
	out.reason = "";
	if (sti(deal.nothing)) out.reason = deal.moreReason;
	out.disabled = !sti(game.editable) || sti(deal.nothing);
	if (sti(layout.small))
	{
		out.steps.s0 = 1;
		out.steps.s1 = 10;
	}
	out.all = true;
	out.stack = true;
	out.surface = "wood";
}

// The list's rows in their order and filter, and how many; the open deal's control goes to `quantity`.
int TouchCommerce_Rows(aref game, aref layout, aref deal, aref captions, aref list, aref quantity)
{
	object order;
	TouchCommerce_Order(game, &order);
	bool money = sti(game.money);
	int selected = sti(game.selected);
	int filter = sti(TouchCommerce.filter);
	object type;
	aref row;
	aref at;
	aref image;
	aref from;
	int shown = 0;
	string slot;
	for (int n = 0; n < TouchCommerce_Count(game, "rows"); n++)
	{
		slot = "o" + n;
		int k = sti(order.(slot));
		row = TouchCommerce_Row(game, k);
		TouchCommerce_Type(row.type, &type);
		bool skip = filter == 1 && sti(row.own) == 0;
		if (filter == 2 && !money) skip = skip || sti(row.other) == 0;
		if (filter == 2 && money) skip = skip || (row.type != "export" && row.type != "import" && row.type != "demand");
		if (!skip)
		{
			slot = "r" + shown;
			shown++;
			makearef(at, list.rows.(slot));
			at.id = k;
			at.name = row.name;
			makearef(image, at.image);
			makearef(from, row.image);
			CopyAttributes(image, from);
			at.meta = type.word;
			at.sub = type.word;
			at.quick = sti(layout.quick) && sti(row.other) > 0 && sti(row.activate);
			at.metaTone = type.tone;
			at.cells.c0 = row.ownText;
			at.cells.c1 = row.otherText;
			if (money)
			{
				at.cells.c2.text = row.buyText;
				at.cells.c2.bold = true;
				at.cells.c3.text = row.sellText;
				at.cells.c3.bold = true;
			}
			at.selected = k == selected;
			at.disabled = !sti(row.activate) && !sti(row.select);
			at.reason = row.reason;
			if (k == selected)
			{
				makearef(from, at.facts);
				TouchCommerce_Facts(game, row, from);
			}
			if (k == selected && sti(game.deal))
			{
				TouchCommerce_Quantity(game, layout, deal, captions, quantity);
				at.text = game.dealText;
			}
		}
	}
	return shown;
}

void TouchCommerce_BindList(aref game, aref layout, aref deal)
{
	UiOn("goods", "sort");
	if (sti(layout.quick)) UiOn("goods", "quick");
	UiOn("goods", "row");
	if (sti(game.deal)) UiDraftBind("deal", "goods", sti(deal.low), sti(deal.most), deal.moreReason, 1);
}

// Whose side: the ship or character selector, null with one side. The arrows stay while a deal is open: a switch
// closes the deal first.
void TouchCommerce_Subject(aref game, aref view)
{
	int owners = TouchCommerce_Count(game, "owners");
	view.subject = "";
	if (owners <= 1) return;
	int current = 0;
	aref side;
	for (int k = 0; k < owners; k++)
	{
		side = GetAttributeN(TouchCommerce_Owners(game), k);
		if (sti(side.selected)) current = k;
	}
	side = GetAttributeN(TouchCommerce_Owners(game), current);
	bool closable = !sti(game.deal) || sti(game.cancel);
	view.subject.label = side.label;
	view.subject.sub = (current + 1) + " " + UiText("common.of") + " " + owners;
	view.subject.prev = current > 0 && closable;
	view.subject.next = current + 1 < owners && closable;
	view.subject.list = false;
	TouchCommerce.current = current;
	UiOn("header", "subject-prev");
	UiOn("header", "subject-next");
	UiIgnore("header", "subject-list");
}

void TouchCommerce_Choose(int owner)
{
	aref game;
	makearef(game, TouchCommerce.game);
	if (owner < 0 || owner >= TouchCommerce_Count(game, "owners")) return;
	aref side = GetAttributeN(TouchCommerce_Owners(game), owner);
	if (!sti(side.can)) return;
	if (TouchCommerce.hold != "" || TouchCommerce.quick != "")
	{
		TouchCommerce.switchTo = owner; // a row's window is on its way: switched once it has come
		return;
	}
	TouchCommerce_SwitchOwner(game, owner);
}

// What a weight is written as: pieces with one decimal, goods in whole cwt.
string TouchCommerce_Weight(aref game, float value)
{
	if (sti(game.pieces)) return UiDecimal(Sprintf("%.1f", value));
	return UiDecimal(Sprintf("%.0f", value));
}

// The header's figures: what we carry, the place's load, the purse. While a money deal is set they show what it
// changes; the preview is there from the moment the deal opens, so the header does not reflow at the first step.
void TouchCommerce_Stats(aref game, aref layout, aref deal, aref view)
{
	object figure;
	aref at;
	string slot;
	int n = 0;
	bool previews = sti(game.deal) && sti(game.money);
	if (TouchLib_Figure(game.carry, &figure))
	{
		makearef(at, view.stats.s0);
		at.value = UiDecimal(figure.value);
		at.icon = layout.carryIcon;
		if (previews && stf(game.capacity) >= 0.0 && sti(game.weighs))
		{
			float load = stf(game.load);
			float moved = stf(deal.weight);
			if (sti(deal.amount) < 0) moved = -moved;
			at.value = TouchCommerce_Weight(game, load); // as the preview writes it ("34" and "34,0" would differ)
			at.preview = TouchCommerce_Weight(game, load + moved);
			at.previewTone = "neutral";
		}
		n++;
	}
	if (TouchLib_Figure(game.place, &figure))
	{
		slot = "s" + n;
		makearef(at, view.stats.(slot));
		at.label = figure.label;
		at.value = UiDecimal(figure.value);
		n++;
	}
	if (n < 2 && TouchLib_Figure(game.storage, &figure))
	{
		slot = "s" + n;
		makearef(at, view.stats.(slot));
		at.label = figure.label;
		at.value = figure.value;
		string place = UiOneLine(game.place);
		object named;
		if (place != "" && !TouchLib_Figure(place, &named)) at.label = place;
		n++;
	}
	if (game.purse != "")
	{
		if (n == 2) n = 1;
		slot = "s" + n;
		makearef(at, view.stats.(slot));
		DeleteAttribute(at, "");
		if (sti(game.doubloons)) at.label = UiText("trade.doubloons");
		else at.icon = "money";
		at.value = game.purse;
		at.tone = "gold";
		int balance = sti(game.balance);
		if (previews && balance >= 0)
		{
			if (sti(deal.amount) > 0) balance -= sti(deal.total);
			else balance += sti(deal.total);
			at.preview = TouchLib_Thousands(balance);
		}
		n++;
	}
	if (n == 0) view.stats = "";
}

// The component shows column icons and keeps captions as long-press explanations.
void TouchCommerce_Columns(aref game, aref layout, aref captions, aref list)
{
	list.label = game.headers.name;
	TouchCommerce_Column(list, 0, captions.own, captions.ownIcon);
	TouchCommerce_Column(list, 1, captions.other, captions.otherIcon);
	if (sti(game.money))
	{
		TouchCommerce_Column(list, 2, UiText("trade.buy"), "price-buy");
		TouchCommerce_Column(list, 3, UiText("trade.sell"), "price-sell");
	}
	list.quick = "";
	if (sti(layout.quick)) list.quick = "hand";
}

void TouchCommerce_Column(aref list, int n, string label, string icon)
{
	aref at;
	string slot = "c" + n;
	makearef(at, list.cols.(slot));
	at.label = label;
	at.icon = icon;
	at.iconTone = TouchCommerce_IconTone(icon);
	at.sortable = true;
	at.sort = "";
	if (sti(TouchCommerce.sortBy) == n)
	{
		at.sort = "asc";
		if (sti(TouchCommerce.descending)) at.sort = "desc";
	}
}

// The game's own category tabs replace our filters.
void TouchCommerce_Filters(aref game, aref layout, aref captions, aref list)
{
	aref at;
	aref tab;
	string slot;
	int tabs = TouchCommerce_Count(game, "tabs");
	TouchCommerce.tabs = tabs;
	UiOn("goods", "filter");
	if (tabs > 0)
	{
		aref all;
		makearef(all, game.tabs);
		for (int k = 0; k < tabs; k++)
		{
			tab = GetAttributeN(all, k);
			slot = "f" + k;
			makearef(at, list.filters.(slot));
			// our short caption by position; a tab past the known ones (a mod's) keeps the game's
			slot = "t" + tab.position;
			at.label = tab.label;
			if (CheckAttribute(layout, "tabs." + slot)) at.label = UiText(layout.tabs.(slot));
			at.selected = tab.selected;
			at.disabled = !sti(tab.can);
		}
		return;
	}
	int filter = sti(TouchCommerce.filter);
	list.filters.f0.label = UiText("common.all");
	list.filters.f0.selected = filter == 0;
	list.filters.f1.label = captions.own;
	list.filters.f1.selected = filter == 1;
	if (sti(layout.small)) return;
	list.filters.f2.label = captions.other;
	if (sti(game.money)) list.filters.f2.label = UiText("trade.profitable");
	list.filters.f2.selected = filter == 2;
}

// The description of the open deal is in the dock (a tap reads it whole); without a deal the dock offers the place's
// own text.
void TouchCommerce_Info(aref game, aref view)
{
	view.infoLabel = UiText("common.description");
	view.dealing = game.deal;
	if (sti(game.deal))
	{
		view.info = false;
		UiOn("goods", "info");
		return;
	}
	string info = game.info;
	if (game.storage != "")
	{
		if (info != "") info += TouchLib_Newline();
		info += game.storage;
	}
	TouchCommerce.infoText = info;
	view.info = info != "";
	UiOn("info", "");
}

// Bar buttons: the deal, else what the place offers (take all, bury, …).
void TouchCommerce_Primary(aref game, aref deal, aref view)
{
	aref at;
	view.primary = "";
	view.actions = "";
	bool money = sti(game.money);
	if (sti(deal.nothing))
	{
		// not even one can be bought and there is nothing to sell: the deal is there, disabled, and says why
		view.primary.label = UiText("trade.buy");
		view.primary.sub = "";
		view.primary.disabled = true;
		view.primary.reason = deal.moreReason;
		view.primary.busy = false;
		return;
	}
	if (sti(game.deal) && sti(game.ok))
	{
		// The button names the deal in our words with its price and the weight it moves: the amount and the game's OK
		// go out together.
		string sub = "";
		if (money) sub = TouchLib_Thousands(sti(deal.total));
		if (sti(game.weighs))
		{
			if (sub != "") sub += " · ";
			sub += TouchCommerce_Weight(game, stf(deal.weight));
			if (!sti(game.pieces)) sub += " " + UiText("transfer.cwt");
		}
		string more = "trade.take";
		string less = "trade.give";
		if (money)
		{
			more = "trade.buy";
			less = "trade.sell";
		}
		object button;
		UiDraftButton(&button, "deal", sti(deal.low), sti(deal.most), more, less, sub,
		              !sti(game.confirm) || !sti(game.editable), game.confirmReason, "deal-commit");
		if (GetAttributesNum(&button) > 0)
		{
			makearef(at, view.primary);
			CopyAttributes(at, &button);
		}
		return;
	}
	if (sti(game.deal)) return;
	string id;
	for (int k = 0; k < TouchCommerce_Count(game, "buttons"); k++)
	{
		aref buttons;
		makearef(buttons, game.buttons);
		aref place = GetAttributeN(buttons, k);
		id = "action-" + k;
		string slot = "a" + k;
		makearef(at, view.actions.(slot));
		at.id = id;
		at.label = place.label;
		at.variant = "secondary";
		at.disabled = !sti(place.can);
		at.reason = place.reason;
		at.busy = UiPending(id);
		UiOn(id, "");
	}
	if (CheckAttribute(game, "getAll"))
	{
		view.primary.label = game.getAll.label;
		view.primary.sub = "";
		view.primary.disabled = !sti(game.getAll.can);
		view.primary.reason = game.getAll.reason;
		view.primary.busy = UiPending("primary");
		UiOn("primary", "");
		return;
	}
	if (CheckAttribute(game, "takeAll") && TouchCommerce_AnyThere(game))
	{
		// A chest or a barrel: the game keeps its take-all hidden there; the engine runs it in one script call.
		view.primary.label = UiText("trade.take_all");
		view.primary.sub = "";
		view.primary.disabled = !sti(game.takeAll.can);
		view.primary.busy = false;
		UiOn("primary", "");
	}
}

void TouchCommerce_Build()
{
	aref ui = GetEventData();
	if (ui.screen != "commerce") return;
	TouchCommerce_Ui = ui;
	string capability = ui.capability;
	if (UiFirst() || TouchCommerce.capability != capability) TouchCommerce_Reset(capability);
	aref game;
	makearef(game, TouchCommerce.game);
	TouchAdpTrade_State(ui, game);
	TouchCommerce.revision = UiRevision();
	aref layout; // the screen entry's look: title, small, quick, tabs, captions and icons
	makearef(layout, ui.look);
	bool holding = TouchCommerce_Follow(game);
	object captions;
	TouchCommerce_Captions(game, layout, &captions);
	aref deal;
	makearef(deal, TouchCommerce.deal);
	DeleteAttribute(deal, "");
	TouchCommerce_Deal(game, deal);
	TouchCommerce_TakeQuick(game, deal);
	TouchCommerce_AnswerTakeAll(game);

	aref view;

	aref list;

	aref quantity;
	makearef(view, TouchCommerce_View);
	DeleteAttribute(view, "");
	view.title = game.title;
	if (view.title == "") view.title = UiText(layout.title);
	makearef(list, view.list);
	makearef(quantity, view.deal);
	if (TouchCommerce_Rows(game, layout, deal, &captions, list, quantity) == 0) list.rows = "";
	TouchCommerce_BindList(game, layout, deal);
	TouchCommerce_Subject(game, view);
	TouchCommerce_Stats(game, layout, deal, view);
	TouchCommerce_Columns(game, layout, &captions, list);
	TouchCommerce_Filters(game, layout, &captions, list);
	TouchCommerce_Info(game, view);
	TouchCommerce_Primary(game, deal, view);
	UiOn("header-back", "");
	if (TouchCommerce.hold != "") UiAwait(); // the row's window comes in a later game frame
	if (holding)
	{
		// The view kept is the one before the game's step; what it shows of a deal this model has closed takes no
		// input until the next model: the game has no window to apply it to.
		if (!sti(game.deal))
		{
			if (!UiBound("goods", "info")) UiIgnore("goods", "info");
			if (!UiBound("goods", "change")) UiIgnore("goods", "change");
			if (!UiBound("goods", "commit")) UiIgnore("goods", "commit");
			if (!UiBound("primary", "")) UiIgnore("primary", "");
		}
		UiKeep();
		return;
	}
	TouchCommerce.shown = true;
	UiShow(view);
}

void TouchCommerce_Tap()
{
	aref ui = GetEventData();
	if (ui.screen != "commerce") return;
	TouchCommerce_Ui = ui;
	string id = GetEventData(), verb = GetEventData(), value = GetEventData();
	aref game;
	aref row;
	makearef(game, TouchCommerce.game);
	int selected = sti(game.selected);
	bool deal = sti(game.deal);
	if (id == "header-back")
	{
		TouchCommerce.quick = "";
		TouchCommerce.reopen = "";
		// The game's exit closes its amount window when one is open and leaves only when none is.
		if (deal) TouchAdpTrade_Exit(TouchCommerce_Ui);
		TouchAdpTrade_Exit(TouchCommerce_Ui);
		return;
	}
	if (id == "header")
	{
		int current = sti(TouchCommerce.current);
		if (verb == "subject-prev") TouchCommerce_Choose(current - 1);
		if (verb == "subject-next") TouchCommerce_Choose(current + 1);
		return;
	}
	if (id == "info")
	{
		UiRead("", TouchCommerce.infoText);
		return;
	}
	if (id == "primary")
	{
		if (CheckAttribute(game, "getAll"))
		{
			TouchAdpTrade_GetAll(TouchCommerce_Ui);
			return;
		}
		TouchCommerce.takeAll = TouchCommerce.revision;
		TouchAdpTrade_TakeAll(TouchCommerce_Ui);
		return;
	}
	if (TouchLib_StartsWith(id, "action-"))
	{
		TouchAdpTrade_Press(TouchCommerce_Ui, sti(TouchLib_Cut(id, 7, strlen(id))));
		return;
	}
	if (id == "deal-commit")
	{
		TouchAdpTrade_Move(TouchCommerce_Ui, sti(value));
		TouchCommerce.reopen = TouchCommerce_SelectedKey(game); // its deal again once the game has closed this one
		TouchCommerce.reopenRevision = TouchCommerce.revision;
		return;
	}
	if (id != "goods") return;
	if (verb == "info")
	{
		UiRead(game.dealTitle, game.dealText);
		return;
	}
	if (!UiHasAmount(value))
	{
		UiError(TouchCommerce.capability + ": goods " + verb + " '" + value + "' is no number");
		return;
	}
	int n = sti(value);
	if (verb == "sort")
	{
		// a caption's taps: largest first, smallest first, the game's order again
		if (sti(TouchCommerce.sortBy) != n)
		{
			TouchCommerce.sortBy = n;
			TouchCommerce.descending = true;
		}
		else
		{
			if (sti(TouchCommerce.descending)) TouchCommerce.descending = false;
			else TouchCommerce.sortBy = -1;
		}
		UiRebuild();
		return;
	}
	if (verb == "filter")
	{
		if (sti(TouchCommerce.tabs) > 0)
		{
			if (n < sti(TouchCommerce.tabs))
			{
				aref tabs;
				makearef(tabs, game.tabs);
				aref tab = GetAttributeN(tabs, n);
				if (sti(tab.can)) TouchAdpTrade_Tab(TouchCommerce_Ui, n);
			}
			return;
		}
		if (n < 0) n = 0;
		if (n > 2) n = 2;
		TouchCommerce.filter = n;
		UiRebuild();
		return;
	}
	if (n < 0 || n >= TouchCommerce_Count(game, "rows")) return;
	row = TouchCommerce_Row(game, n);
	if (verb == "quick")
	{
		if (deal && n == selected)
		{
			if (sti(game.editable) && sti(game.ok)) TouchAdpTrade_Move(TouchCommerce_Ui, sti(row.other));
			return;
		}
		if (deal && sti(game.cancel)) TouchAdpTrade_Close(TouchCommerce_Ui);
		if (!sti(row.activate)) return;
		TouchCommerce.quick = row.key;
		TouchCommerce.quickAt = UiNow();
		TouchAdpTrade_Open(TouchCommerce_Ui, n);
		return;
	}
	if (verb != "row") return;
	if (sti(TouchCommerce.switching) || sti(TouchCommerce.settling))
	{
		// another side was just chosen and the game has not bound it yet: the tapped goods open once its list is in
		TouchCommerce.follow = row.name;
		TouchCommerce.following = true;
		return;
	}
	if (deal && sti(game.cancel)) TouchAdpTrade_Close(TouchCommerce_Ui);
	TouchCommerce.quick = "";
	TouchCommerce.reopen = "";
	if (deal && n == selected) return; // a tap on the open row closes it
	if (sti(row.activate)) TouchCommerce_OpenRowAt(game, n, sti(TouchCommerce.revision));
	else
	{
		if (sti(row.select)) TouchAdpTrade_Select(TouchCommerce_Ui, n);
	}
}

// OpenRow from a tap: the revision is the one the tapped view was built from.
void TouchCommerce_OpenRowAt(aref game, int k, int revision)
{
	TouchCommerce_OpenRow(game, k);
	TouchCommerce.holdRevision = revision;
}
