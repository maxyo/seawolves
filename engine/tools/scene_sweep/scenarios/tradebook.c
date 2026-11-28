// SPDX-License-Identifier: GPL-3.0-only
// Scenario tradebook: Сен-Пьер streets, the prices of three colonies' stores remembered as a visit remembers them; the run then opens the trade book (not saved).
#include "scenarios/scenario.c"

void Scenario_Setup()
{
	Scenario_Unlock();
	// What a talk with each store keeper leaves (Common_Store.c): the store's prices in the hero's price list.
	SetPriceListByStoreMan(GetColonyByIndex(FindColony("FortFrance")));
	SetPriceListByStoreMan(GetColonyByIndex(FindColony("PortRoyal")));
	SetPriceListByStoreMan(GetColonyByIndex(FindColony("Havana")));
}

void Scenario_Arrived()
{
}

void Scenario_Trigger()
{
	LaunchTradeBook();
}
