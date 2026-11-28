// SPDX-License-Identifier: GPL-3.0-only
// The title: «Продолжить» and the game's main menu commands as a column over the game's menu scene, the game's exit
// question when it asks one. What the death screen chose or the host asked (a note, UiTake) is done once the title is
// up.

#event_handler("UiBuild", "TouchTitle_Build");
#event_handler("UiTap", "TouchTitle_Tap");

object TouchTitle;

// A command of the game's; the save list it opens loads a record without a question (no game is in progress).
void TouchTitle_Command(aref ui, string id)
{
	if (id == "load")
	{
		object from = {from = "title"};
		UiPass("saves", &from);
	}
	TouchAdpTitle_Command(ui, id);
}

void TouchTitle_Button(aref game, string id, string label)
{
	if (!CheckAttribute(game, "commands." + id)) return;
	aref buttons;
	makearef(buttons, TouchTitle.buttons);
	aref at = TouchLib_Add(buttons, "b");
	at.id = id;
	at.label = label;
	at.busy = UiPending(id);
	UiOn(id, "");
}

// An intent of the death screen or the host: its command's name.
string TouchTitle_Intent(string intent)
{
	if (intent == "Continue") return "continue";
	if (intent == "Load") return "load";
	if (intent == "Options") return "options";
	UiError("title: no intent " + intent);
	return "";
}

// Chosen on the death screen, or asked by the host: done once the title is up.
void TouchTitle_Asked(aref ui, aref game)
{
	object asked;
	if (!UiTake("title", &asked)) return;
	if (asked.intent == "") return;
	string id = TouchTitle_Intent(asked.intent);
	if (id == "continue" && sti(game.noRecord)) { UiToast(UiText("menu.no_saves")); return; }
	if (!CheckAttribute(game, "commands." + id)) UiError("title: the game's main menu has no command " + id);
	if (sti(game.commands.(id).can)) TouchTitle_Command(ui, id);
}

void TouchTitle_Build()
{
	aref ui = GetEventData();
	if (ui.screen != "title") return;
	DeleteAttribute(&TouchTitle, "");
	object game;
	TouchAdpTitle_State(ui, &game);
	aref at;
	if (CheckAttribute(&game, "commands.continue"))
	{
		object go = {id = "continue", label = UiText("menu.continue"), sub = game.last,
		             disabled = !sti(game.commands.("continue").can), reason = "", busy = UiPending("continue")};
		if (sti(game.noRecord)) go.reason = UiText("menu.no_saves");
		makearef(at, TouchTitle.primary);
		CopyAttributes(at, &go);
		UiOn("continue", "");
	}
	TouchTitle_Button(&game, "new", UiText("menu.new_game"));
	TouchTitle_Button(&game, "load", UiText("menu.load"));
	TouchTitle_Button(&game, "options", UiText("menu.settings"));
	TouchTitle_Button(&game, "credits", UiText("menu.credits"));
	if (CheckAttribute(&game, "commands.exit"))
	{
		object leave = {id = "exit", label = UiText("menu.exit"), busy = UiPending("exit")};
		makearef(at, TouchTitle.exit);
		CopyAttributes(at, &leave);
		UiOn("exit", "");
	}
	object about = {id = "about", label = UiText("menu.about")};
	makearef(at, TouchTitle.about);
	CopyAttributes(at, &about);
	UiOn("about", "");
	if (CheckAttribute(&game, "confirm"))
	{
		object confirm = {preset = "danger", title = game.confirm.question, text = "", yes = UiText("menu.exit"),
		                  no = game.confirm.no};
		if (confirm.no == "") confirm.no = UiText("common.cancel");
		makearef(at, TouchTitle.confirm);
		CopyAttributes(at, &confirm);
		UiOn("confirm-confirm", "");
		UiOn("confirm-cancel", "");
		if (sti(game.confirm.canNo)) UiModal("confirm", "confirm-cancel");
		else UiModal("confirm", "");
	}
	TouchTitle_Asked(ui, &game);
	UiShow(&TouchTitle);
}

void TouchTitle_Tap()
{
	aref ui = GetEventData();
	if (ui.screen != "title") return;
	string id = GetEventData(), verb = GetEventData(), value = GetEventData();
	if (id == "about") { TouchLib_Command("host", "action", "app-about"); return; }
	object game;
	TouchAdpTitle_State(ui, &game);
	if (id == "confirm-confirm") { TouchAdpTitle_Answer(ui, true); return; }
	if (id == "confirm-cancel") { if (sti(game.confirm.canNo)) TouchAdpTitle_Answer(ui, false); return; }
	if (!CheckAttribute(&game, "commands." + id)) { UiError("title: no tap of " + id); return; }
	if (sti(game.commands.(id).can)) TouchTitle_Command(ui, id);
}
