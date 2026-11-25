// SPDX-License-Identifier: GPL-3.0-only
// Scenario crew: Сен-Пьер streets, opening quest finished, corvette with a navigator, one officer on two posts (cannoner and doctor, the ByWorker perk), a fighter, a passenger without a post, a prisoner and two companions (the hero screens' list of people).
#include "scenarios/scenario.c"

void Scenario_Setup()
{
	Scenario_Unlock();
	Scenario_Money(100000);
	Scenario_Flagship(SHIP_CORVETTE, "Scenario");
	Scenario_Officer("ScenarioNavigator", 12, "navigator");
	// A second post as character_all.c AcceptAddOfficer gives it to a ByWorker officer.
	ref both = Scenario_Officer("ScenarioCannoner", 14, "cannoner");
	SetCharacterPerk(both, "ByWorker");
	pchar.Fellows.Passengers.doctor = sti(both.index);
	both.doctor = true;
	both.isfree = 2;
	// AcceptAddOfficer marks a fighter as holding a post too; without it the game lists him again as a passenger.
	ref fighter = Scenario_Officer("ScenarioFighter", 12, "fighter");
	fighter.isfree = 1;
	ref passenger = Scenario_Officer("ScenarioPassenger", 10, "");
	passenger.name = "Bartholomew";
	passenger.lastname = "de la Fontaine-Beaumarchais";
	ref prisoner = GetCharacter(NPC_GenerateCharacter("ScenarioPrisoner", "citiz_46", "man", "man", 8, PIRATE, -1, true, "citizen"));
	prisoner.location = "None";
	AddPassenger(pchar, prisoner, true);
	Scenario_Companion("ScenarioBrigCap", SHIP_BRIG, "Faucon");
	Scenario_Companion("ScenarioLuggerCap", SHIP_LUGGER, "Mouette");
}

void Scenario_Arrived()
{
}

void Scenario_Trigger()
{
}
