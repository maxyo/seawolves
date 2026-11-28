// SPDX-License-Identifier: GPL-3.0-only
// The nations from the interface nationrelation. Implements contracts/nations.c. The relations, the hero's standing
// and its word are the game's functions' (as its window reads them); the flags the game lets the ship fly are its
// window's: SetNewNation moves the candidate FlagPic over them one press of › at a time.
// data: help.<part>.{title, text} (the game's string and text keys of the window's help).

// The pick under way: asked (the nation's picture asked for), start (the candidate before), presses; refused.<picture>:
// a flag the game's cycle did not reach in this opening.
object TouchLegNations;

string TouchLegNations_Candidate(aref model)
{
	if (!CheckAttribute(model, "FlagPic.images.i0")) return "";
	return model.FlagPic.images.i0.image;
}

// The game's answer to the pick's last press: the asked flag reached, the cycle back where it started (the game
// does not offer it) or another press. SetNewNation skips the flags it does not allow, so a full cycle is at most
// one press a nation.
void TouchLegNations_Step(aref ui, string current)
{
	if (UiFirst()) DeleteAttribute(&TouchLegNations, "");
	if (!CheckAttribute(&TouchLegNations, "asked")) return;
	string answer = UiAnswer();
	if (answer == "waiting") return;
	string asked = TouchLegNations.asked;
	if (current == asked) { DeleteAttribute(&TouchLegNations, "asked"); return; }
	if (answer == "refused" || current == TouchLegNations.start || sti(TouchLegNations.presses) >= MAX_NATIONS)
	{
		TouchLegNations.refused.(asked) = true;
		DeleteAttribute(&TouchLegNations, "asked");
		return;
	}
	TouchLegNations.presses = sti(TouchLegNations.presses) + 1;
	UiSend("RIGHTCHANGE_NATION", "press", "");
	UiAwait();
}

// A picture of the game's atlas group.
void TouchLegNations_Image(aref at, string group, string image)
{
	at.image.kind = "atlas";
	at.image.group = group;
	at.image.image = image;
}

void TouchLegNations_Relations(aref nation, int n)
{
	aref relations;
	makearef(relations, nation.relations);
	for (int j = 0; j < MAX_NATIONS; j++)
	{
		aref at = TouchLib_Add(relations, "r");
		at.self = j == n;
		at.kind = "";
		at.label = "";
		if (j != n)
		{
			int relation = GetNationRelation(n, j);
			if (relation == RELATION_FRIEND) at.kind = "ally";
			if (relation == RELATION_NEUTRAL) at.kind = "neutral";
			if (relation == RELATION_ENEMY) at.kind = "war";
			at.label = XI_ConvertString(GetRelationName(relation));
			TouchLegNations_Image(at, "relations", GetRelationName(relation));
		}
	}
}

// The hero's standing with the nations the window shows one for (its <short name>_Text): the number the game keeps
// (ChangeCharacterNationReputation with no change, as the window reads it) and the tone of the game's word for it.
void TouchLegNations_Standing(aref model, aref out)
{
	ref hero = GetMainCharacter();
	aref standing;
	makearef(standing, out.standing);
	for (int n = 0; n < MAX_NATIONS; n++)
	{
		string node = NationShortName(n) + "_Text";
		if (CheckAttribute(model, node))
		{
			int score = ChangeCharacterNationReputation(hero, n, 0);
			aref at = TouchLib_Add(standing, "s");
			at.nation = n;
			at.word = model.(node).label;
			at.score = score;
			at.tone = "";
			if (GetNationReputationName(n, score) != GetNationReputationName(n, 0))
			{
				if (score < 0) at.tone = "hostile";
				else at.tone = "good";
			}
		}
	}
}

void TouchAdpNations_State(aref ui, aref out)
{
	aref model = TouchLegLib_Model(ui);
	string current = TouchLegNations_Candidate(model);
	TouchLegNations_Step(ui, current);
	TouchLegNations_Raised(ui);
	out.title = UiOneLine(TouchLegLib_Label(model, "TITLE"));
	out.flags = false;
	if (CheckAttribute(model, "RIGHTCHANGE_NATION")) out.flags = UiOffers("RIGHTCHANGE_NATION", "press");
	aref nations;
	makearef(nations, out.nations);
	for (int n = 0; n < MAX_NATIONS; n++)
	{
		string name = GetNationNameByType(n);
		aref nation = TouchLib_Add(nations, "n");
		nation.key = name;
		nation.label = XI_ConvertString(name);
		nation.icon = TouchLegLib_Flag(name);
		TouchLegNations_Image(nation, "NATIONS", name);
		TouchLegNations_Relations(nation, n);
		nation.flown = name == current;
		nation.flag = sti(out.flags) && !CheckAttribute(&TouchLegNations, "refused." + name);
	}
	out.standingTitle = "";
	if (CheckAttribute(&GameInterface, "strings.titul")) out.standingTitle = GameInterface.strings.titul;
	TouchLegNations_Standing(model, out);
	if (CheckAttribute(model, "FLAGS"))
	{
		out.apply.label = model.FLAGS.label;
		out.apply.can = UiOffers("FLAGS", "press");
		out.apply.reason = model.FLAGS.reason;
		if (CheckAttribute(&TouchLegNations, "refusal"))
			out.apply.reason = UiText("nations.refusal_" + TouchLegNations.refusal);
	}
	out.help.standing = CheckAttribute(ui, "data.help.standing");
	out.help.flag = CheckAttribute(ui, "data.help.flag");
}

// The game's candidate moves one press of › at a time; State presses on until it is the asked flag.
void TouchAdpNations_Pick(aref ui, int nation)
{
	aref model = TouchLegLib_Model(ui);
	string current = TouchLegNations_Candidate(model);
	string asked = GetNationNameByType(nation);
	if (asked == current || CheckAttribute(&TouchLegNations, "refused." + asked)) return;
	TouchLegNations.asked = asked;
	TouchLegNations.start = current;
	TouchLegNations.presses = 1;
	UiSend("RIGHTCHANGE_NATION", "press", "");
}

// The press of the game's flag button; its knock says no more than that it refused (TouchLegNations_Raised).
void TouchAdpNations_Raise(aref ui)
{
	ref hero = GetMainCharacter();
	DeleteAttribute(&TouchLegNations, "refusal");
	TouchLegNations.raise = CheckAttribute(hero, "DisableChangeFlagMode");
	UiSend("FLAGS", "press", "");
}

// After the game answered a press of its flag button: a change it made sets DisableChangeFlagMode; a press it knocked
// back did not, and the reason is asked of the game (FlagChangeRefusal, which repeats the check FlagsProcess has just
// made, writes included) once, shown until the next press. A press while a change was under way is that reason.
void TouchLegNations_Raised(aref ui)
{
	if (!CheckAttribute(&TouchLegNations, "raise")) return;
	if (UiAnswer() == "waiting") return;
	ref hero = GetMainCharacter();
	bool under = sti(TouchLegNations.raise);
	DeleteAttribute(&TouchLegNations, "raise");
	if (!under && CheckAttribute(hero, "DisableChangeFlagMode")) return;
	if (under) { TouchLegNations.refusal = "wait"; return; }
	if (!sti(ui.data.optional.flag_change)) return;
	string refusal = TouchAdpFlagChange_Refusal();
	if (refusal != "") TouchLegNations.refusal = refusal;
}

void TouchAdpNations_Help(aref ui, string part, aref out)
{
	if (!CheckAttribute(ui, "data.help." + part)) UiError("nations: no help of " + part);
	out.title = XI_ConvertString(ui.data.help.(part).title);
	out.text = TouchLegLib_StockText(GetRPGText(ui.data.help.(part).text));
}

void TouchAdpNations_Section(aref ui, string section)
{
	TouchLegLib_Section(TouchLegLib_Model(ui), section);
}

void TouchAdpNations_Leave(aref ui)
{
	if (CheckAttribute(ui, "model.EXIT_BTN")) { if (UiOffers("EXIT_BTN", "press")) { UiSend("EXIT_BTN", "press", ""); return; } }
	UiBack("");
}
