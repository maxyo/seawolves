// SPDX-License-Identifier: GPL-3.0-only
// The sail emblem: a ribbon of emblems with ‹ ›, a tap chooses one in the game, and one primary does the work the game
// offers for it, painting it or taking the one on the sails off (pack screen s-sail-emblem).

#event_handler("UiBuild", "TouchSails_Build");
#event_handler("UiTap", "TouchSails_Tap");

// The adapter's state of the last build (the taps act on what it showed) and the view.
object TouchSails;
object TouchSails_View;

void TouchSails_Build()
{
	aref ui = GetEventData();
	if (ui.screen != "sails") return;
	aref game;
	makearef(game, TouchSails.game);
	TouchAdpSails_State(ui, game);
	aref view;
	makearef(view, TouchSails_View);
	DeleteAttribute(view, "");
	view.title = game.title;
	if (view.title == "") view.title = UiText("sails.title");
	int current = TouchSails_Current(game);
	aref emblems;
	makearef(emblems, view.emblems);
	for (int k = 0; k < TouchLib_Count(game, "emblems"); k++)
	{
		aref emblem = TouchLib_Item(game, "emblems", k);
		object item = {id = emblem.key, texture = emblem.texture, label = emblem.label, selected = emblem.selected,
		               applied = emblem.applied, disabled = !sti(emblem.can)};
		aref at = TouchLib_Add(emblems, "e");
		CopyAttributes(at, &item);
	}
	if (GetAttributesNum(emblems) == 0) view.emblems = "";
	view.appliedLabel = UiText("sails.applied");
	view.prev = current > 0;
	view.next = current + 1 < TouchLib_Count(game, "emblems");
	aref stats;
	makearef(stats, view.stats);
	aref purse = TouchLib_Stat(stats, "", TouchLib_Thousands(sti(game.purse)), "money");
	purse.tone = "gold";
	TouchSails_Primary(game, view);
	UiOn("emblems", "emblem");
	UiOn("emblem-prev", "");
	UiOn("emblem-next", "");
	UiOn("header-back", "");
	UiShow(view);
}

int TouchSails_Current(aref game)
{
	return TouchLib_Current(game, "emblems", "sail emblem: the emblems");
}

// The game charges the price without asking for the money: nothing of ours stands before its work.
void TouchSails_Primary(aref game, aref view)
{
	view.primary = "";
	if (!CheckAttribute(game, "work")) return;
	object primary = {label = UiText("sails.apply"), sub = TouchLib_Thousands(sti(game.price)),
	                  disabled = !sti(game.work.can), reason = game.work.reason, busy = UiPending("primary")};
	if (sti(game.work.remove)) primary.label = UiText("sails.remove");
	aref at;
	makearef(at, view.primary);
	CopyAttributes(at, &primary);
	UiOn("primary", "");
}

// An emblem the game can make current and is not already.
void TouchSails_Choose(aref ui, aref game, int k)
{
	if (k < 0 || k >= TouchLib_Count(game, "emblems")) return;
	aref emblem = TouchLib_Item(game, "emblems", k);
	if (!sti(emblem.can) || sti(emblem.selected)) return;
	TouchAdpSails_Choose(ui, k);
	UiHold();
}

void TouchSails_Tap()
{
	aref ui = GetEventData();
	if (ui.screen != "sails") return;
	string id = GetEventData(), verb = GetEventData(), value = GetEventData();
	aref game;
	makearef(game, TouchSails.game);
	if (id == "header-back") { TouchAdpSails_Leave(ui); return; }
	if (id == "primary") { TouchAdpSails_Work(ui); UiHold(); return; }
	int current = TouchSails_Current(game);
	if (id == "emblem-prev") { TouchSails_Choose(ui, game, current - 1); return; }
	if (id == "emblem-next") { TouchSails_Choose(ui, game, current + 1); return; }
	if (id != "emblems") UiError("sail emblem: no tap of " + id);
	// An emblem gone since the frame the player tapped is -1: the tap ends.
	TouchSails_Choose(ui, game, TouchLib_Find(game, "emblems", "key", value));
}
