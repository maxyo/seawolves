// SPDX-License-Identifier: GPL-3.0-only
// Scenario sea_board: on the open sea, opening quest finished, corvette with a full crew and two fighters, a pirate brig with a full crew and a captain who may give up; the run then boards the brig (not saved): deck fight, the next deck, the cabin, plunder, prize.
#include "scenarios/scenario.c"

void Scenario_Setup()
{
	Scenario_Unlock();
	Scenario_Money(50000);
	Scenario_Flagship(SHIP_CORVETTE, "Scenario");
	SetCharacterGoods(pchar, GOOD_FOOD, 300);
	SetCharacterGoods(pchar, GOOD_BALLS, 500);
	SetCharacterGoods(pchar, GOOD_WEAPON, 200);
	Scenario_Officer("ScenarioFighter", 20, "fighter");
	Scenario_Officer("ScenarioFighter2", 20, "fighter");
	ref pirate = Scenario_Enemy("ScenarioPirate", SHIP_BRIG, 8);
	// A captain of the ordinary encounters, not a quest one: he may give up, and the boarding goes deck by deck to
	// his cabin as the game's own (Scenario_Enemy's FantomMakeSmallSailor marks its captains as quest ones).
	DeleteAttribute(pirate, "DontRansackCaptain");
	DeleteAttribute(pirate, "AboardToFinalDeck");
	SetCharacterGoods(pirate, GOOD_SUGAR, 150);
	SetCharacterGoods(pirate, GOOD_MEDICAMENT, 40);
	pirate.Money = 3000;
	LAi_SetHP(pchar, 1500.0, 1500.0);
	Scenario_PlaceSea("");
}

void Scenario_Arrived()
{
}

// After the save the run boards the brig at once, as the InstantBoarding perk does (BattleInterface.c,
// Sea_AbordageStartNow).
void Scenario_Trigger()
{
	Sea_AbordageStartNow(SHIP_ABORDAGE, GetCharacterIndex("ScenarioPirate"), true, true);
}
