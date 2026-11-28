// SPDX-License-Identifier: GPL-3.0-only
// Scenario sea_exchange: on the calm open sea, no enemy, opening quest finished, corvette with officers and a loaded companion brig (sail to it, ship exchange at sea, the deck boat to its captain).
#include "scenarios/scenario.c"

void Scenario_Setup()
{
	Scenario_Unlock();
	Scenario_Money(50000);
	Scenario_Flagship(SHIP_CORVETTE, "Scenario");
	SetCharacterGoods(pchar, GOOD_FOOD, 300);
	SetCharacterGoods(pchar, GOOD_BALLS, 500);
	SetCharacterGoods(pchar, GOOD_PLANKS, 40);
	Scenario_Officer("ScenarioNavigator", 12, "navigator");
	Scenario_Officer("ScenarioFighter", 12, "fighter");
	ref brig = Scenario_Companion("ScenarioBrigCap", SHIP_BRIG, "Faucon");
	SetCharacterGoods(brig, GOOD_SUGAR, 200);
	SetCharacterGoods(brig, GOOD_RUM, 60);
	SetCharacterGoods(brig, GOOD_BALLS, 300);
	Scenario_PlaceSea("");
}

void Scenario_Arrived()
{
}

void Scenario_Trigger()
{
}
