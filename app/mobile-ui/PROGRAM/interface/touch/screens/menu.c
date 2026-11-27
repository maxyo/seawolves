// SPDX-License-Identifier: GPL-3.0-only
// The menus: the title's column over the game's menu scene, the pause menu over the paused game, the plate of a
// quick save being written and the death screen. The look's mode ("title", "pause", "saving", "over") says which.
// The title's commands and the quick save and load are the engine's (UiCommand); what the death screen chose the
// title opens once (a note, UiPass and UiTake).

#event_handler("UiBuild", "TouchMenu_Build");
#event_handler("UiTap", "TouchMenu_Tap");

object TouchMenu;

aref TouchMenu_Button(aref list, string id, string label)
{
	aref at = TouchLib_Add(list, "b");
	at.id = id;
	at.label = label;
	at.busy = UiPending(id);
	UiOn(id, "");
	return at;
}

// A command of the title; the save list it opens loads a record without a question (no game is in progress).
void TouchMenu_TitleCommand(string command)
{
	if (command == "Load")
	{
		object from = {from = "title"};
		UiPass("saves", &from);
	}
	TouchLib_Command("title", "command", command);
}

void TouchMenu_Title(aref ui)
{
	object game;
	TouchAdpMenu_Title(ui, &game);
	bool any = game.last != "";
	aref primary;
	makearef(primary, TouchMenu.primary);
	object go = {id = "continue", label = UiText("menu.continue"), sub = game.last, disabled = !any, reason = "",
	             busy = UiPending("continue")};
	if (!any) go.reason = UiText("menu.no_saves");
	CopyAttributes(primary, &go);
	UiOn("continue", "");
	aref buttons;
	makearef(buttons, TouchMenu.buttons);
	TouchMenu_Button(buttons, "new", UiText("menu.new_game"));
	TouchMenu_Button(buttons, "load", UiText("menu.load"));
	TouchMenu_Button(buttons, "options", UiText("menu.settings"));
	TouchMenu_Button(buttons, "credits", UiText("menu.credits"));
	object leave = {id = "exit", label = UiText("menu.exit"), busy = UiPending("exit")};
	aref at;
	makearef(at, TouchMenu.exit);
	CopyAttributes(at, &leave);
	UiOn("exit", "");
	object about = {id = "about", label = UiText("menu.about")};
	makearef(at, TouchMenu.about);
	CopyAttributes(at, &about);
	UiOn("about", "");
	// Chosen on the death screen, or asked by the host: done once the title is up.
	object asked;
	if (!UiTake("title", &asked)) return;
	if (asked.intent == "Continue" && !any) UiToast(UiText("menu.no_saves"));
	else { if (asked.intent != "") TouchMenu_TitleCommand(asked.intent); }
}

// «Шарль де Мор · Сен-Пьер, улицы · 1 окт. 1654»
string TouchMenu_Line(aref game)
{
	string line = game.game;
	if (sti(game.month) >= 1 && sti(game.month) <= 12)
		line += " · " + game.day + " " + UiText("common.month_" + game.month) + " " + game.year;
	return line;
}

void TouchMenu_Tile(aref list, aref game, string id)
{
	if (!CheckAttribute(game, "buttons." + id)) return;
	aref button;
	makearef(button, game.buttons.(id));
	aref at = TouchMenu_Button(list, id, button.label);
	at.variant = "secondary";
	at.disabled = !sti(button.can);
	at.reason = button.reason;
}

// Quick save and quick load, the game's own keys pressed by the engine once the menu has resumed: offered while the
// menu can resume, the save where the game lets the player save, the load while its record exists.
void TouchMenu_Quick(aref list, aref game, string id, bool can, string reason)
{
	aref at = TouchMenu_Button(list, id, UiText("menu." + TouchLib_Cut(id, 0, 5) + "_" + TouchLib_Cut(id, 6, strlen(id))));
	at.variant = "secondary";
	at.disabled = !can;
	at.reason = "";
	if (!can && sti(game.resume.can)) at.reason = reason;
	at.hold = id == "quick-load";
}

void TouchMenu_Pause(aref ui)
{
	object game;
	TouchAdpMenu_Pause(ui, &game);
	TouchMenu.line = TouchMenu_Line(&game);
	object resume = {id = "resume", label = game.resume.label, variant = "primary", disabled = !sti(game.resume.can),
	                 reason = game.resume.reason, busy = UiPending("resume")};
	aref at;
	makearef(at, TouchMenu.resume);
	CopyAttributes(at, &resume);
	UiOn("resume", "");
	bool resumes = sti(game.resume.can);
	aref buttons;
	makearef(buttons, TouchMenu.buttons);
	TouchMenu_Tile(buttons, &game, "save");
	TouchMenu_Tile(buttons, &game, "load");
	bool saves = false;
	string saveReason = "";
	if (CheckAttribute(&game, "buttons.save"))
	{
		saves = sti(game.buttons.save.can);
		saveReason = game.buttons.save.reason;
	}
	TouchMenu_Quick(buttons, &game, "quick-save", resumes && saves, saveReason);
	TouchMenu_Quick(buttons, &game, "quick-load", resumes && sti(game.quickLoad), UiText("menu.no_quick_save"));
	TouchMenu_Tile(buttons, &game, "options");
	TouchMenu_Tile(buttons, &game, "exit");
	// The game's label of its program exit («Выход») would promise leaving the game.
	if (CheckAttribute(&game, "buttons.exit") && !sti(game.home))
	{
		aref last = GetAttributeN(buttons, GetAttributesNum(buttons) - 1);
		last.label = UiText("menu.exit");
	}
	// Optional in game variants; shown only while the game offers it.
	if (CheckAttribute(&game, "buttons.updates"))
	{
		if (sti(game.buttons.updates.can)) TouchMenu_Tile(buttons, &game, "updates");
	}
	if (!CheckAttribute(&game, "confirm"))
	{
		TouchMenu.confirm = "";
		return;
	}
	// The game's question of its own exit to the main menu is kept; its question of the program exit is not what
	// happens here, so ours replaces it.
	object confirm = {preset = "danger", title = game.confirm.question, text = "", yes = UiText("menu.exit"),
	                  no = game.confirm.no};
	if (!sti(game.home)) confirm.title = UiText("menu.exit_to_main_menu_question");
	if (confirm.no == "") confirm.no = UiText("common.cancel");
	makearef(at, TouchMenu.confirm);
	CopyAttributes(at, &confirm);
	UiOn("confirm-confirm", "");
	UiOn("confirm-cancel", "");
	if (sti(game.confirm.canNo)) UiModal("confirm", "confirm-cancel");
	else UiModal("confirm", "");
}

void TouchMenu_Saving(aref ui)
{
	object game;
	TouchAdpMenu_Saving(ui, &game);
	TouchMenu.title = game.title;
	if (game.title == "") TouchMenu.title = UiText("menu.quick_save");
	TouchMenu.text = game.text;
	UiOn("header-back", ""); // the game closes it once the record is written
}

void TouchMenu_Over(aref ui)
{
	object game;
	TouchAdpMenu_Over(ui, &game);
	TouchMenu.title = UiText("menu.game_over_" + game.kind);
	aref actions;
	makearef(actions, TouchMenu.actions);
	TouchMenu_Button(actions, "go-load", UiText("menu.load"));
	TouchMenu_Button(actions, "go-options", UiText("menu.settings"));
	TouchMenu_Button(actions, "go-title", UiText("menu.main_menu"));
	UiOn("header-back", ""); // the game leaves this screen by itself
}

void TouchMenu_Build()
{
	aref ui = GetEventData();
	if (ui.screen != "menu") return;
	DeleteAttribute(&TouchMenu, "");
	if (ui.look.mode == "title") TouchMenu_Title(ui);
	if (ui.look.mode == "pause") TouchMenu_Pause(ui);
	if (ui.look.mode == "saving") TouchMenu_Saving(ui);
	if (ui.look.mode == "over") TouchMenu_Over(ui);
	UiShow(&TouchMenu);
}

void TouchMenu_PauseTap(aref ui, string id)
{
	object game;
	TouchAdpMenu_Pause(ui, &game);
	bool resumes = sti(game.resume.can);
	if (id == "resume") { if (resumes) TouchAdpMenu_Resume(ui); return; }
	if (id == "quick-save" || id == "quick-load")
	{
		bool can = resumes && sti(game.quickLoad);
		if (id == "quick-save") can = resumes && CheckAttribute(&game, "buttons.save");
		if (id == "quick-save" && can) can = sti(game.buttons.save.can);
		if (!can) return;
		TouchAdpMenu_Resume(ui);
		TouchLib_Command("quick", "which", TouchLib_Cut(id, 6, strlen(id)));
		return;
	}
	if (id == "confirm-confirm")
	{
		// Before the answer: the host must know why the engine ends before it ends.
		if (!sti(game.home)) TouchLib_Command("host", "action", "app-exit-to-main-menu");
		TouchAdpMenu_Answer(ui, true);
		return;
	}
	if (id == "confirm-cancel") { if (sti(game.confirm.canNo)) TouchAdpMenu_Answer(ui, false); return; }
	TouchAdpMenu_Press(ui, id);
}

void TouchMenu_Tap()
{
	aref ui = GetEventData();
	if (ui.screen != "menu") return;
	string id = GetEventData(), verb = GetEventData(), value = GetEventData();
	if (id == "header-back") return;
	if (ui.look.mode == "pause") { TouchMenu_PauseTap(ui, id); return; }
	if (ui.look.mode == "over")
	{
		// The choice ends the death picture at once; the title screen opens it.
		string intent = "";
		if (id == "go-load") intent = "Load";
		if (id == "go-options") intent = "Options";
		object asked = {intent = intent};
		UiPass("title", &asked);
		TouchAdpMenu_End(ui);
		return;
	}
	if (id == "about") { TouchLib_Command("host", "action", "app-about"); return; }
	if (id == "continue") { TouchMenu_TitleCommand("Continue"); return; }
	if (id == "new") { TouchMenu_TitleCommand("New"); return; }
	if (id == "load") { TouchMenu_TitleCommand("Load"); return; }
	if (id == "options") { TouchMenu_TitleCommand("Options"); return; }
	if (id == "credits") { TouchMenu_TitleCommand("Credits"); return; }
	if (id == "exit") TouchMenu_TitleCommand("Exit");
}
