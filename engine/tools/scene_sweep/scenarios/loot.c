// SPDX-License-Identifier: GPL-3.0-only
// Scenario loot: Сен-Пьер streets, a brig with 30 crew and 50 000 pesos; the run then opens the plunder of the town's store (not saved).
#include "scenarios/scenario.c"

void Scenario_Setup()
{
	Scenario_Unlock();
	Scenario_Money(50000);
	Scenario_Flagship(SHIP_BRIG, "Scenario");
	SetCrewQuantity(pchar, 30);
}

void Scenario_Arrived()
{
}

// The window a captured town's store opens with (colony.c TWN_ExitForPay), without the fort battle before it.
void Scenario_Trigger()
{
	LaunchColonyCapture(GetColonyByIndex(FindColony("FortFrance")));
}
