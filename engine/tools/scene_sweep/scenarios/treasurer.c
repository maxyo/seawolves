// SPDX-License-Identifier: GPL-3.0-only
// Scenario treasurer: Сен-Пьер streets, opening quest finished, a lugger with a treasurer, standing orders of balls and powder, a loaded brig companion; the run then opens the purchase list (not saved).
#include "scenarios/scenario.c"

void Scenario_Setup()
{
	Scenario_Unlock();
	Scenario_Money(100000);
	Scenario_Flagship(SHIP_LUGGER, "Scenario");
	SetCharacterGoods(pchar, GOOD_BALLS, 400);
	SetCharacterGoods(pchar, GOOD_FOOD, 160);
	Scenario_Officer("ScenarioTreasurer", 12, "treasurer");
	ref brig = Scenario_Companion("ScenarioBrigCap", SHIP_BRIG, "Faucon");
	SetCharacterGoods(brig, GOOD_POWDER, 300);
	// Two standing orders, as the purchase list keeps them (GoodsTransfer.c).
	string hero = pchar.id;
	pchar.TransferGoods.(hero).Balls = 600;
	pchar.TransferGoods.(hero).Powder = 400;
}

void Scenario_Arrived()
{
}

// The purchase list as the treasurer's dialogue turns it on and opens it (Enc_Officer_dialog.c), after the save.
void Scenario_Trigger()
{
	pchar.TransferGoods.Enable = true;
	LaunchTransferGoodsScreen();
}
