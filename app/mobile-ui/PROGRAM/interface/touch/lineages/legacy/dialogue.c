// SPDX-License-Identifier: GPL-3.0-only
// The dialogue of this engine's games: the model the engine makes of the game's dialogue (engine screens/dialogue.cpp:
// SPEAKER, TEXT, the REPLY rows by their link, INPUT the typed reply's field, EDIT_ERROR). Implements
// contracts/dialogue.c, which it does not include: a segment cannot define what it declares extern.

void TouchAdpDialogue_State(aref ui, aref out)
{
	aref model = TouchLegLib_Model(ui);
	out.speaker = "";
	out.text = "";
	if (CheckAttribute(model, "SPEAKER")) out.speaker = model.SPEAKER.label;
	if (CheckAttribute(model, "TEXT")) out.text = model.TEXT.label;
	string refused = "";
	int refusedLink = -1;
	if (CheckAttribute(model, "EDIT_ERROR"))
	{
		aref error = TouchLib_Item(model, "EDIT_ERROR.rows", 0);
		refused = error.label;
		refusedLink = sti(error.index);
	}
	DeleteAttribute(out, "replies");
	aref replies;
	makearef(replies, out.replies);
	for (int k = 0; k < TouchLib_Count(model, "REPLY.rows"); k++)
	{
		aref row = TouchLib_Item(model, "REPLY.rows", k);
		aref at = TouchLib_Add(replies, "r");
		at.link = sti(row.index);
		at.text = row.label;
		at.typed = CheckAttribute(row, "offers.edit");
		at.refused = "";
		if (sti(at.typed) && refusedLink == sti(at.link)) at.refused = refused;
	}
}

void TouchAdpDialogue_Reply(aref ui, int link)
{
	object action = {node = "REPLY", verb = "reply", value = "", row = link};
	UiSendTo(&action);
}

void TouchAdpDialogue_Type(aref ui, string title)
{
	UiEditText("INPUT", title);
}
