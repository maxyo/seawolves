// SPDX-License-Identifier: GPL-3.0-only
// The reading windows over the running game: a card with the game's answers, a notice of news under its flags, the
// interlude's line over its picture, or nothing for a window the game closes at once. The look of each capability:
// mode ("card", "notice", "frame", "none"), icon, decline (the variant of the second answer), the string keys
// acceptLabel and declineLabel (the game's captions where ""), acceptRefused and declineRefused (said of an answer
// the game does not offer without saying why, "" nothing), aside (a tap beside the card declines) and title (a
// notice's when the game gives none).

#event_handler("UiBuild", "TouchReading_Build");
#event_handler("UiTap", "TouchReading_Tap");

object TouchReading;

string TouchReading_Joined(aref game, string separator)
{
	string text = "";
	aref lines;
	makearef(lines, game.lines);
	for (int k = 0; k < GetAttributesNum(lines); k++)
	{
		aref line = GetAttributeN(lines, k);
		if (k > 0) text += separator;
		text += line.text;
	}
	return text;
}

string TouchReading_Key(aref look, string field)
{
	if (look.(field) == "") return "";
	return UiText(look.(field));
}

// An answer of the game as a button; `label` and `refused` are the look's keys of its caption and of why it is off.
void TouchReading_Button(aref actions, aref answer, string id, string variant, string label, string refused)
{
	aref at = TouchLib_Button(actions, id, answer.label, variant, answer);
	if (label != "") at.label = label;
	if (!sti(answer.can) && answer.reason == "") at.reason = refused;
}

void TouchReading_Card(aref ui, aref game)
{
	aref look;
	makearef(look, ui.look);
	TouchReading.icon = look.icon;
	TouchReading.title = game.title;
	TouchReading.text = TouchReading_Joined(game, " ");
	aref actions;
	makearef(actions, TouchReading.actions);
	string aside = "";
	if (CheckAttribute(game, "decline"))
	{
		aref decline;
		makearef(decline, game.decline);
		TouchReading_Button(actions, decline, "cancel", look.decline, TouchReading_Key(look, "declineLabel"),
		                    TouchReading_Key(look, "declineRefused"));
		if (sti(look.aside) && sti(decline.can)) aside = "cancel";
	}
	aref accept;
	makearef(accept, game.accept);
	TouchReading_Button(actions, accept, "ok", "primary", TouchReading_Key(look, "acceptLabel"),
	                    TouchReading_Key(look, "acceptRefused"));
	// The window's sum is said on the answer that pays it, not in the text.
	if (game.amount != "")
	{
		aref pay = GetAttributeN(actions, GetAttributesNum(actions) - 1);
		pay.sub = "−" + game.amount;
	}
	UiModal("card", aside);
}

void TouchReading_Notice(aref ui, aref game)
{
	aref look;
	makearef(look, ui.look);
	TouchReading.icon = "";
	TouchReading.title = game.title;
	if (game.title == "") TouchReading.title = UiText(look.title);
	TouchReading.text = TouchReading_Joined(game, " ");
	aref flags, at;
	makearef(flags, TouchReading.flags);
	for (int k = 0; k < TouchLib_Count(game, "flags"); k++)
	{
		if (k == 1 && game.relation != "")
		{
			// Between two flags the relation of their nations, in a word.
			at = TouchLib_Add(flags, "f");
			at.icon = "rel-neutral";
			if (game.relation == "war") at.icon = "rel-war";
			if (game.relation == "alliance") at.icon = "rel-ally";
			at.word = UiText("reading." + game.relation);
			at.tone = "";
			if (game.relation == "war") at.tone = "war";
		}
		aref flag = TouchLib_Item(game, "flags", k);
		at = TouchLib_Add(flags, "f");
		at.icon = flag.icon;
	}
	aref actions;
	makearef(actions, TouchReading.actions);
	// A notice under two flags is a short news line: its cross is enough.
	if (GetAttributesNum(flags) < 2)
	{
		aref accept;
		makearef(accept, game.accept);
		TouchReading_Button(actions, accept, "ok", "secondary", "", "");
	}
	UiOn("ok", "");
	UiOn("card-close", "");
	UiModal("card", "ok");
}

void TouchReading_Build()
{
	aref ui = GetEventData();
	if (ui.screen != "reading") return;
	object game;
	TouchAdpReading_State(ui, &game);
	DeleteAttribute(&TouchReading, "");
	TouchReading.mode = ui.look.mode;
	TouchReading.title = "";
	TouchReading.text = "";
	TouchReading.icon = "";
	TouchReading.actions = "";
	if (ui.look.mode == "card") TouchReading_Card(ui, &game);
	if (ui.look.mode == "notice") TouchReading_Notice(ui, &game);
	if (ui.look.mode == "frame")
	{
		TouchReading.text = TouchReading_Joined(&game, TouchLib_Newline());
		if (CheckAttribute(&game, "picture"))
		{
			aref picture, shown;
			makearef(picture, game.picture);
			makearef(shown, TouchReading.picture);
			CopyAttributes(shown, picture);
		}
		// The interlude ends by itself; a tap ends it early the game's way.
		UiOn("frame", "");
	}
	UiShow(&TouchReading);
}

void TouchReading_Tap()
{
	aref ui = GetEventData();
	if (ui.screen != "reading") return;
	string id = GetEventData(), verb = GetEventData(), value = GetEventData();
	if (id == "frame") { TouchAdpReading_Leave(ui); return; }
	object game;
	TouchAdpReading_State(ui, &game);
	if (id == "cancel")
	{
		if (sti(game.decline.can)) TouchAdpReading_Decline(ui);
		return;
	}
	if (sti(game.accept.can)) TouchAdpReading_Accept(ui);
}
