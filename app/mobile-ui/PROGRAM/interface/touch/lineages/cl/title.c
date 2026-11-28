// SPDX-License-Identifier: GPL-3.0-only
// The title as a window of the game's own buttons (mainmenu: RESUME_GAME, NEW_GAME, LOAD_GAME, OPTIONS, CREDITS,
// QUIT; the exit question CONFIRM_EXIT_*). A button the game hides is absent, one it greys out cannot be pressed;
// «Продолжить» is the game's, which names no record. Implements contracts/title.c.

void TouchClTitle_Command(aref model, aref out, string id, string node)
{
	if (!CheckAttribute(model, node)) return;
	out.commands.(id).can = UiOffers(node, "press");
	out.commands.(id).node = node;
}

void TouchAdpTitle_State(aref ui, aref out)
{
	aref model = TouchLegLib_Model(ui);
	out.last = "";
	out.noRecord = false;
	TouchClTitle_Command(model, out, "continue", "RESUME_GAME");
	TouchClTitle_Command(model, out, "new", "NEW_GAME");
	TouchClTitle_Command(model, out, "load", "LOAD_GAME");
	TouchClTitle_Command(model, out, "options", "OPTIONS");
	TouchClTitle_Command(model, out, "credits", "CREDITS");
	TouchClTitle_Command(model, out, "exit", "QUIT");
	object set;
	if (!TouchLegLib_Question(model, &set)) return;
	aref confirm;
	makearef(confirm, out.confirm);
	TouchLegLib_Asked(model, confirm);
}

void TouchAdpTitle_Command(aref ui, string id)
{
	object state;
	TouchAdpTitle_State(ui, &state);
	if (!CheckAttribute(&state, "commands." + id)) UiError("title: the game's main menu has no command " + id);
	if (sti(state.commands.(id).can)) UiSend(state.commands.(id).node, "press", "");
}

void TouchAdpTitle_Answer(aref ui, bool yes)
{
	UiSend(TouchLegLib_Reply(TouchLegLib_Model(ui), yes), "press", "");
}
