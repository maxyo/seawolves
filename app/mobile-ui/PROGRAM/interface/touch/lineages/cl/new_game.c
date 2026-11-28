// SPDX-License-Identifier: GPL-3.0-only
// The new game of Caribbean Legend's set (selectcharacter): the hero between LEFTCHANGE_/RIGHTCHANGE_CHARACTER, its
// choices as cards VALUE_<key> captioned HEADER_<key> with their text TEXT_<key> between BUTTON_L_/BUTTON_R_<key> (the
// hero's cards are named by the binding's data.hero, the others are the world's and the game's settings), the
// window's pages (TABBTN_*), its CHECK_* switches, the starts BTN_START*, BTN_EXIT, and the profile window
// CONFIRM_WINDOW_* with TEXT_CONFIRM_WINDOW and the field PROFILE_NAME. Implements contracts/new_game.c.

void TouchClNewGame_Step(aref model, aref rows, string key, string label, string text, string left, string right)
{
	aref at = TouchLib_Add(rows, "r");
	at.kind = "step";
	at.key = key;
	at.label = TouchLib_Trim(label);
	at.text = TouchLib_Trim(TouchLegLib_Label(model, text));
	at.note = "";
	at.hero = false;
	at.left = CheckAttribute(model, left) && UiOffers(left, "press");
	at.right = CheckAttribute(model, right) && UiOffers(right, "press");
}

void TouchClNewGame_Answer(aref model, aref buttons, string node)
{
	if (!CheckAttribute(model, node)) return;
	aref at = TouchLib_Add(buttons, "b");
	at.node = node;
	at.label = TouchLib_Trim(model.(node).label);
	at.can = UiOffers(node, "press");
}

void TouchAdpNewGame_State(aref ui, aref out)
{
	aref model = TouchLegLib_Model(ui);
	aref rows;
	makearef(rows, out.rows);
	aref at;
	if (CheckAttribute(model, "LEFTCHANGE_CHARACTER") || CheckAttribute(model, "RIGHTCHANGE_CHARACTER"))
	{
		TouchClNewGame_Step(model, rows, "hero", "", "HERO_NAME", "LEFTCHANGE_CHARACTER", "RIGHTCHANGE_CHARACTER");
		at = TouchLib_Item(out, "rows", TouchLib_Count(out, "rows") - 1);
		at.hero = true;
	}
	object pages;
	if (TouchClLib_Pages(model, &pages) > 1)
	{
		at = TouchLib_Add(rows, "r");
		CopyAttributes(at, &pages);
		at.kind = "choice";
		at.key = "page";
		at.label = "";
		if (sti(at.selected) < 0) UiError("new game: no page of the window is shown as selected");
	}
	object heroCards;
	aref cards;
	makearef(cards, heroCards.list);
	TouchLib_Words(ui.data.hero, cards);
	aref toggles;
	makearef(toggles, out.toggles);
	for (int i = 0; i < GetAttributesNum(model); i++)
	{
		aref widget = GetAttributeN(model, i);
		string name = GetAttributeName(widget);
		if (TouchLib_StartsWith(name, "CHECK_") && CheckAttribute(widget, "rows.r0"))
		{
			aref check;
			makearef(check, widget.rows.r0);
			at = TouchLib_Add(toggles, "t");
			at.key = name;
			at.label = TouchLib_Trim(check.label);
			at.on = check.value == "1";
			at.can = CheckAttribute(check, "offers.toggle");
			at.reason = check.reason;
			continue;
		}
		if (!TouchLib_StartsWith(name, "VALUE_")) continue;
		string key = TouchLib_Cut(name, 6, strlen(name));
		TouchClNewGame_Step(model, rows, key, TouchLegLib_Label(model, "HEADER_" + key), name, "BUTTON_L_" + key,
		                    "BUTTON_R_" + key);
		at = TouchLib_Item(out, "rows", TouchLib_Count(out, "rows") - 1);
		at.note = TouchLegLib_Label(model, "TEXT_" + key);
		at.hero = TouchLib_Find(&heroCards, "list", "text", key) >= 0;
	}
	TouchLib_Copy(out, "hero.portrait", model, "FACEPICT.images.i0");
	object flag;
	out.hero.nation = "";
	if (TouchLegLib_NodeImage(model, "NATIONS_PICTURE", &flag)) out.hero.nation = TouchLegLib_FlagOf(&flag);
	out.hero.name = TouchLib_Trim(TouchLegLib_Label(model, "HERO_NAME"));
	out.hero.bio = TouchLegLib_Label(model, "INFO_TEXT");
	out.hero.bioTitle = TouchLib_Trim(TouchLegLib_Label(model, "INFO_FRAME_CAPTION"));
	aref starts;
	makearef(starts, out.starts);
	for (int n = 1; n <= 4; n++)
	{
		string node = "BTN_START" + n;
		if (!CheckAttribute(model, node)) continue;
		object start = {key = node, label = TouchLib_Trim(model.(node).label), can = UiOffers(node, "press"),
		                reason = model.(node).reason};
		at = TouchLib_Add(starts, "s");
		CopyAttributes(at, &start);
	}
	if (GetAttributesNum(starts) == 0) UiError("new game: the game offers no start");
	out.profile = false;
	if (!CheckAttribute(model, "CONFIRM_WINDOW_MB_YES")) return;
	out.window.text = TouchLegLib_Label(model, "TEXT_CONFIRM_WINDOW");
	aref buttons;
	makearef(buttons, out.window.buttons);
	TouchClNewGame_Answer(model, buttons, "CONFIRM_WINDOW_MB_YES");
	TouchClNewGame_Answer(model, buttons, "CONFIRM_WINDOW_MB_NO");
	if (!CheckAttribute(model, "PROFILE_NAME")) return;
	out.window.name.label = TouchLib_Trim(TouchLegLib_Label(model, "PROFILE_STR"));
	out.window.name.value = model.PROFILE_NAME.value;
}

void TouchAdpNewGame_Step(aref ui, string key, bool right)
{
	string node = "BUTTON_L_" + key;
	if (right) node = "BUTTON_R_" + key;
	if (key == "hero")
	{
		node = "LEFTCHANGE_CHARACTER";
		if (right) node = "RIGHTCHANGE_CHARACTER";
	}
	if (CheckAttribute(ui, "model." + node) && UiOffers(node, "press")) UiSend(node, "press", "");
}

void TouchAdpNewGame_Choose(aref ui, string key, int k)
{
	if (key != "page") UiError("new game: the game has no choice " + key);
	object pages;
	if (k < 0 || k >= TouchClLib_Pages(TouchLegLib_Model(ui), &pages)) UiError("new game: the window has no page " + k);
	string node = pages.options.("o" + k).node;
	if (UiOffers(node, "press")) UiSend(node, "press", "");
}

void TouchAdpNewGame_Toggle(aref ui, string key, bool on)
{
	if (!CheckAttribute(ui, "model." + key + ".rows.r0.offers.toggle")) return;
	object action = {node = key, verb = "toggle", value = "0", row = 0};
	if (on) action.value = "1";
	UiSendTo(&action);
}

void TouchAdpNewGame_Start(aref ui, string key)
{
	if (UiOffers(key, "press")) UiSend(key, "press", "");
}

void TouchAdpNewGame_Cancel(aref ui)
{
	UiBack("BTN_EXIT");
}

void TouchAdpNewGame_Profile(aref ui)
{
	UiError("new game: the game asks no profile question of its own");
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
	UiEditText("PROFILE_NAME", title);
}
