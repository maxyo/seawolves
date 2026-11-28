// SPDX-License-Identifier: GPL-3.0-only
// Scenario sea_surrender: on the open sea, opening quest finished, a corvette and a pirate brig with cargo; the run then boards the brig with the game's own surrender switch on (not saved): the surrender message, the captain's fate, the crew's fate, the ship left without a captain (sink or keep).
#include "scenarios/scenario.c"

void Scenario_Setup()
{
	Scenario_Unlock();
	Scenario_Money(50000);
	Scenario_Flagship(SHIP_CORVETTE, "Scenario");
	SetCharacterGoods(pchar, GOOD_FOOD, 300);
	SetCharacterGoods(pchar, GOOD_BALLS, 500);
	SetCharacterGoods(pchar, GOOD_WEAPON, 200);
	ref pirate = Scenario_Enemy("ScenarioPirate", SHIP_BRIG, 8);
	// An ordinary encounter's captain, not a quest one: only he may give up (LAi_boarding.c).
	DeleteAttribute(pirate, "DontRansackCaptain");
	DeleteAttribute(pirate, "AboardToFinalDeck");
	SetCharacterGoods(pirate, GOOD_SUGAR, 150);
	SetCharacterGoods(pirate, GOOD_MEDICAMENT, 40);
	pirate.Money = 3000;
	Scenario_PlaceSea("");
}

void Scenario_Arrived()
{
}

// The game's debug switch TestRansackCaptain makes the captain give up before the deck fight (LAi_boarding.c),
// the way CheckForSurrender does when the odds are on our side, without its dice.
void Scenario_Trigger()
{
	TestRansackCaptain = true;
	Sea_AbordageStartNow(SHIP_ABORDAGE, GetCharacterIndex("ScenarioPirate"), true, true);
}
