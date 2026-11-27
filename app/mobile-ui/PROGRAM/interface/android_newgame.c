// SPDX-License-Identifier: GPL-3.0-only
// Authored Android new-game adapter: the start without the tutorial. The game's new-game start schedules
// AndroidStartWithoutTutorial when the player chose it (script fix new-game-without-tutorial).
// This explicit starter kit skips compulsory onboarding, not the main story.
void AndroidTutorialSkipValidate()
{
	// Keep capability metadata on the existing object. A dynamic segment's
	// standalone globals are not recreated when the engine loads a save.
	InterfaceStates.AndroidTutorialSkipSupported = (VERSION_NUMBER2 == "ver. 1.7.3 (26.11.21) x64");
}

void AndroidTutorialShip()
{
	pchar.Ship.Type = GenerateShipHand(pchar, SHIP_LUGGER, 6, 700, 40, 610, 16000, 15.2, 58.8, 1.42);
	pchar.Ship.name = "Аделина";
	SetBaseShipData(pchar);
	pchar.Ship.Cannons.Type = CANNON_TYPE_CANNON_LBS6;
	ResetShipCannonsDamages(pchar);
	SetShipCannonsDamagesNull(pchar);
	NullCharacterGoods(pchar);
	// Modest ready-to-sail cargo, meeting the original provisioning checks.
	AddCharacterGoods(pchar, GOOD_FOOD, 100);
	AddCharacterGoods(pchar, GOOD_MEDICAMENT, 30);
	AddCharacterGoods(pchar, GOOD_RUM, 10);
	AddCharacterGoods(pchar, GOOD_WEAPON, 50);
	AddCharacterGoods(pchar, GOOD_POWDER, 220);
	AddCharacterGoods(pchar, GOOD_BALLS, 100);
	AddCharacterGoods(pchar, GOOD_GRAPES, 100);
	AddCharacterGoods(pchar, GOOD_KNIPPELS, 100);
	AddCharacterGoods(pchar, GOOD_BOMBS, 100);
	SetCrewQuantityOverMax(pchar, 40);
	pchar.Ship.Crew.Morale = 50;
	pchar.Ship.Crew.Exp.Sailors = 50;
	pchar.Ship.Crew.Exp.Cannoners = 50;
	pchar.Ship.Crew.Exp.Soldiers = 50;
}

void AndroidTutorialNavigator()
{
	ref officer = GetCharacter(NPC_GenerateCharacter("Folke", "DeLuck", "man", "man", 1, FRANCE, -1, false, "quest"));
	officer.name = "Фульк";
	officer.lastname = "Делюк";
	officer.rank = 5;
	officer.CompanionDisable = true;
	SetSPECIAL(officer, 8, 9, 6, 5, 10, 7, 5);
	SetSelfSkill(officer, 25, 28, 21, 24, 22);
	SetShipSkill(officer, 10, 5, 24, 22, 25, 15, 5, 15, 18);
	LAi_SetHP(officer, 90, 90);
	LAi_SetImmortal(officer, false);
	SetCharacterPerk(officer, "BasicDefense");
	SetCharacterPerk(officer, "ShipSpeedUp");
	SetCharacterPerk(officer, "HullDamageUp");
	GiveItem2Character(officer, "unarmed");
	EquipCharacterbyItem(officer, "unarmed");
	officer.Dialog.Filename = "Enc_Officer_dialog.c";
	officer.greeting = "Gr_Officer";
	officer.quest.meeting = true;
	officer.quest.OfficerPrice = 20;
	officer.loyality = 30;
	pchar.questTemp.HiringOfficerIDX = GetCharacterIndex("Folke");
	LandEnc_OfficerHired();
	// Role flags match the passenger interface; hiring above establishes payroll.
	pchar.Fellows.Passengers.navigator = GetCharacterIndex("Folke");
	officer.isfree = 1;
	officer.navigator = true;
	Event("eSwitchPerks", "l", sti(officer.index));
}

void AndroidTutorialMainStory()
{
	ref brother = characterFromId("Mishelle");
	ChangeCharacterAddressGroup(brother, "FortFrance_Dungeon", "quest", "quest1");
	LAi_SetStayType(brother);
	brother.dialog.currentnode = "First time";
	brother.greeting = "mishelle_3";
	ref priest = characterFromId("Benua");
	priest.quest.help = true;
	pchar.questTemp.jailCanMove = true;
	pchar.questTemp.Sharlie = "trial";
	pchar.questTemp.Sharlie.Ship = "lugger";
	pchar.questTemp.Trial = "begin";
	SetFunctionTimerCondition("Sharlie_TrialOver", 0, 0, 10, false);
	// Preserve the original 90-day AND rank-three main-story gate.
	SetFunctionTimerCondition("Sharlie_trial", 0, 0, 90, false);
	pchar.quest.Sharlie_trial.function = "Sharlie_GambitStage";
	pchar.quest.Sharlie_trial.win_condition.l1.date.hour = sti(GetTime());
	pchar.quest.Sharlie_trial.win_condition.l2 = "Rank";
	pchar.quest.Sharlie_trial.win_condition.l2.value = 3;
	pchar.quest.Sharlie_trial.win_condition.l2.operation = ">=";
	pchar.quest.Sharlie_FMQ.win_condition.l1 = "Rank";
	pchar.quest.Sharlie_FMQ.win_condition.l1.value = 4;
	pchar.quest.Sharlie_FMQ.win_condition.l1.operation = ">=";
	pchar.quest.Sharlie_FMQ.function = "FMQ_SetConditions";
	AddQuestRecord("Sharlie", "1");
	AddQuestRecordInfo("Sharlie_Father", "1");
	AddQuestRecordInfo("Sharlie_Shevallie", "1");
	AddQuestRecordInfo("Sharlie_Lover", "1");
	AddQuestRecord("Sharlie", "19");
	pchar.alchemy.known = 1;
	SetAlchemyRecipeKnown("cartridge");
	AddQuestRecordInfo("Recipe", "cartridge");
	GiveItem2Character(pchar, "knife_01");
	ref knife = ItemsFromID("knife_01");
	knife.Weight = 1.5;
	knife.price = 0;
}

void AndroidStartWithoutTutorial(string questName)
{
	// Validate dependencies before creating a ship, officer or quest state.
	bool introReady = false;
	if (CheckAttribute(pchar, "GenQuest.VideoAfterQuest"))
		introReady = (pchar.GenQuest.VideoAfterQuest == "Sharlie_Start");
	int town = FindLocation("Fortfrance_town");
	string harbour = "";
	if (town >= 0)
	{
		if (CheckAttribute(&locations[town], "fastreload"))
			harbour = GetCityFrom_Sea(locations[town].fastreload);
	}
	// No effect on saved games or an already-started quest. Failure retains the intro
	// (the full tutorial) and says which precondition failed.
	string refused = "";
	if (VERSION_NUMBER2 != "ver. 1.7.3 (26.11.21) x64") refused += " game version " + VERSION_NUMBER2;
	if (startHeroType != 1) refused += " hero type " + startHeroType;
	if (!introReady) refused += " intro is not Sharlie_Start";
	if (CheckAttribute(pchar, "questTemp.Sharlie")) refused += " questTemp.Sharlie already set";
	if (CheckAttribute(pchar, "AndroidTutorialSkipped")) refused += " already skipped";
	if (GetCharacterIndex("Folke") != -1) refused += " Folke exists";
	if (GetCharacterIndex("Mishelle") < 0) refused += " no Mishelle";
	if (GetCharacterIndex("Benua") < 0) refused += " no Benua";
	if (GetCharacterIndex("Fadey") < 0) refused += " no Fadey";
	if (town < 0) refused += " no Fortfrance_town";
	if (FindLocation(harbour) < 0) refused += " no harbour '" + harbour + "'";
	if (FindLocation("FortFrance_Dungeon") < 0) refused += " no FortFrance_Dungeon";
	if (FindLocation("FortFrance_store") < 0) refused += " no FortFrance_store";
	if (FindLocation("Charles_townhall") < 0) refused += " no Charles_townhall";
	if (FindIsland("Martinique") < 0) refused += " no Martinique";
	if (refused != "")
	{
		Trace("ERROR: tutorial skip refused, the full intro plays instead:" + refused);
		CompleteQuestName("PostVideo_Start", "");
		QuestsCheck();
		return;
	}
	AndroidTutorialShip();
	AndroidTutorialNavigator();
	AndroidTutorialMainStory();
	DeleteAttribute(pchar, "GenQuest.VideoAVI");
	DeleteAttribute(pchar, "GenQuest.VideoAfterQuest");
	DeleteAttribute(pchar, "GenQuest.CannotWait");
	DeleteAttribute(pchar, "NoNavyPenalty");
	bQuestDisableMapEnter = false;
	bDisableFastReload = false;
	chrDisableReloadToLocation = false;
	bNoEatNoRats = false;
	for (int island = 0; island < MAX_ISLANDS; island++)
	{
		if (!CheckAttribute(&Islands[island], "hidden")) Island_SetReloadEnableGlobal(Islands[island].id, true);
	}
	Islands[FindIsland("Martinique")].EffectRadius = 5000;
	LocatorReloadEnterDisable("Fortfrance_town", "reloadPr1", false);
	LocatorReloadEnterDisable("Fortfrance_town", "reload5_back", false);
	LocatorReloadEnterDisable("FortFrance_store", "reload2", false);
	// The governor's private office remains plot-locked.
	LocatorReloadEnterDisable("Charles_townhall", "reload3", true);
	setCharacterShipLocation(pchar, harbour);
	setWDMPointXZ(harbour);
	pchar.AndroidTutorialSkipped = "1.7.3-v1";
	Trace("Tutorial skip applied: Sharlie trial stage, lugger at " + harbour);
	LoadMainCharacterInFirstLocationGroup("Fortfrance_town", "reload", "reload1");
}
