// SPDX-License-Identifier: GPL-3.0-only
// The new game of this lineage (selectcharacter): COMPLEX_TYPE and SETUP_FRAME_CAPTION between their ‹ › buttons,
// the CHECK_ARCADESAIL pair, the CHECK_* switches, the hero's HERO_NAME, FACEPICT, NATIONS_PICTURE and INFO_TEXT,
// OK_BUTTON / CANCEL_BUTTON, and the game's windows CONFIRM_WINDOW_* / NOTE_WINDOW_*. The engine adds
// ANDROID_TUTORIAL_SKIP (its toggle carries its row id) and ANDROID_HERO_TYPE (the occupation's key). Implements
// contracts/new_game.c.

string TouchLegNewGame_Toggles[5] = {"CHECK_HARDCORE", "CHECK_PISTOL", "CHECK_PARTITION", "CHECK_SEABATTLESAVE",
                                     "CHECK_RAINS"};

void TouchLegNewGame_Step(aref model, aref out, string id, string text, string left, string right)
{
	if (!CheckAttribute(model, text)) return;
	aref at;
	makearef(at, out.(id));
	at.text = TouchLib_Trim(model.(text).label);
	at.left = CheckAttribute(model, left) && UiOffers(left, "press");
	at.right = CheckAttribute(model, right) && UiOffers(right, "press");
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
	TouchLegNewGame_Step(model, out, "difficulty", "COMPLEX_TYPE", "LEFTCHANGE_COMPLEX", "RIGHTCHANGE_COMPLEX");
	TouchLegNewGame_Step(model, out, "setup", "SETUP_FRAME_CAPTION", "LEFTCHANGE_SETUP", "RIGHTCHANGE_SETUP");
	if (CheckAttribute(out, "setup"))
	{
		out.setup.note = TouchLegLib_Label(model, "SETUP_TEXT");
		out.setup.occupation = TouchLegLib_Label(model, "ANDROID_HERO_TYPE");
	}
	aref at;
	if (CheckAttribute(model, "CHECK_ARCADESAIL.rows"))
	{
		makearef(at, out.battle);
		at.label = TouchLib_Trim(TouchLegLib_Label(model, "CHECK_ARCADESAIL_NAME"));
		at.selected = 0;
		at.can = false;
		aref rows;
		aref options;
		makearef(rows, model.CHECK_ARCADESAIL.rows);
		makearef(options, at.options);
		for (int k = 0; k < GetAttributesNum(rows); k++)
		{
			aref row = GetAttributeN(rows, k);
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
	object face;
	if (TouchLegLib_NodeImage(model, "FACEPICT", &face))
	{
		makearef(at, out.hero.portrait);
		CopyAttributes(at, &face);
	}
	object flag;
	out.hero.nation = "";
	if (TouchLegLib_NodeImage(model, "NATIONS_PICTURE", &flag)) out.hero.nation = TouchLegLib_FlagOf(&flag);
	out.hero.name = TouchLib_Trim(TouchLegLib_Label(model, "HERO_NAME"));
	out.hero.bio = TouchLegLib_Label(model, "INFO_TEXT");
	out.hero.bioTitle = TouchLib_Trim(TouchLegLib_Label(model, "INFO_FRAME_CAPTION"));
	out.start.can = UiOffers("OK_BUTTON", "press");
	out.start.reason = model.OK_BUTTON.reason;
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

void TouchAdpNewGame_Step(aref ui, string id, bool right)
{
	string node = "LEFTCHANGE_";
	if (right) node = "RIGHTCHANGE_";
	if (id == "difficulty") node += "COMPLEX";
	else node += "SETUP";
	if (CheckAttribute(ui, "model." + node) && UiOffers(node, "press")) UiSend(node, "press", "");
}

void TouchAdpNewGame_Battle(aref ui, int k)
{
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
	// The engine binds the port's own switch to the row identity it published.
	if (key == "tutorial") action.value = action.value + "@" + ui.model.ANDROID_TUTORIAL_SKIP.rows.r0.id;
	UiSendTo(&action);
}

void TouchAdpNewGame_Start(aref ui)
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
