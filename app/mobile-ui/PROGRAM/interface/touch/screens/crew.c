// SPDX-License-Identifier: GPL-3.0-only
// Crew hire: one row, the ship's crew, whose amount is always open; one slider for both directions (the crew the
// window started from is its origin, dismissing to its left, hiring to its right) that follows what the game's window
// makes of each move; under it both crews' experience and morale (pack screen s-crew).

#event_handler("UiBuild", "TouchCrew_Build");
#event_handler("UiTap", "TouchCrew_Tap");

// The adapter's state of the last build (the taps act on what it showed) and the view.
object TouchCrew;
object TouchCrew_View;

// s: how long a move waits for the game's answer; past it the view shows what the game has.
float TouchCrew_Wait = 0.5;

void TouchCrew_Build()
{
	aref ui = GetEventData();
	if (ui.screen != "crew") return;
	aref game;
	makearef(game, TouchCrew.game);
	TouchAdpCrew_State(ui, game);
	aref view;
	makearef(view, TouchCrew_View);
	DeleteAttribute(view, "");
	view.title = game.title;
	if (view.title == "") view.title = UiText("crew.title");
	object deal;
	TouchCrew_Deal(game, &deal);
	TouchCrew_Subject(game, view);
	TouchCrew_Stats(game, view);
	TouchCrew_List(game, &deal, view);
	view.shipLabel = UiText("crew.on_ship");
	view.tavernLabel = UiText("crew.in_tavern");
	TouchCrew_Quality(game, "ship", view);
	TouchCrew_Quality(game, "tavern", view);
	view.notes = game.notes;
	TouchCrew_Primary(game, &deal, view);
	UiOn("header-back", "");
	UiShow(view);
}

// The amount as the player sets it (UiDeal: value, change, button) over every man of the ship and the tavern.
void TouchCrew_Deal(aref game, aref deal)
{
	int ship = TouchCrew_Current(game);
	aref at = TouchLib_Item(game, "ships", ship);
	object spec = {key = at.key, element = "crew", origin = game.base, other = game.baseAshore, low = 0,
	               high = sti(game.base) + sti(game.baseAshore), answer = game.aboard,
	               cutReason = UiText("trade.limit"), more = "crew.hire", less = "crew.fire",
	               reason = game.reason, disabled = !sti(game.confirm) || !sti(game.edit),
	               commit = "crew-commit", offer = "crew-offer"};
	UiDeal(&spec, deal);
	deal.high = spec.high;
}

int TouchCrew_Current(aref game)
{
	for (int k = 0; k < TouchLib_Count(game, "ships"); k++)
	{
		aref ship = TouchLib_Item(game, "ships", k);
		if (sti(ship.selected)) return k;
	}
	UiError("crew: no ship is chosen");
	return 0;
}

// Whose crew: the fleet's ships, none with one.
void TouchCrew_Subject(aref game, aref view)
{
	int ships = TouchLib_Count(game, "ships");
	view.subject = "";
	if (ships <= 1) return;
	int current = TouchCrew_Current(game);
	aref ship = TouchLib_Item(game, "ships", current);
	object subject = {label = ship.label, sub = (current + 1) + " " + UiText("common.of") + " " + ships,
	                  prev = current > 0, next = current + 1 < ships, list = false};
	aref at;
	makearef(at, view.subject);
	CopyAttributes(at, &subject);
	UiOn("header", "subject-prev");
	UiOn("header", "subject-next");
}

// The crew the window started from with the game's crew now as its preview, and our money.
void TouchCrew_Stats(aref game, aref view)
{
	aref stats;
	makearef(stats, view.stats);
	aref men = TouchLib_Stat(stats, "", game.base, "crew");
	if (sti(game.aboard) != sti(game.base)) men.preview = game.aboard;
	if (game.purse == "") return;
	aref purse = TouchLib_Stat(stats, "", game.purse, "money");
	purse.tone = "gold";
}

void TouchCrew_List(aref game, aref deal, aref view)
{
	aref list;
	makearef(list, view.list);
	list.label = "";
	TouchCrew_Column(list, "crew.on_ship", "ship");
	TouchCrew_Column(list, "crew.in_tavern", "tavern");
	TouchCrew_Column(list, "trade.price", "money");
	object row = {id = "0", name = UiText("crew.sailors"), meta = game.info, selected = true};
	row.image.kind = "icon";
	row.image.icon = "crew";
	row.cells.c0 = game.aboard;
	row.cells.c1 = game.ashore;
	row.cells.c2.text = game.price;
	row.cells.c2.bold = true;
	object q = {value = deal.value, min = 0, max = deal.high, zones = true, origin = game.base,
	            lessLabel = UiText("crew.fire"), moreLabel = UiText("crew.hire"), label = UiText("crew.on_ship"),
	            allLabel = UiText("common.all"), summary = "-", disabled = !sti(game.edit)};
	q.steps.s0 = 1;
	q.steps.s1 = 10;
	aref at;
	makearef(at, row.q);
	CopyAttributes(at, &q);
	aref rows;
	makearef(rows, list.rows);
	at = TouchLib_Add(rows, "r");
	CopyAttributes(at, &row);
	// the row only shows the crew: its amount is always open
	UiOn("crew", "row");
}

void TouchCrew_Column(aref list, string label, string icon)
{
	aref cols;
	makearef(cols, list.cols);
	aref at = TouchLib_Add(cols, "c");
	at.label = UiText(label);
	at.icon = icon;
}

void TouchCrew_Quality(aref game, string side, aref view)
{
	TouchLib_Copy(view, side, game, side);
	aref at;
	makearef(at, view.(side));
	at.moraleLabel = UiText("ship.morale");
	if (!CheckAttribute(at, "groups")) at.groups = "";
}

// «Нанять / Уволить N» with the game's cost of it.
void TouchCrew_Primary(aref game, aref deal, aref view)
{
	view.primary = "";
	if (!CheckAttribute(deal, "button")) return;
	TouchLib_Copy(view, "primary", deal, "button");
	aref primary;
	makearef(primary, view.primary);
	primary.sub = "";
	if (sti(game.cost) > 0) primary.sub = TouchLib_Thousands(sti(game.cost));
}

void TouchCrew_Tap()
{
	aref ui = GetEventData();
	if (ui.screen != "crew") return;
	string id = GetEventData(), verb = GetEventData(), value = GetEventData();
	aref game;
	makearef(game, TouchCrew.game);
	if (id == "header-back") { TouchAdpCrew_Leave(ui); return; }
	if (id == "crew-offer") { TouchAdpCrew_Offer(ui, sti(value)); UiHold(TouchCrew_Wait); return; }
	if (id == "crew-commit") { TouchAdpCrew_Commit(ui, sti(value)); UiHold(TouchCrew_Wait); return; }
	if (id == "crew") return;
	if (id != "header") UiError("crew: no tap of " + id);
	int ship = TouchCrew_Current(game) + 1;
	if (verb == "subject-prev") ship = TouchCrew_Current(game) - 1;
	TouchAdpCrew_Ship(ui, TouchLib_TapIndex("" + ship, TouchLib_Count(game, "ships"), "the ship"));
	UiHold(TouchCrew_Wait);
}
