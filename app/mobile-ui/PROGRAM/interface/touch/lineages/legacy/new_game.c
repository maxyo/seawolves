// SPDX-License-Identifier: GPL-3.0-only
// The new game of this lineage (selectcharacter): COMPLEX_TYPE and SETUP_FRAME_CAPTION between their ‹ › buttons,
// the CHECK_ARCADESAIL pair, the CHECK_* switches, the hero's HERO_NAME, FACEPICT, NATIONS_PICTURE and INFO_TEXT,
// OK_BUTTON (the one start) / CANCEL_BUTTON, and the game's windows CONFIRM_WINDOW_* / NOTE_WINDOW_*. The engine adds
// ANDROID_TUTORIAL_SKIP and ANDROID_HERO_TYPE (the occupation's key). Implements
// contracts/new_game.c.

string TouchLegNewGame_Toggles[5] = {"CHECK_HARDCORE", "CHECK_PISTOL", "CHECK_PARTITION", "CHECK_SEABATTLESAVE",
                                     "CHECK_RAINS"};

bool TouchLegNewGame_Step(aref model, aref rows, string key, string label, string text, string left, string right)
{
	if (!CheckAttribute(model, text)) return false;
	aref at = TouchLib_Add(rows, "r");
	at.kind = "step";
	at.key = key;
	at.label = label;
	at.text = TouchLib_Trim(model.(text).label);
	at.note = "";
	at.hero = false;
	at.left = CheckAttribute(model, left) && UiOffers(left, "press");
	at.right = CheckAttribute(model, right) && UiOffers(right, "press");
	return true;
}

void TouchLegNewGame_Toggle(aref list, string key, string label, aref check)
{
	aref at = TouchLib_Add(list, "t");
	at.key = key;
	at.label = label;
	at.on = check.value == "1";
	at.can = CheckAttribute(check, "offers.toggle");
	at.reason = check.reason;
}

void TouchAdpNewGame_State(aref ui, aref out)
{
	aref model = TouchLegLib_Model(ui);
	aref rows;
	makearef(rows, out.rows);
	TouchLegNewGame_Step(model, rows, "difficulty", UiText("menu.difficulty"), "COMPLEX_TYPE", "LEFTCHANGE_COMPLEX",
	                     "RIGHTCHANGE_COMPLEX");
	aref at;
	if (TouchLegNewGame_Step(model, rows, "setup", "", "SETUP_FRAME_CAPTION", "LEFTCHANGE_SETUP", "RIGHTCHANGE_SETUP"))
	{
		at = TouchLib_Item(out, "rows", TouchLib_Count(out, "rows") - 1);
		at.hero = true;
		at.note = TouchLegLib_Label(model, "SETUP_TEXT");
		at.occupation = TouchLegLib_Label(model, "ANDROID_HERO_TYPE");
	}
	if (CheckAttribute(model, "CHECK_ARCADESAIL.rows"))
	{
		at = TouchLib_Add(rows, "r");
		at.kind = "choice";
		at.key = "battle";
		at.label = TouchLib_Trim(TouchLegLib_Label(model, "CHECK_ARCADESAIL_NAME"));
		at.selected = 0;
		at.can = false;
		aref options;
		aref choices;
		makearef(choices, model.CHECK_ARCADESAIL.rows);
		makearef(options, at.options);
		for (int k = 0; k < GetAttributesNum(choices); k++)
		{
			aref row = GetAttributeN(choices, k);
			aref option = TouchLib_Add(options, "o");
			option.label = row.label;
			if (row.value == "1") at.selected = k;
			if (CheckAttribute(row, "offers.toggle")) at.can = true;
		}
	}
	aref toggles;
	makearef(toggles, out.toggles);
	if (CheckAttribute(model, "ANDROID_TUTORIAL_SKIP.rows.r0"))
	{
		makearef(at, model.ANDROID_TUTORIAL_SKIP.rows.r0);
		TouchLegNewGame_Toggle(toggles, "tutorial", at.label, at);
	}
	for (int n = 0; n < 5; n++)
	{
		string node = TouchLegNewGame_Toggles[n];
		if (!CheckAttribute(model, node + ".rows.r0")) continue;
		makearef(at, model.(node).rows.r0);
		// The game's interface file labels this switch with its untranslated text key in every language.
		string label = at.label;
		if (label == "HardcoreGame") label = UiText("menu.hardcore");
		TouchLegNewGame_Toggle(toggles, node, label, at);
	}
	TouchLib_Copy(out, "hero.portrait", model, "FACEPICT.images.i0");
	object flag;
	out.hero.nation = "";
	if (TouchLegLib_NodeImage(model, "NATIONS_PICTURE", &flag)) out.hero.nation = TouchLegLib_FlagOf(&flag);
	out.hero.name = TouchLib_Trim(TouchLegLib_Label(model, "HERO_NAME"));
	out.hero.bio = TouchLegLib_Label(model, "INFO_TEXT");
	out.hero.bioTitle = TouchLib_Trim(TouchLegLib_Label(model, "INFO_FRAME_CAPTION"));
	object start = {key = "start", label = "", can = UiOffers("OK_BUTTON", "press"), reason = model.OK_BUTTON.reason};
	makearef(at, out.starts.s0);
	CopyAttributes(at, &start);
	out.profile = CheckAttribute(model, "CONFIRM_WINDOW_MB_YES") && UiOffers("CONFIRM_WINDOW_MB_YES", "press");
	if (sti(out.profile)) return;
	object lines;
	aref buttons;
	makearef(buttons, out.window.buttons);
	for (int i = 0; i < GetAttributesNum(model); i++)
	{
		aref widget = GetAttributeN(model, i);
		string name = GetAttributeName(widget);
		if (!TouchLib_StartsWith(name, "CONFIRM_WINDOW_") && !TouchLib_StartsWith(name, "NOTE_WINDOW_")) continue;
		if (widget.type == "text") TouchLegLib_Lines(model, name, &lines, "l");
		if (widget.type == "button" && widget.label != "")
		{
			aref button = TouchLib_Add(buttons, "b");
			button.node = name;
			button.label = widget.label;
			button.can = UiOffers(name, "press");
		}
	}
	string text = "";
	for (int l = 0; l < GetAttributesNum(&lines); l++)
	{
		aref line = GetAttributeN(&lines, l);
		string part = TouchLib_Trim(line.text);
		if (part == "") continue;
		if (text != "") text += TouchLib_Newline();
		text += part;
	}
	out.window.text = text;
	if (text == "" && GetAttributesNum(buttons) == 0) DeleteAttribute(out, "window");
}

void TouchAdpNewGame_Step(aref ui, string key, bool right)
{
	string node = "LEFTCHANGE_";
	if (right) node = "RIGHTCHANGE_";
	if (key == "difficulty") node += "COMPLEX";
	else node += "SETUP";
	if (CheckAttribute(ui, "model." + node) && UiOffers(node, "press")) UiSend(node, "press", "");
}

void TouchAdpNewGame_Choose(aref ui, string key, int k)
{
	if (key != "battle") UiError("new game: the game has no choice " + key);
	if (!CheckAttribute(ui, "model.CHECK_ARCADESAIL.rows.r" + k + ".offers.toggle")) return;
	object action = {node = "CHECK_ARCADESAIL", verb = "toggle", value = "1", row = k};
	UiSendTo(&action);
}

void TouchAdpNewGame_Toggle(aref ui, string key, bool on)
{
	string node = key;
	if (key == "tutorial") node = "ANDROID_TUTORIAL_SKIP";
	if (!CheckAttribute(ui, "model." + node + ".rows.r0.offers.toggle")) return;
	object action = {node = node, verb = "toggle", value = "0", row = 0};
	if (on) action.value = "1";
	UiSendTo(&action);
}

void TouchAdpNewGame_Start(aref ui, string key)
{
	if (UiOffers("OK_BUTTON", "press")) UiSend("OK_BUTTON", "press", "");
}

void TouchAdpNewGame_Cancel(aref ui)
{
	UiBack("CANCEL_BUTTON");
}

void TouchAdpNewGame_Profile(aref ui)
{
	UiSend("CONFIRM_WINDOW_MB_YES", "press", "");
}

void TouchAdpNewGame_Window(aref ui, int k)
{
	object state;
	TouchAdpNewGame_State(ui, &state);
	if (!CheckAttribute(&state, "window.buttons.b" + k)) UiError("new game: the game's window has no button " + k);
	aref button;
	makearef(button, state.window.buttons.("b" + k));
	if (sti(button.can)) UiSend(button.node, "press", "");
}

void TouchAdpNewGame_Name(aref ui, string title)
{
	UiError("new game: the game's window has no name field");
}
