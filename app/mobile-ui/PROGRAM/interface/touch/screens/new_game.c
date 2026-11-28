// SPDX-License-Identifier: GPL-3.0-only
// New game: the game's options on the left (its ‹ › choices, one-of-several choices, its switches), the hero, his
// choices and the starts on the right; the game's own windows over it as a dialog with its buttons. The game's
// question whether to keep the existing profile folder is answered for the player: the port has no profiles.

#event_handler("UiBuild", "TouchNewGame_Build");
#event_handler("UiTap", "TouchNewGame_Tap");

object TouchNewGame;
object TouchNewGame_Game; // the adapter's state of the last build

void TouchNewGame_Stepper(aref at, aref row)
{
	at.kind = "stepper";
	at.id = row.key;
	at.label = row.label;
	at.text = row.text;
	at.left = row.left;
	at.right = row.right;
	UiOn(row.key + "-left", "");
	UiOn(row.key + "-right", "");
	if (!CheckAttribute(row, "occupation")) return;
	// The game captions the stock occupations with a fragment of a sentence: ours title them by their key.
	string key = row.occupation;
	if (TouchLib_StartsWith(key, "HeroType_") && strlen(key) == 10)
	{
		string n = TouchLib_Cut(key, 9, 10);
		if (n == "1" || n == "2" || n == "3" || n == "4") at.text = UiText("menu.hero_type_" + n);
	}
}

void TouchNewGame_Segment(aref at, aref row)
{
	at.kind = "segment";
	at.id = row.key;
	at.label = row.label;
	at.selected = row.selected;
	at.disabled = !sti(row.can);
	aref options;
	makearef(options, row.options);
	for (int k = 0; k < GetAttributesNum(options); k++)
	{
		aref option = GetAttributeN(options, k);
		at.options.("o" + k) = option.label;
	}
	UiOn(row.key, "change");
}

// The ways to start: the first is «start», the others «start-<k>».
void TouchNewGame_Starts(aref game)
{
	aref at;
	aref others;
	makearef(others, TouchNewGame.starts);
	for (int k = 0; k < TouchLib_Count(game, "starts"); k++)
	{
		aref start = TouchLib_Item(game, "starts", k);
		string id = "start";
		if (k == 0) makearef(at, TouchNewGame.start);
		else
		{
			id = "start-" + k;
			at = TouchLib_Add(others, "s");
			at.id = id;
		}
		at.label = start.label;
		if (start.label == "") at.label = UiText("menu.start");
		at.disabled = !sti(start.can);
		at.reason = start.reason;
		at.busy = UiPending(id);
		UiOn(id, "");
	}
	if (GetAttributesNum(others) == 0) DeleteAttribute(&TouchNewGame, "starts");
}

void TouchNewGame_View(aref game)
{
	DeleteAttribute(&TouchNewGame, "");
	TouchNewGame.title = UiText("menu.new_game_title");
	aref rows;
	makearef(rows, TouchNewGame.rows);
	aref traits;
	makearef(traits, TouchNewGame.traits);
	aref at;
	bool setup = false;
	for (int r = 0; r < TouchLib_Count(game, "rows"); r++)
	{
		aref row = TouchLib_Item(game, "rows", r);
		if (row.kind == "choice")
		{
			TouchNewGame_Segment(TouchLib_Add(rows, "r"), row);
			continue;
		}
		if (!sti(row.hero))
		{
			TouchNewGame_Stepper(TouchLib_Add(rows, "r"), row);
			continue;
		}
		// The hero's first choice heads his column, the others follow it.
		if (setup) at = TouchLib_Add(traits, "t");
		else makearef(at, TouchNewGame.setup);
		setup = true;
		TouchNewGame_Stepper(at, row);
		at.note = row.note;
	}
	if (GetAttributesNum(traits) == 0) DeleteAttribute(&TouchNewGame, "traits");
	aref toggles;
	makearef(toggles, TouchNewGame.toggles);
	for (int t = 0; t < TouchLib_Count(game, "toggles"); t++)
	{
		aref toggle = TouchLib_Item(game, "toggles", t);
		at = TouchLib_Add(toggles, "t");
		at.kind = "toggle";
		at.id = toggle.key;
		at.label = toggle.label;
		at.on = toggle.on;
		at.disabled = !sti(toggle.can);
		at.reason = toggle.reason;
		UiOn(toggle.key, "change");
	}
	TouchNewGame.hero.name = game.hero.name;
	TouchNewGame.hero.nation = "";
	if (game.hero.nation != "")
		TouchNewGame.hero.nation = UiText("menu.nation_" + TouchLib_Cut(game.hero.nation, 7, strlen(game.hero.nation)));
	TouchLib_Copy(&TouchNewGame, "hero.image", game, "hero.portrait");
	TouchNewGame.hero.bio = game.hero.bio != "";
	UiOn("bio", "");
	TouchNewGame_Starts(game);
	UiOn("header-back", "");
	if (!CheckAttribute(game, "window")) return;
	TouchNewGame.modal.title = game.window.text;
	TouchNewGame.modal.text = "";
	if (CheckAttribute(game, "window.name"))
	{
		string field = game.window.name.label;
		if (field != "" && !TouchLib_EndsWith(field, ":")) field += ":";
		TouchNewGame.modal.text = field + " " + game.window.name.value;
	}
	aref actions;
	makearef(actions, TouchNewGame.modal.actions);
	for (int b = 0; b < TouchLib_Count(game, "window.buttons"); b++)
	{
		aref button = TouchLib_Item(game, "window.buttons", b);
		at = TouchLib_Add(actions, "a");
		at.id = "modal-" + b;
		at.label = button.label;
		at.variant = "secondary";
		if (b == 0) at.variant = "primary";
		at.disabled = !sti(button.can);
		UiOn("modal-" + b, "");
	}
	if (CheckAttribute(game, "window.name"))
	{
		at = TouchLib_Add(actions, "a");
		at.id = "modal-name";
		at.label = UiText("ship.rename");
		at.variant = "secondary";
		at.disabled = false;
		UiOn("modal-name", "");
	}
	UiModal("modal", ""); // the game's window closes only by its buttons
}

void TouchNewGame_Build()
{
	aref ui = GetEventData();
	if (ui.screen != "new_game") return;
	aref game;
	makearef(game, TouchNewGame_Game);
	DeleteAttribute(game, "");
	TouchAdpNewGame_State(ui, game);
	if (sti(game.profile) && UiAnswer() != "waiting")
	{
		TouchAdpNewGame_Profile(ui);
		UiHold();
	}
	TouchNewGame_View(game);
	UiShow(&TouchNewGame);
}

void TouchNewGame_Tap()
{
	aref ui = GetEventData();
	if (ui.screen != "new_game") return;
	string id = GetEventData(), verb = GetEventData(), value = GetEventData();
	aref game;
	makearef(game, TouchNewGame_Game);
	if (id == "header-back") { TouchAdpNewGame_Cancel(ui); return; }
	if (id == "bio") { UiRead(game.hero.bioTitle, game.hero.bio); return; }
	if (id == "modal-name") { TouchAdpNewGame_Name(ui, game.window.name.label); return; }
	if (TouchLib_StartsWith(id, "modal-"))
	{
		TouchAdpNewGame_Window(ui, UiAmount(TouchLib_Cut(id, 6, strlen(id)), "the window's button"));
		return;
	}
	int k;
	for (k = 0; k < TouchLib_Count(game, "starts"); k++)
	{
		string start = "start";
		if (k > 0) start = "start-" + k;
		if (id != start) continue;
		aref way = TouchLib_Item(game, "starts", k);
		TouchAdpNewGame_Start(ui, way.key);
		return;
	}
	for (k = 0; k < TouchLib_Count(game, "rows"); k++)
	{
		aref row = TouchLib_Item(game, "rows", k);
		if (row.kind == "choice" && id == row.key)
		{
			TouchAdpNewGame_Choose(ui, row.key, UiAmount(value, "the choice"));
			return;
		}
		if (row.kind == "step" && (id == row.key + "-left" || id == row.key + "-right"))
		{
			TouchAdpNewGame_Step(ui, row.key, id == row.key + "-right");
			return;
		}
	}
	TouchAdpNewGame_Toggle(ui, id, value == "1");
}
