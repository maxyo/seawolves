// SPDX-License-Identifier: GPL-3.0-only
// Scenario self_repair: a Martinique shore as if landed from the sea, a brig at hull 44 % with a mast down, planks and sailcloth, the hero with the self-repair perk: the land menu's repair.
#include "scenarios/scenario.c"

void Scenario_Setup()
{
	Scenario_Unlock();
	Scenario_Place("Shore38", "reload", "reload1_back");
	Scenario_Money(50000);
	Scenario_Flagship(SHIP_BRIG, "Scenario");
	SetCrewQuantity(pchar, 60);
	SetCharacterPerk(pchar, "SelfRepair");
	AddCharacterGoods(pchar, GOOD_PLANKS, 80);
	AddCharacterGoods(pchar, GOOD_SAILCLOTH, 80);
	AddCharacterGoods(pchar, GOOD_FOOD, 50);
	Scenario_Damage(0.45, 0.4, 1, 0);
}

// The land menu offers the repair only where the hero came ashore (landinterface.c: location.from_sea).
void Scenario_Arrived()
{
	pchar.location.from_sea = "Shore38";
}

void Scenario_Trigger()
{
}
