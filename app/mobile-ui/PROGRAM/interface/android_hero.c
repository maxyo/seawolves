// SPDX-License-Identifier: GPL-3.0-only
// Facts for the C++ screens of the portable UI. The engine raises
// AndroidInterfaceFacts(screen, subject) with every interface snapshot (subject: the character
// the interface shows, or -1) and exports
// GameInterface.AndroidFacts.<list>.<row>.<field> as widgets ANDROID_<LIST>. Only facts the stock
// interface already computes are read; no game rules change. The game's #defines are the program's (its
// main segment is loaded first). Attribute names reach the engine in lower case.
#event_handler("AndroidInterfaceFacts", "AndroidInterfaceFacts");
#event_handler("AndroidCharacterName", "AndroidCharacterName");

// The name the stock screens give a character (quests.c GetFullName), for the engine's screen state: the
// loot / exchange screen names the other ship's captain, an appointed one included (xinterface ANDROID_TRANSFER).
void AndroidCharacterName()
{
	int index = GetEventData();
	GameInterface.AndroidCharacterName = GetFullName(&Characters[index]);
}

void AndroidInterfaceFacts()
{
	string screen = GetEventData();
	int subject = GetEventData();
	GameInterface.AndroidScript = AndroidInterfaceScript();
	DeleteAttribute(&GameInterface, "AndroidFacts");
	if (screen == "colonycapture") AndroidLootValues();
}

// Whether the script of the open interface is still loaded: 1, 0, or -1 when the scripts do not
// say which interface is open. An interface that ends unloads its segment at once (interface.c
// EndCancelInterface) and stays on the screen for a few frames more with its state gone
// (xi_refCharacter: the facts below would come out empty); the engine publishes no model of it
// from then on (xinterface mobile_interface_leaving.hpp).
int AndroidInterfaceScript()
{
	if (CurrentInterface < 0) return -1;
	if (!CheckAttribute(&Interfaces[CurrentInterface], "SectionName")) return -1;
	string section = Interfaces[CurrentInterface].SectionName;
	if (section == "") return -1;
	return SegmentIsLoaded(section);
}

// Looting a town (ColonyCapture.c): what a pack of each listed good is worth (Goods[].Cost, the good's base
// price: the loot is sold elsewhere, a looted town quotes none). The row attribute index is the Goods[] index
// (ColonyCapture.c AddToTable). The screen takes the most valuable per unit of hold space first.
void AndroidLootValues()
{
	int i, idx;
	string tr;
	aref row;
	for (i = 1; i <= 256; i++)
	{
		tr = "tr" + i;
		if (!CheckAttribute(&GameInterface, "TABLE_LIST." + tr + ".index")) break;
		idx = sti(GameInterface.TABLE_LIST.(tr).index);
		makearef(row, GameInterface.AndroidFacts.loot.(tr));
		row.cost = Goods[idx].Cost;
	}
}
