// SPDX-License-Identifier: GPL-3.0-only
// The reading windows of this lineage: a caption, INFO_TEXT… lines, an info picture, B_OK (or B_OK2) and B_CANCEL
// (map, DirSailEnc, LeaveBattle, salary; a lesson, a message), the news notices with their flags (ColonyInfo,
// NationLegend) and the interlude's picture and line (boal_frame). The binding's data names the nodes: title, amount
// (the line that is the window's sum), picture with its ornament (the window's own frame texture, no picture of the
// moment), flags {n<k>: node}, relation, check (a switch beside the answers). Implements contracts/reading.c.

// The flags' nations, and between two of them the game's relation picture (NationLegend.c GetRelationName): Enemy is
// war, Friend an alliance, Neutral peace.
void TouchLegReading_Flags(aref model, aref data, aref out)
{
	out.relation = "";
	aref flags;
	makearef(flags, out.flags);
	if (!CheckAttribute(data, "flags")) return;
	aref nodes;
	makearef(nodes, data.flags);
	object image;
	for (int k = 0; k < GetAttributesNum(nodes); k++)
	{
		DeleteAttribute(&image, "");
		if (TouchLegLib_NodeImage(model, GetAttributeValue(GetAttributeN(nodes, k)), &image))
		{
			string icon = TouchLegLib_FlagOf(&image);
			if (icon != "")
			{
				aref flag = TouchLib_Add(flags, "f");
				flag.icon = icon;
			}
		}
	}
	if (GetAttributesNum(flags) != 2) return;
	DeleteAttribute(&image, "");
	if (!TouchLegLib_NodeImage(model, data.relation, &image))
		UiError("reading: two nations and no relation picture " + data.relation);
	object relations = {Enemy = "war", Friend = "alliance", Neutral = "peace"};
	string named = image.image;
	if (named == "" || !CheckAttribute(&relations, named))
		UiError("reading: the relation picture " + data.relation + " shows '" + named + "'");
	out.relation = relations.(named);
}

// The window's answer that accepts (a window shows one of its two OK buttons at a time).
string TouchLegReading_Accept(aref model)
{
	return TouchLegLib_Exit(model, "B_OK B_OK2");
}

void TouchAdpReading_State(aref ui, aref out)
{
	aref model = TouchLegLib_Model(ui);
	aref data;
	makearef(data, ui.data);
	out.title = TouchLegLib_Label(model, data.title);
	out.amount = TouchLegLib_Label(model, data.amount);
	aref lines;
	makearef(lines, out.lines);
	for (int k = 0; k < GetAttributesNum(model); k++)
	{
		aref node = GetAttributeN(model, k);
		string name = GetAttributeName(node);
		if (TouchLib_StartsWith(name, "INFO_TEXT") && name != data.amount) TouchLegLib_Lines(model, name, lines, "l");
	}
	object picture;
	if (TouchLegLib_NodeImage(model, data.picture, &picture))
	{
		if (picture.image != data.ornament)
		{
			aref shown;
			makearef(shown, out.picture);
			CopyAttributes(shown, &picture);
		}
	}
	TouchLegReading_Flags(model, data, out);
	aref answer;
	string accept = TouchLegReading_Accept(model);
	if (accept != "")
	{
		makearef(answer, out.accept);
		TouchLegLib_Button(model, accept, answer);
	}
	if (CheckAttribute(data, "check") && CheckAttribute(model, data.check))
	{
		makearef(answer, out.check);
		answer.label = "";
		TouchLegLib_Option(model, data.check, answer);
	}
	// A cancel without a caption is no answer of the window.
	if (TouchLegLib_Label(model, "B_CANCEL") != "")
	{
		makearef(answer, out.decline);
		TouchLegLib_Button(model, "B_CANCEL", answer);
	}
}

void TouchAdpReading_Accept(aref ui)
{
	UiSend(TouchLegReading_Accept(TouchLegLib_Model(ui)), "press", "");
}

void TouchAdpReading_Check(aref ui, bool on)
{
	TouchLegLib_OptionToggle(ui, ui.data.check, 0, on);
}

void TouchAdpReading_Decline(aref ui)
{
	UiSend("B_CANCEL", "press", "");
}

void TouchAdpReading_Leave(aref ui)
{
	UiBack("");
}
