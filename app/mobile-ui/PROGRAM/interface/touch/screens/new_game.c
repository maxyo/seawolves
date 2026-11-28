// SPDX-License-Identifier: GPL-3.0-only
// New game: the game's options on the left (difficulty, the sea battle type, its switches), the hero, the
// occupation and «start» on the right; the game's own windows over it as a dialog with its buttons. The game's
// question whether to keep the existing profile folder is answered for the player: the port has no profiles.

#event_handler("UiBuild", "TouchNewGame_Build");
#event_handler("UiTap", "TouchNewGame_Tap");

object TouchNewGame;
object TouchNewGame_Game; // the adapter's state of the last build
aref TouchNewGame_Stepper(aref at, aref step, string id, string label)
{
	at.kind = "stepper";
	at.id = id;
	at.label = label;
	at.text = step.text;
	at.left = step.left;
	at.right = step.right;
	UiOn(id + "-left", "");
	UiOn(id + "-right", "");
	return at;
}

void TouchNewGame_View(aref game)
{
	DeleteAttribute(&TouchNewGame, "");
	TouchNewGame.title = UiText("menu.new_game_title");
	aref rows;
	makearef(rows, TouchNewGame.rows);
	aref at;
	aref step;
	if (CheckAttribute(game, "difficulty"))
	{
		makearef(step, game.difficulty);
		TouchNewGame_Stepper(TouchLib_Add(rows, "r"), step, "difficulty", UiText("menu.difficulty"));
	}
	if (CheckAttribute(game, "battle"))
	{
		at = TouchLib_Add(rows, "r");
		at.kind = "segment";
		at.id = "battle";
		at.label = game.battle.label;
		at.selected = game.battle.selected;
		at.disabled = !sti(game.battle.can);
		aref options;
		makearef(options, game.battle.options);
		for (int k = 0; k < GetAttributesNum(options); k++)
		{
			aref option = GetAttributeN(options, k);
			at.options.("o" + k) = option.label;
		}
		UiOn("battle", "change");
	}
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
	if (CheckAttribute(game, "setup"))
	{
		makearef(at, TouchNewGame.setup);
		makearef(step, game.setup);
		TouchNewGame_Stepper(at, step, "setup", "");
		// The game captions the stock occupations with a fragment of a sentence: ours title them by their key.
		string key = game.setup.occupation;
		if (TouchLib_StartsWith(key, "HeroType_") && strlen(key) == 10)
		{
			string n = TouchLib_Cut(key, 9, 10);
			if (n == "1" || n == "2" || n == "3" || n == "4") at.text = UiText("menu.hero_type_" + n);
		}
		at.note = game.setup.note;
	}
	object start = {label = UiText("menu.start"), disabled = !sti(game.start.can), reason = game.start.reason,
	                busy = UiPending("start")};
	makearef(at, TouchNewGame.start);
	CopyAttributes(at, &start);
	UiOn("start", "");
	UiOn("header-back", "");
	if (!CheckAttribute(game, "window")) return;
	TouchNewGame.modal.title = game.window.text;
	TouchNewGame.modal.text = "";
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
	if (id == "start") { TouchAdpNewGame_Start(ui); return; }
	if (id == "bio") { UiRead(game.hero.bioTitle, game.hero.bio); return; }
	if (id == "battle") { TouchAdpNewGame_Battle(ui, UiAmount(value, "the sea battle type")); return; }
	if (id == "difficulty-left" || id == "difficulty-right" || id == "setup-left" || id == "setup-right")
	{
		string which = "setup";
		if (TouchLib_StartsWith(id, "difficulty")) which = "difficulty";
		TouchAdpNewGame_Step(ui, which, TouchLib_EndsWith(id, "-right"));
		return;
	}
	if (TouchLib_StartsWith(id, "modal-"))
	{
		TouchAdpNewGame_Window(ui, UiAmount(TouchLib_Cut(id, 6, strlen(id)), "the window's button"));
		return;
	}
	TouchAdpNewGame_Toggle(ui, id, value == "1");
}
