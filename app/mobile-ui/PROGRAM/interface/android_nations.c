// SPDX-License-Identifier: GPL-3.0-only
// Authored presentation adapter for the nations screen: the hero's reputation with each nation as the number the
// game keeps (ChangeCharacterNationReputation with no change, as NationRelation.c GetNationReputation reads it for
// its words) and the tone of its word: "" for the game's word of a neutral 0, else "hostile" below 0, "good" above.
// Which flags the ship may raise and when it may change one are the game's (SetNewNation, FlagsProcess).
#event_handler("AndroidNationFacts", "AndroidNationFacts");
void AndroidNationFacts()
{
	ref mchar = GetMainCharacter();
	DeleteAttribute(&GameInterface, "AndroidNations");
	int n, score;
	string name;
	aref row;
	for (n = 0; n < MAX_NATIONS; n++)
	{
		name = NationShortName(n);
		makearef(row, GameInterface.AndroidNations.(name));
		score = ChangeCharacterNationReputation(mchar, n, 0);
		row.score = score;
		row.tone = "";
		if (GetNationReputationName(n, score) != GetNationReputationName(n, 0))
		{
			if (score < 0) row.tone = "hostile";
			else row.tone = "good";
		}
	}
}
