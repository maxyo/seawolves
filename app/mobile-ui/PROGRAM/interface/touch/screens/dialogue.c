// SPDX-License-Identifier: GPL-3.0-only
// The dialogue over the game: who speaks, the speech a page at a time and, with its last page, the replies the game
// offers. A reply the player types (a name, a bet) opens the phone's text editor over the game's proposal; the game
// takes or refuses the answer, and the engine says why it refused one.

#event_handler("UiBuild", "TouchDialogue_Build");
#event_handler("UiTap", "TouchDialogue_Tap");

object TouchDialogue;
object TouchDialogue_Game; // the adapter's state of the last build
int TouchDialogue_Page;    // the «next» presses on this node

// The string key of the engine's code of a refused answer; the engine reports a code it has no word for.
string TouchDialogue_Refusal(string code)
{
	if (code == "too_long") return "dialogue.too_long";
	if (code == "invalid_text") return "dialogue.invalid";
	return "dialogue.unavailable";
}

void TouchDialogue_Build()
{
	aref ui = GetEventData();
	if (ui.screen != "dialogue") return;
	if (UiFirst()) TouchDialogue_Page = 0;
	aref game;
	makearef(game, TouchDialogue_Game);
	TouchAdpDialogue_State(ui, game);
	DeleteAttribute(&TouchDialogue, "");
	TouchDialogue.speaker = game.speaker;
	TouchDialogue.text = game.text;
	TouchDialogue.page = TouchDialogue_Page;
	aref replies;
	makearef(replies, TouchDialogue.replies);
	string id;
	for (int k = 0; k < TouchLib_Count(game, "replies"); k++)
	{
		aref reply = TouchLib_Item(game, "replies", k);
		aref at = TouchLib_Add(replies, "r");
		id = "reply-" + reply.link;
		at.id = id;
		at.title = reply.text;
		if (TouchLib_Trim(reply.text) == "")
		{
			if (sti(reply.typed)) at.title = UiText("dialogue.input");
			else at.title = UiText("dialogue.continue");
		}
		at.pressed = UiPending(id);
		if (reply.refused != "")
		{
			at.meta = UiText(TouchDialogue_Refusal(reply.refused));
			at.metaTone = "danger";
		}
		UiOn(id, "click");
	}
	UiOn("dialogue-next", "click");
	UiShow(&TouchDialogue);
}

void TouchDialogue_Tap()
{
	aref ui = GetEventData();
	if (ui.screen != "dialogue") return;
	string id = GetEventData(), verb = GetEventData(), value = GetEventData();
	if (id == "dialogue-next")
	{
		TouchDialogue_Page++; // the pager stops at the speech's last page
		UiRebuild();
		return;
	}
	int link = UiAmount(TouchLib_Cut(id, 6, strlen(id)), "the reply " + id);
	for (int k = 0; k < TouchLib_Count(&TouchDialogue_Game, "replies"); k++)
	{
		aref reply = TouchLib_Item(&TouchDialogue_Game, "replies", k);
		if (sti(reply.link) == link && sti(reply.typed))
		{
			TouchAdpDialogue_Type(ui, UiText("dialogue.input"));
			return;
		}
	}
	TouchAdpDialogue_Reply(ui, link);
	UiHold();
}
