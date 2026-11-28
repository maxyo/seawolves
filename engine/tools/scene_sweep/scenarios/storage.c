// SPDX-License-Identifier: GPL-3.0-only
// Scenario storage: Сен-Пьер streets, a brig with coffee and food; the run then opens the store's warehouse (not saved).
#include "scenarios/scenario.c"

void Scenario_Setup()
{
	Scenario_Unlock();
	Scenario_Money(200000);
	Scenario_Flagship(SHIP_BRIG, "Scenario");
	SetCrewQuantity(pchar, 30);
	AddCharacterGoods(pchar, GOOD_COFFEE, 40);
	AddCharacterGoods(pchar, GOOD_FOOD, 30);
}

void Scenario_Arrived()
{
}

// The window the store keeper opens once the warehouse is rented (Common_Store.c).
void Scenario_Trigger()
{
	LaunchStorage(sti(GetColonyByIndex(FindColony("FortFrance")).StoreNum));
}
