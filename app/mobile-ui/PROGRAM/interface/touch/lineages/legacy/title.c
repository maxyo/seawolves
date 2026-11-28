// SPDX-License-Identifier: GPL-3.0-only
// The title over the menu scene of the application's own mainmenu.c: the window has no widgets of the game, its
// commands are the engine's ("title": the scene's command events), «Продолжить» the record the engine names
// (ANDROID_CONTINUE), loaded by that mainmenu.c's AndroidContinue. Implements contracts/title.c.

void TouchAdpTitle_State(aref ui, aref out)
{
	out.last = TouchLegLib_Label(TouchLegLib_Model(ui), "ANDROID_CONTINUE");
	out.noRecord = out.last == "";
	object ids;
	TouchLib_Words("continue new load options credits exit", &ids);
	for (int k = 0; k < GetAttributesNum(&ids); k++)
	{
		aref id = GetAttributeN(&ids, k);
		out.commands.(id.text).can = true;
	}
	out.commands.("continue").can = !sti(out.noRecord);
}

void TouchAdpTitle_Command(aref ui, string id)
{
	string name = "";
	if (id == "continue") name = "Continue";
	if (id == "new") name = "New";
	if (id == "load") name = "Load";
	if (id == "options") name = "Options";
	if (id == "credits") name = "Credits";
	if (id == "exit") name = "Exit";
	if (name == "") UiError("title: no command " + id);
	TouchLib_Command("title", "command", name);
}

void TouchAdpTitle_Answer(aref ui, bool yes)
{
	UiError("title: this title asks no question");
}
