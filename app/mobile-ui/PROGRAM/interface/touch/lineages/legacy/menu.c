// SPDX-License-Identifier: GPL-3.0-only
// The menus of this lineage: the pause menu (game_menu: MB_* buttons, its exit question: TouchLegLib_Question), the
// quick save's plate (quick_save) and the death screen the engine makes of the game's death picture (game_over: KIND,
// END). The engine adds ANDROID_GAME (context gameDate YYYY-MM-DD), ANDROID_QUICK_LOAD and MB_RESUME's context
// resumeOnCancel. Implements contracts/menu.c.

void TouchLegMenu_Button(aref model, string node, aref buttons, string id)
{
	if (!CheckAttribute(model, node)) return;
	object button = {node = node, label = model.(node).label, can = UiOffers(node, "press"),
	                 reason = model.(node).reason};
	aref at;
	makearef(at, buttons.(id));
	CopyAttributes(at, &button);
}

// The game's exit: to its own main menu where it has one, else out of the program.
string TouchLegMenu_Exit(aref model)
{
	if (CheckAttribute(model, "MB_EXITMENU")) return "MB_EXITMENU";
	if (CheckAttribute(model, "MB_EXITGAME")) return "MB_EXITGAME";
	if (CheckAttribute(model, "MB_QUIT")) return "MB_QUIT";
	return "";
}

void TouchAdpMenu_Pause(aref ui, aref out)
{
	aref model = TouchLegLib_Model(ui);
	out.game = TouchLegLib_Label(model, "ANDROID_GAME");
	out.year = 0;
	out.month = 0;
	out.day = 0;
	if (CheckAttribute(model, "ANDROID_GAME.context.gameDate"))
	{
		string date = model.ANDROID_GAME.context.gameDate;
		if (strlen(date) != 10) UiError("menu: the game's date " + date + " is not YYYY-MM-DD");
		out.year = UiAmount(TouchLib_Cut(date, 0, 4), "the game's year");
		out.month = UiAmount(TouchLib_Cut(date, 5, 7), "the game's month");
		out.day = UiAmount(TouchLib_Cut(date, 8, 10), "the game's day");
	}
	// «Продолжить» is the menu's cancel (the route of MB_RESUME's own command), honoured while the engine's
	// resumeOnCancel is 1, also where the game greys the button out itself (TEHO's boarding).
	if (!CheckAttribute(model, "MB_RESUME.context.resumeOnCancel"))
		UiError("menu: MB_RESUME has no resumeOnCancel");
	object resume = {label = model.MB_RESUME.label, can = model.MB_RESUME.context.resumeOnCancel == "1",
	                 reason = model.MB_RESUME.reason};
	aref at;
	makearef(at, out.resume);
	CopyAttributes(at, &resume);
	aref buttons;
	makearef(buttons, out.buttons);
	TouchLegMenu_Button(model, "MB_SAVE", buttons, "save");
	TouchLegMenu_Button(model, "MB_LOAD", buttons, "load");
	TouchLegMenu_Button(model, "MB_OPTIONS", buttons, "options");
	string way = TouchLegMenu_Exit(model);
	if (way != "") TouchLegMenu_Button(model, way, buttons, "exit");
	TouchLegMenu_Button(model, "MB_UPDATES", buttons, "updates");
	out.home = way == "MB_EXITMENU";
	out.quickLoad = CheckAttribute(model, "ANDROID_QUICK_LOAD");
	object set;
	if (!TouchLegLib_Question(model, &set)) return;
	makearef(at, out.confirm);
	TouchLegLib_Asked(model, at);
}

void TouchAdpMenu_Press(aref ui, string id)
{
	object state;
	TouchAdpMenu_Pause(ui, &state);
	if (!CheckAttribute(&state, "buttons." + id)) UiError("menu: the game's pause menu has no button " + id);
	string node = state.buttons.(id).node;
	if (sti(state.buttons.(id).can)) UiSend(node, "press", "");
}

void TouchAdpMenu_Resume(aref ui)
{
	UiBack("");
}

void TouchAdpMenu_Answer(aref ui, bool yes)
{
	UiSend(TouchLegLib_Reply(TouchLegLib_Model(ui), yes), "press", "");
}

void TouchAdpMenu_Saving(aref ui, aref out)
{
	aref model = TouchLegLib_Model(ui);
	out.title = UiOneLine(TouchLegLib_Label(model, "INFO_WINDOW_CAPTION"));
	out.text = UiOneLine(TouchLegLib_Label(model, "INFO_TEXT"));
}

void TouchAdpMenu_Over(aref ui, aref out)
{
	out.kind = TouchLegLib_Label(TouchLegLib_Model(ui), "KIND");
}

void TouchAdpMenu_End(aref ui)
{
	UiSend("END", "press", "");
}
