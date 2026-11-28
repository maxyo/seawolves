// SPDX-License-Identifier: GPL-3.0-only
// The save list of this lineage (save_load): the engine lists every record of every profile as ANDROID_SAVE_LIST rows
// under ANDROID_SAVES (label: the file name; the new record's reason "new"; the row's context: its profile and what the
// record says about itself, mobile_save_list.hpp SaveFacts) with the verbs activate and delete, each sent with the row's identity. The
// game's questions are CONFIRM_TEXT / CONFIRM_YES / CONFIRM_NO, its name editor CUSTOM_SAVE_*. Implements
// contracts/saves.c.

string TouchLegSaves_Fact(aref row, string key)
{
	if (!CheckAttribute(row, "context." + key)) return "";
	return row.context.(key);
}

// A record's identity: its profile and file (two profiles can name a record alike); "" for the new record.
string TouchLegSaves_Key(aref row)
{
	if (row.reason == "new") return "";
	return TouchLegSaves_Fact(row, "profile") + "/" + row.label;
}

void TouchAdpSaves_State(aref ui, aref out)
{
	aref model = TouchLegLib_Model(ui);
	// The engine's list of every record stands for the game's pages of slots.
	if (!CheckAttribute(model, "ANDROID_SAVES")) UiError("saves: the engine lists no records (ANDROID_SAVES)");
	out.saving = false;
	aref records;
	makearef(records, out.records);
	int count = TouchLib_Count(model, "ANDROID_SAVE_LIST.rows");
	for (int k = 0; k < count; k++)
	{
		aref row = TouchLib_Item(model, "ANDROID_SAVE_LIST.rows", k);
		aref at = TouchLib_Add(records, "r");
		at.fresh = row.reason == "new";
		if (sti(at.fresh)) out.saving = true;
		at.key = TouchLegSaves_Key(row);
		at.file = row.label;
		at.autosave = TouchLegSaves_Fact(row, "autosave") == "1";
		at.unreadable = TouchLegSaves_Fact(row, "unreadable") == "1";
		at.hero = TouchLegSaves_Fact(row, "hero");
		at.location = TouchLegSaves_Fact(row, "location");
		at.gameDate = TouchLegSaves_Fact(row, "gameDate");
		at.gameTime = TouchLegSaves_Fact(row, "gameTime");
		at.created = TouchLegSaves_Fact(row, "created");
		at.createdWhen = TouchLegSaves_Fact(row, "createdWhen");
		at.minutes = -1;
		if (TouchLegSaves_Fact(row, "playMinutes") != "")
			at.minutes = UiAmount(row.context.playMinutes, "the record's play minutes");
		at.preview = TouchLegSaves_Fact(row, "preview");
		at.selected = row.value == "1";
		at.can = CheckAttribute(row, "offers.activate");
		at.canDelete = CheckAttribute(row, "offers.delete");
		at.reason = row.reason;
		if (sti(at.fresh)) at.reason = "";
		at.enabled = row.enabled;
	}
	out.naming = CheckAttribute(model, "CUSTOM_SAVE_BTN_OK") && UiOffers("CUSTOM_SAVE_BTN_OK", "press");
	if (!CheckAttribute(model, "CONFIRM_YES")) return;
	object lines;
	TouchLegLib_Lines(model, "CONFIRM_TEXT", &lines, "l");
	string text = "";
	for (int l = 0; l < GetAttributesNum(&lines); l++)
	{
		aref line = GetAttributeN(&lines, l);
		if (text != "") text += TouchLib_Newline();
		text += line.text;
	}
	out.question.text = text;
	out.question.yes = model.CONFIRM_YES.label;
	out.question.canYes = UiOffers("CONFIRM_YES", "press");
	out.question.no = TouchLegLib_Label(model, "CONFIRM_NO");
	out.question.canNo = CheckAttribute(model, "CONFIRM_NO") && UiOffers("CONFIRM_NO", "press");
}

// The engine accepts a row's verb only with the identity it gave that row.
void TouchLegSaves_Send(aref ui, string key, string verb)
{
	aref model = TouchLegLib_Model(ui);
	for (int k = 0; k < TouchLib_Count(model, "ANDROID_SAVE_LIST.rows"); k++)
	{
		aref row = TouchLib_Item(model, "ANDROID_SAVE_LIST.rows", k);
		if (TouchLegSaves_Key(row) != key) continue;
		object action = {node = "ANDROID_SAVE_LIST", verb = verb, value = row.id, row = row.index};
		UiSendTo(&action);
		return;
	}
	UiError("saves: no record '" + key + "' to " + verb);
}

void TouchAdpSaves_Activate(aref ui, string key)
{
	TouchLegSaves_Send(ui, key, "activate");
}

void TouchAdpSaves_Delete(aref ui, string key)
{
	TouchLegSaves_Send(ui, key, "delete");
}

void TouchAdpSaves_Answer(aref ui, bool yes)
{
	string node = "CONFIRM_NO";
	if (yes) node = "CONFIRM_YES";
	UiSend(node, "press", "");
}

void TouchAdpSaves_Name(aref ui)
{
	UiSend("CUSTOM_SAVE_BTN_OK", "press", "");
}

void TouchAdpSaves_Leave(aref ui)
{
	UiBack(TouchLegLib_Exit(TouchLegLib_Model(ui), "BTN_EXIT"));
}
