// SPDX-License-Identifier: GPL-3.0-only
// What the games of this engine name on land, for the HUD scenes. Implements contracts/hud.c, which it does not
// include: a segment cannot define what it declares extern.

// The travel stage's pictures are cells of the land target texture (the fast travel table's FRP_* pictures; the
// ship's port is cell 10). The slot name "fr_<n>" is only the place's position in its town's table.
string TouchLegHud_Places[11] = {"store", "shipyard", "tavern", "usurer", "residence", "prison", "church", "fort",
	"portoffice", "lantern", "port"};

string TouchAdpHud_Place(string picture)
{
	string prefix = "landtarget_slb:";
	if (TouchLib_StartsWith(picture, prefix))
	{
		string cell = TouchLib_Cut(picture, strlen(prefix), strlen(picture));
		for (int i = 0; i < 11; i++)
			if (cell == "" + i) return TouchLegHud_Places[i];
	}
	Trace("touch HUD: the travel picture '" + picture + "' is no place this adapter knows: the travel icon");
	return "";
}

// The dialog files of the kinds of people (PROGRAM/dialogs), the keepers of the places by their place.
string TouchAdpHud_Role(string dialog)
{
	switch (dialog)
	{
		case "common_store": return "store";
		case "common_itemtrader": return "store";
		case "common_ammo": return "ammo";
		case "common_shipyard": return "shipyard";
		case "common_tavern": return "tavern";
		case "waitress_dialog": return "tavern";
		case "usurer_dialog": return "usurer";
		case "common_mayor": return "residence";
		case "common_governor": return "residence";
		case "common_church": return "church";
		case "common_portman": return "portoffice";
		case "common_brothel": return "lantern";
		case "common_prison": return "prison";
		case "common_lighthouse": return "port";
		case "enc_officer_dialog": return "officer";
		case "smuggler agent_dialog": return "smuggler";
		case "common_soldier": return "soldier";
		case "common_fort": return "soldier";
	}
	return "";
}

// The land menu's command events (battle_interface/landinterface.c).
string TouchAdpHud_Command(string source)
{
	switch (source)
	{
		case "BI_FastReload": return "travel";
		case "BI_OutDoor": return "exit";
		case "BI_ItemsUse": return "use";
		case "BI_DialogStart": return "talk";
		case "BI_ItemsChange": return "exchange";
		case "BI_TakeItem": return "take";
		case "BI_PlaceItem": return "put";
		case "BI_UseBox": return "chest";
		case "BI_DeadBox": return "search";
		case "BI_TalkSelf": return "thought";
		case "BI_SelfRepair": return "repair";
		case "BI_Alchemy": return "alchemy";
		case "BI_MapAtlas": return "atlas";
		case "BI_ActivateRush": return "rush";
		case "BI_Exit_Deck": return "deck";
	}
	return "";
}

// The names the game's scripts give Log_SetActiveAction on land; "Talk" is the engine's target query's.
string TouchAdpHud_Context(string action)
{
	switch (action)
	{
		case "": return "";
		case "Nothing": return "";
		case "Talk": return "";
		case "Reload": return "enter";
		case "Pick": return "take";
		case "OpenBox": return "open";
		case "EnterToSea": return "toSea";
		case "EnterToShip": return "toShip";
		case "Action": return "action";
	}
	Trace("touch HUD: the quick action '" + action + "' has no look of its own: the plain action button");
	return "action";
}

// The ship models' names (the engine's model) carry the kind as a word, capitalised or not; the order of the words
// matters where one contains another (brigantine before brig).
string TouchLegHud_ShipWords[17] = {"ugger", "loop", "chooner", "arkentine", "rigantine", "rig", "aravel", "leut",
	"luyt", "aleon", "alleon", "rigate", "etch", "artane", "ebec", "anowar", "ineship"};
string TouchLegHud_ShipKinds[17] = {"lugger", "sloop", "schooner", "barkentine", "brigantine", "brig", "caravel",
	"fluyt", "fluyt", "galleon", "galleon", "frigate", "ketch", "tartane", "xebec", "manowar", "manowar"};

string TouchAdpHud_ShipKind(string type)
{
	for (int i = 0; i < 17; i++)
	{
		string word = TouchLegHud_ShipWords[i], kind = TouchLegHud_ShipKinds[i];
		// The word without its first letter, after that letter in either case.
		string first = TouchLib_Cut(kind, 0, 1);
		if (findSubStr(type, first + word, 0) >= 0 || findSubStr(type, TouchLegHud_Upper(first) + word, 0) >= 0)
			return kind;
	}
	return "";
}

string TouchLegHud_Upper(string letter)
{
	string lower = "abcdefghijklmnopqrstuvwxyz", upper = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	int at = findSubStr(lower, letter, 0);
	if (at < 0) return letter;
	return TouchLib_Cut(upper, at, at + 1);
}

// The sea orders' command events (battle_interface/BattleInterface.c).
string TouchAdpHud_Order(string source)
{
	switch (source)
	{
		case "BI_Moor": return "moor";
		case "BI_Board": return "board";
		case "BI_Abordage": return "board";
		case "BI_InstantBoarding": return "board";
		case "BI_LandTroops": return "landTroops";
		case "BI_Map": return "map";
		case "BI_SailTo": return "sailTo";
		case "BI_SailDir": return "sailDir";
		case "BI_SailAway": return "sailAway";
		case "BI_HeaveToDrift": return "drift";
		case "BI_Attack": return "attack";
		case "BI_Defend": return "defend";
		case "BI_Reload": return "reload";
		case "BI_Charge": return "charge";
		case "BI_Speed": return "speed";
		case "BI_CompanionCommand": return "follow";
		case "BI_Ability": return "ability";
		case "BI_Speak": return "talk";
		case "BI_ImmDeath": return "kill";
		case "BI_Cabin": return "cabin";
		case "BI_Boat": return "boat";
		case "BI_Brander": return "brander";
		case "BI_Bomb": return "bomb";
		case "BI_ImmediateReload": return "immediateReload";
		case "BI_LightRepair": return "lightRepair";
		case "BI_InstantRepair": return "instantRepair";
		case "BI_Turn180": return "turn180";
	}
	Trace("touch HUD: the sea order '" + source + "' has no look of its own: the plain orders icon");
	return "";
}
