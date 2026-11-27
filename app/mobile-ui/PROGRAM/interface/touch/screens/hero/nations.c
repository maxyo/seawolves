// SPDX-License-Identifier: GPL-3.0-only
// Nations, a hero section: the relations between the nations with their legend, the hero's standing with each, the
// flag the ship will raise and the game's help of both.

#event_handler("UiBuild", "TouchNations_Build");
#event_handler("UiTap", "TouchNations_Tap");

object TouchNations;      // the adapter's state of the last build (.game)
object TouchNations_View;

// s: how long a move waits for the game's answer; past it the view shows what the game has.
float TouchNations_Wait = 0.5;
// The legend's relations, each with its icon rel-<kind> and its string key nations.<kind>.
string TouchNations_Kinds[3] = {"ally", "neutral", "war"};

// A nation's picture: our flag icon, else the game's image.
void TouchNations_Picture(aref at, aref nation)
{
	at.icon = nation.icon;
	at.image = "";
	if (nation.icon != "") return;
	aref image;
	makearef(image, at.image);
	aref from;
	makearef(from, nation.image);
	CopyAttributes(image, from);
}

void TouchNations_Matrix(aref game, aref view)
{
	aref columns, rows;
	makearef(columns, view.columns);
	makearef(rows, view.rows);
	int count = TouchLib_Count(game, "nations");
	for (int k = 0; k < count; k++)
	{
		aref nation = TouchLib_Item(game, "nations", k);
		aref column = TouchLib_Add(columns, "c");
		column.label = nation.label;
		TouchNations_Picture(column, nation);
		aref row = TouchLib_Add(rows, "r");
		row.label = nation.label;
		TouchNations_Picture(row, nation);
		aref cells;
		makearef(cells, row.cells);
		for (int j = 0; j < count; j++)
		{
			aref relation = TouchLib_Item(nation, "relations", j);
			aref cell = TouchLib_Add(cells, "c");
			if (sti(relation.self)) { cell.self = true; continue; }
			cell.label = nation.label + " — " + relation.label;
			cell.icon = "";
			cell.image = "";
			if (relation.kind != "")
			{
				cell.icon = "rel-" + relation.kind;
				continue;
			}
			aref image;
			makearef(image, cell.image);
			aref from;
			makearef(from, relation.image);
			CopyAttributes(image, from);
		}
	}
	aref legend;
	makearef(legend, view.legend);
	for (int i, string kind: TouchNations_Kinds)
	{
		aref key = TouchLib_Add(legend, "l");
		key.icon = "rel-" + kind;
		key.label = UiText("nations." + kind);
	}
}

void TouchNations_Standing(aref game, aref view)
{
	view.reputationTitle = game.standingTitle;
	aref list;
	makearef(list, view.reputation);
	for (int k = 0; k < TouchLib_Count(game, "standing"); k++)
	{
		aref standing = TouchLib_Item(game, "standing", k);
		aref nation = TouchLib_Item(game, "nations", sti(standing.nation));
		aref at = TouchLib_Add(list, "p");
		at.label = nation.label;
		at.value = standing.word;
		at.tone = "";
		TouchNations_Picture(at, nation);
		if (CheckAttribute(standing, "score"))
		{
			int score = sti(standing.score);
			if (score < -100) score = -100;
			if (score > 100) score = 100;
			at.bar = (score + 100) / 2;
			if (standing.tone == "hostile") at.tone = "danger";
			if (standing.tone == "good") at.tone = "ok";
		}
	}
}

void TouchNations_Flags(aref game, aref view)
{
	view.flagTitle = UiText("nations.flag");
	view.flagsDisabled = !sti(game.flags);
	aref flags;
	makearef(flags, view.flags);
	for (int k = 0; k < TouchLib_Count(game, "nations"); k++)
	{
		aref nation = TouchLib_Item(game, "nations", k);
		if (nation.icon != "")
		{
			object flag = {id = "" + k, icon = nation.icon, label = nation.label, selected = sti(nation.flown),
			               disabled = !sti(nation.flag)};
			aref at = TouchLib_Add(flags, "f");
			CopyAttributes(at, &flag);
		}
		if (sti(nation.flown)) view.flag.label = nation.label;
	}
	if (!CheckAttribute(game, "apply")) return;
	object apply = {id = "flag-apply", label = game.apply.label, variant = "primary", disabled = !sti(game.apply.can),
	                reason = game.apply.reason};
	aref at;
	makearef(at, view.apply);
	CopyAttributes(at, &apply);
}

void TouchNations_Show(aref game)
{
	DeleteAttribute(&TouchNations_View, "");
	aref view;
	makearef(view, TouchNations_View);
	view.title = game.title;
	view.headerSection = "nations";
	TouchNations_Matrix(game, view);
	TouchNations_Standing(game, view);
	view.helpReputation = sti(game.help.standing);
	view.helpFlag = sti(game.help.flag);
	TouchNations_Flags(game, view);
}

void TouchNations_Build()
{
	aref ui = GetEventData();
	if (ui.screen != "nations") return;
	DeleteAttribute(&TouchNations, "");
	aref game;
	makearef(game, TouchNations.game);
	TouchAdpNations_State(ui, game);
	TouchNations_Show(game);
	UiOn("header", "section");
	UiOn("header-back", "");
	UiOn("flags", "pick");
	UiOn("flag-apply", "");
	UiOn("help-reputation", "");
	UiOn("help-flag", "");
	UiShow(&TouchNations_View);
}

void TouchNations_Tap()
{
	aref ui = GetEventData();
	if (ui.screen != "nations") return;
	string id = GetEventData(), verb = GetEventData(), value = GetEventData();
	if (id == "header")
	{
		if (value == "nations") return;
		// The section tapped is drawn chosen from this frame; the game opens it.
		TouchNations_View.headerSection = value;
		UiPredict(&TouchNations_View);
		TouchAdpNations_Section(ui, value);
		UiHold(TouchNations_Wait);
		return;
	}
	if (id == "header-back") { TouchAdpNations_Leave(ui); return; }
	if (id == "help-reputation" || id == "help-flag")
	{
		string part = "standing";
		if (id == "help-flag") part = "flag";
		object help;
		TouchAdpNations_Help(ui, part, &help);
		UiRead(help.title, help.text);
		return;
	}
	aref game;
	makearef(game, TouchNations.game);
	if (id == "flag-apply")
	{
		if (!CheckAttribute(game, "apply")) return;
		if (!sti(game.apply.can)) return;
		TouchAdpNations_Raise(ui);
		UiHold(TouchNations_Wait);
		return;
	}
	TouchAdpNations_Pick(ui, TouchLib_TapIndex(value, TouchLib_Count(game, "nations"), "the flag"));
	UiHold(TouchNations_Wait);
}
