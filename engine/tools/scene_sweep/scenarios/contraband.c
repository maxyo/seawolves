// SPDX-License-Identifier: GPL-3.0-only
// Scenario contraband: Сен-Пьер streets, a brig with coffee, tobacco, wine and cinnamon, 200 000 pesos; the run then opens the smugglers' trade on Сен-Пьер's store (not saved).
#include "scenarios/scenario.c"

void Scenario_Setup()
{
	Scenario_Unlock();
	Scenario_Money(200000);
	Scenario_Flagship(SHIP_BRIG, "Scenario");
	SetCrewQuantity(pchar, 30);
	AddCharacterGoods(pchar, GOOD_COFFEE, 40);
	AddCharacterGoods(pchar, GOOD_TOBACCO, 40);
	AddCharacterGoods(pchar, GOOD_WINE, 40);
	AddCharacterGoods(pchar, GOOD_CINNAMON, 20);
	AddCharacterGoods(pchar, GOOD_FOOD, 30);
}

void Scenario_Arrived()
{
}

// The window the smugglers on a shore open (Smuggler_OnShore_dialog.c), without the night on the shore: the store's
// trader stands in for them, and the window's exit, which clears the shore's smugglers, logs their absence.
void Scenario_Trigger()
{
	LaunchContrabandTrade(CharacterFromID("FortFrance_trader"), sti(GetColonyByIndex(FindColony("FortFrance")).StoreNum));
}
