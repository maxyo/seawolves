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
