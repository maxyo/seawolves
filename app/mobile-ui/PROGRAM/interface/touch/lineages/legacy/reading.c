// SPDX-License-Identifier: GPL-3.0-only
// The reading windows of this lineage: a caption, INFO_TEXT… lines, an info picture, B_OK and B_CANCEL (map,
// DirSailEnc, LeaveBattle, salary), the news notices with their flags (ColonyInfo, NationLegend) and the interlude's
// picture and line (boal_frame). The binding's data names the nodes: title, amount (the line that is the window's
// sum), picture with its ornament (the window's own frame texture, no picture of the moment), flags {n<k>: node},
// relation. Implements contracts/reading.c.

void TouchLegReading_Line(aref out, string text)
{
	if (text == "") return;
	aref line = TouchLib_Add(out, "l");
	line.text = text;
}

// The texts of a node in its order: the rows of a multi-line text, else its label.
void TouchLegReading_Lines(aref node, aref out)
{
	if (!CheckAttribute(node, "rows"))
	{
		TouchLegReading_Line(out, node.label);
		return;
	}
	aref rows;
	makearef(rows, node.rows);
	for (int k = 0; k < GetAttributesNum(rows); k++)
	{
		aref row = GetAttributeN(rows, k);
		TouchLegReading_Line(out, row.label);
	}
}

// A node's first picture, false without one.
bool TouchLegReading_Image(aref model, string node, aref out)
{
	if (node == "") return false;
	if (!CheckAttribute(model, node + ".images.i0")) return false;
	aref image;
	makearef(image, model.(node).images.i0);
	CopyAttributes(out, image);
	return true;
}

void TouchLegReading_Answer(aref model, string node, aref out)
{
	object answer = {label = model.(node).label, can = UiOffers(node, "press"), reason = model.(node).reason};
	CopyAttributes(out, &answer);
}

// The flags' nations, and between two of them the game's relation picture: Enemy is war, Friend an alliance.
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
		if (TouchLegReading_Image(model, GetAttributeValue(GetAttributeN(nodes, k)), &image))
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
	out.relation = "peace";
	if (!TouchLegReading_Image(model, data.relation, &image)) return;
	if (image.image == "Enemy") out.relation = "war";
	if (image.image == "Friend") out.relation = "alliance";
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
		if (TouchLib_StartsWith(name, "INFO_TEXT") && name != data.amount) TouchLegReading_Lines(node, lines);
	}
	object picture;
	if (TouchLegReading_Image(model, data.picture, &picture))
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
	if (CheckAttribute(model, "B_OK"))
	{
		makearef(answer, out.accept);
		TouchLegReading_Answer(model, "B_OK", answer);
	}
	// A cancel without a caption is no answer of the window.
	if (TouchLegLib_Label(model, "B_CANCEL") != "")
	{
		makearef(answer, out.decline);
		TouchLegReading_Answer(model, "B_CANCEL", answer);
	}
}

void TouchAdpReading_Accept(aref ui)
{
	UiSend("B_OK", "press", "");
}

void TouchAdpReading_Decline(aref ui)
{
	UiSend("B_CANCEL", "press", "");
}

void TouchAdpReading_Leave(aref ui)
{
	UiBack("");
}
