// SPDX-License-Identifier: GPL-3.0-only
// The tavern's table games: the felt with the opponent and his line, the bank, his row on top and ours below, the cup
// or the deck, and the moves the game allows now. The game plays the round; a tap sends it the player's move.

#event_handler("UiBuild", "TouchTable_Build");
#event_handler("UiTap", "TouchTable_Tap");

object TouchTable;      // the adapter's state of the last build (.game)
object TouchTable_View;

// Pip centres (% of the die) of faces 1..6, "x,y" pairs.
string TouchTable_Pips[6] = {"50,50", "28,28 72,72", "26,26 50,50 74,74", "26,26 74,26 26,74 74,74",
                             "26,26 74,26 50,50 26,74 74,74", "28,24 72,24 28,50 72,50 28,76 72,76"};
// A slight turn of each piece, so that a row looks laid by hand.
int TouchTable_Tilt[8] = {-3, 2, -2, 3, -1, 2, -3, 1};

void TouchTable_Die(aref piece, aref at, int tilt)
{
	at.kind = "die";
	at.rot = tilt;
	object pips;
	DeleteAttribute(&pips, "");
	TouchLib_Words(TouchTable_Pips[sti(piece.face) - 1], &pips);
	for (int k = 0; k < GetAttributesNum(&pips); k++)
	{
		aref word = GetAttributeN(&pips, k);
		string slot = "p" + k;
		at.pips.(slot) = word.text;
	}
	if (sti(piece.lifted)) at.lift = true;
}

void TouchTable_Card(aref piece, aref at, int tilt)
{
	at.kind = piece.kind;
	at.rot = tilt;
	if (piece.kind != "card") return;
	at.suit = piece.suit;
	at.red = piece.suit == "hearts" || piece.suit == "diamonds";
	at.rank = piece.rank;
	if (strlen(piece.rank) == 1 && !TouchLib_Digit(piece.rank, 0)) at.rank = UiText("games.rank_" + piece.rank);
}

// A row of the game's pieces into view.<side>.pieces; `turn` shifts the tilts of the opponent's row.
void TouchTable_Row(aref game, string side, aref view, int turn, bool dice)
{
	aref row, pieces;
	makearef(row, game.(side));
	makearef(pieces, view.pieces);
	for (int k = 0; k < GetAttributesNum(row); k++)
	{
		aref piece = GetAttributeN(row, k);
		aref at = TouchLib_Add(pieces, "p");
		if (!dice)
		{
			TouchTable_Card(piece, at, TouchTable_Tilt[k % 8] * 2);
			continue;
		}
		TouchTable_Die(piece, at, TouchTable_Tilt[(k + turn) % 8]);
		if (piece.mark != "" && !sti(game.open))
		{
			at.action = "mark";
			at.value = piece.mark;
			at.disabled = !sti(game.moves.mark);
			at.reason = UiText("games.wait");
		}
	}
}

string TouchTable_Count(aref game, string count, bool dice)
{
	string value = game.(count);
	if (value == "") return "";
	if (dice) return UiText("games.combo_" + value);
	return TouchLib_Counted(sti(value), "games.point");
}

// A button of the bar; one the game does not allow now says why (`reason`, a string key; "" none).
void TouchTable_Button(aref view, string id, string label, string sub, string variant, bool can, string reason)
{
	aref buttons;
	makearef(buttons, view.buttons);
	aref at = TouchLib_Add(buttons, "b");
	at.id = id;
	at.label = UiText(label);
	at.sub = sub;
	at.variant = variant;
	at.disabled = !can;
	at.reason = "";
	if (!can && reason != "") at.reason = UiText(reason);
	at.busy = UiPending(id);
}

void TouchTable_Build()
{
	aref ui = GetEventData();
	if (ui.screen != "table") return;
	DeleteAttribute(&TouchTable, "");
	aref game;
	makearef(game, TouchTable.game);
	TouchAdpTable_State(ui, game);
	bool dice = game.kind == "dice";
	bool open = sti(game.open);
	DeleteAttribute(&TouchTable_View, "");
	aref view;
	makearef(view, TouchTable_View);
	view.kind = game.kind;
	view.title = UiText(ui.look.title);
	aref stats;
	makearef(stats, view.stats);
	TouchLib_Stat(stats, UiText("games.stake"), TouchLib_Thousands(sti(game.stake)), "");
	TouchLib_Stat(stats, UiText("games.money"), TouchLib_Thousands(sti(game.money)), "");
	view.locked = !open;
	view.table.opponent = game.opponent;
	view.table.portrait = game.portrait;
	view.table.line = game.line;
	view.table.bank = "";
	if (sti(game.bank) > 0) view.table.bank = TouchLib_Thousands(sti(game.bank));
	view.table.coins = game.coins;

	aref side;
	makearef(side, view.top);
	TouchTable_Row(game, "theirs", side, 3, dice);
	side.label = TouchTable_Count(game, "theirCount", dice);
	side.size = 90;
	if (dice)
	{
		side.size = 44;
		side.below = true;
	}
	makearef(side, view.bottom);
	TouchTable_Row(game, "ours", side, 0, dice);
	side.label = TouchTable_Count(game, "ourCount", dice);
	side.size = 90;
	if (dice) side.size = 60;

	int throws = sti(game.moves.throw);
	if (sti(game.deck))
	{
		view.pack.p0.kind = "cup";
		if (!dice) view.pack.p0.kind = "deck";
		view.pack.p0.action = "press";
		view.pack.p0.value = "";
		view.pack.p0.disabled = !(throws > 0 || (open && sti(game.again)));
		view.pack.p0.reason = "";
		if (!open) view.pack.p0.reason = UiText("games.wait");
	}
	if (open)
	{
		string stake = UiText("games.stake_short") + " " + TouchLib_Thousands(sti(game.stake));
		if (sti(game.again))
		{
			TouchTable_Button(view, "leave", "games.leave", "", "secondary", true, "");
			TouchTable_Button(view, "again", "games.again", stake, "primary", true, "");
		}
		else
		{
			TouchTable_Button(view, "again", "games.again", "", "secondary", false, "");
			TouchTable_Button(view, "leave", "games.leave", "", "primary", true, "");
		}
	}
	else
	{
		if (dice)
		{
			if (sti(game.moves.pass)) TouchTable_Button(view, "pass", "games.keep", "", "primary", true, "");
			else
			{
				string sub = "";
				if (throws > 0) sub = TouchLib_Counted(throws, "games.die");
				TouchTable_Button(view, "throw", "games.roll", sub, "primary", throws > 0, "games.wait");
			}
		}
		else
		{
			TouchTable_Button(view, "pass", "games.enough", "", "secondary", sti(game.moves.pass), "games.wait");
			TouchTable_Button(view, "throw", "games.take", "", "primary", throws > 0, "games.wait");
		}
	}
	UiOn("bottom", "mark");
	UiOn("pack", "press");
	UiOn("throw", "");
	UiOn("pass", "");
	UiOn("again", "");
	UiOn("leave", "");
	UiOn("header-back", "");
	UiShow(&TouchTable_View);
}

void TouchTable_Tap()
{
	aref ui = GetEventData();
	if (ui.screen != "table") return;
	string id = GetEventData(), verb = GetEventData(), value = GetEventData();
	bool open = sti(TouchTable.game.open);
	if (id == "bottom")
	{
		aref ours;
		makearef(ours, TouchTable.game.ours);
		for (int k = 0; k < GetAttributesNum(ours); k++)
		{
			aref piece = GetAttributeN(ours, k);
			if (piece.mark != "" && piece.mark == value) { TouchAdpTable_Mark(ui, value); return; }
		}
		UiError("table game: no die " + value + " to mark");
	}
	if (id == "throw") { TouchAdpTable_Throw(ui); return; }
	if (id == "pass") { TouchAdpTable_Pass(ui); return; }
	if (id == "again") { TouchAdpTable_Again(ui); return; }
	if (id == "leave") { TouchAdpTable_Leave(ui); return; }
	if (id == "pack")
	{
		if (open) TouchAdpTable_Again(ui);
		else TouchAdpTable_Throw(ui);
		return;
	}
	// The header's back: the game lets the player leave only between rounds.
	if (open) TouchAdpTable_Leave(ui);
	else UiToast(UiText("games.round"));
}
