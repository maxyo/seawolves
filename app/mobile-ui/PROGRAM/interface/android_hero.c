// SPDX-License-Identifier: GPL-3.0-only
// Facts for the hero screens of the portable UI. The engine raises
// AndroidInterfaceFacts(screen, subject) with every interface snapshot (subject: the character
// the interface shows, or -1) and exports
// GameInterface.AndroidFacts.<list>.<row>.<field> as widgets ANDROID_<LIST>. Only facts the stock
// interface already computes are read; no game rules change. The game's #defines are the program's (its
// main segment is loaded first). Attribute names reach the engine in lower case.
#event_handler("AndroidInterfaceFacts", "AndroidInterfaceFacts");
#event_handler("AndroidCharacterName", "AndroidCharacterName");

// The name the stock screens give a character (quests.c GetFullName), for the engine's screen state: the
// loot / exchange screen names the other ship's captain, an appointed one included (xinterface ANDROID_TRANSFER).
void AndroidCharacterName()
{
	int index = GetEventData();
	GameInterface.AndroidCharacterName = GetFullName(&Characters[index]);
}

void AndroidInterfaceFacts()
{
	string screen = GetEventData();
	int subject = GetEventData();
	GameInterface.AndroidScript = AndroidInterfaceScript();
	DeleteAttribute(&GameInterface, "AndroidFacts");
	// Salaries (GetMoneyForOfficerFull) divide by the hero's Leadership and Commerce, which the game
	// keeps for up to 25 reads (GetSummonSkillFromName BakSkill) with the navigator penalty of that
	// moment: after a post changed, the cards would keep a salary of the old crew. The cache is dropped
	// as the stock screens drop it before they fill their skill tables (character_all.c FillSkillTables).
	if (screen == "lscchar_all" || screen == "character") DelBakSkillAttr(GetMainCharacter());
	if (screen == "lscchar_all" || screen == "character")
	{
		AndroidHeroSubject(subject);
		AndroidHeroPerks(subject);
		AndroidHeroSkillHelp("TABLE_SPECIAL", subject);
		AndroidHeroSkillHelp("TABLE_SKILL_1", subject);
		AndroidHeroSkillHelp("TABLE_SKILL_2", subject);
	}
	if (screen == "colonycapture") AndroidLootValues();
	if (screen == "ship")
	{
		AndroidShipFacts(subject);
		AndroidShipShares();
	}
	if (screen == "lscchar_all")
	{
		AndroidHeroRoster();
		AndroidHeroPassengers();
		AndroidHeroCandidates();
	}
}

// The stock character_all.c ShowInfoWindow uses these exact row IDs with GetRPGText (RPGDescribe.txt).
// Keep the game's localized descriptions, including their paragraph separators; do not synthesize skill rules.
// With each description goes what the row's value is made of (AndroidSkillSteps).
void AndroidHeroSkillHelp(string table, int subject)
{
	int i;
	string tr, id, text, key;
	aref row;
	int idLngFile = LanguageOpenFile("ItemsDescribe.txt");
	for (i = 0; i < 32; i++)
	{
		tr = "tr" + i;
		if (!CheckAttribute(&GameInterface, table + "." + tr + ".UserData.ID")) continue;
		id = GameInterface.(table).(tr).UserData.ID;
		if (id == "") continue;
		text = GetRPGText(id);
		if (text == "" || text == id) continue;
		key = table + "_" + tr;
		makearef(row, GameInterface.AndroidFacts.skillhelp.(key));
		row.node = table;
		row.skill = id;
		row.text = text;
		if (subject >= 0) AndroidSkillSteps(row, GetCharacter(subject), table, key, id, idLngFile);
	}
	LanguageCloseFile(idLngFile);
}

// What the value of a skill table row is made of, as character_all.c FillSkillTables shows it: the base (the value
// column, GetSkillValue) and the effective value (base + the bonus column: GetCharacterSPECIAL for TABLE_SPECIAL,
// GetSummonSkillFromName for the skills). The steps are the stock computation's own: while chr.SkillTrace exists,
// GetCharacterSPECIALSimple / GetCharacterSkillSimple record their running value after each step (RPGUtilite.c
// SkillTraceAt, script patch skill-steps-trace); a step that changed it is one line. After them come the two stock
// steps outside those functions: an officer's higher skill taking the place of the hero's own
// (GetSummonSkillFromNameSimple) and a ship of a class the navigation does not reach (ApplayNavyPenalty for a
// characteristic, ApplayNavyPenaltyToSkill for a skill). Exported: help.base, help.steps and ANDROID_SKILLSTEPS rows
// {node, skill, kind, label, value, a1..a3}. The screen checks them against the row's own value and bonus, which
// FillSkillTables computed on its own (hero_character.cpp): the lines always add up to what the game uses.
void AndroidSkillSteps(aref help, ref chr, string table, string key, string id, int idLngFile)
{
	bool special = (table == "TABLE_SPECIAL");
	DelBakSkillAttr(chr);
	DeleteAttribute(chr, "SkillTrace");
	chr.SkillTrace = id;
	int own;
	if (special) own = GetCharacterSPECIALSimple(chr, id);
	else own = GetCharacterSkillSimple(chr, id);
	// The recorded steps are read before any other stock skill function runs (each would record into them).
	aref recorded, step;
	makearef(recorded, chr.SkillTrace);
	int n = GetAttributesNum(recorded);
	if (n == 0)
	{
		// no such characteristic or skill: the stock functions return a constant
		DeleteAttribute(chr, "SkillTrace");
		return;
	}
	step = GetAttributeN(recorded, 0);
	if (GetAttributeValue(step) != "base")
	{
		trace("ERROR: AndroidSkillSteps: the first step of " + id + " is " + GetAttributeValue(step) + ", not base");
		DeleteAttribute(chr, "SkillTrace");
		return;
	}
	int base = sti(step.value);
	int prev = base;
	int count = 0;
	int i, value;
	string source, last;
	last = "";
	aref line;
	for (i = 1; i < n; i++)
	{
		step = GetAttributeN(recorded, i);
		value = sti(step.value);
		if (value == prev) continue;
		source = GetAttributeValue(step);
		// one source spread over several stock lines (KnifeAztec: four skills) is one line
		if (source == last)
		{
			line.value = sti(line.value) + value - prev;
			prev = value;
			continue;
		}
		line = AndroidSkillStepRow(table, key, id, count, value - prev);
		AndroidSkillStepSource(line, chr, source, idLngFile);
		last = source;
		prev = value;
		count++;
	}
	DeleteAttribute(chr, "SkillTrace");
	int result = own;
	int officer;
	if (!special)
	{
		result = GetSummonSkillFromNameSimple(chr, id);
		if (result != own)
		{
			line = AndroidSkillStepRow(table, key, id, count, result - own);
			line.kind = "officer";
			officer = AndroidSkillOfficer(id, result);
			if (officer >= 0) line.label = GetFullName(GetCharacter(officer));
			else trace("ERROR: AndroidSkillSteps: no officer on a post has " + id + " " + result);
			count++;
		}
		own = result;
		result = ApplayNavyPenaltyToSkill(chr, id, own);
	}
	else result = ApplayNavyPenalty(chr, id, own);
	if (result != own)
	{
		line = AndroidSkillStepRow(table, key, id, count, result - own);
		line.kind = "navy";
		// the words of the line: the ship's class, the navigation the stock penalty measured, the one it needs
		line.a1 = GetCharacterShipClass(chr);
		line.a2 = GetSummonSkillFromNameSimple(chr, "Sailing");
		line.a3 = GetShipClassNavySkill(GetCharacterShipClass(chr));
		count++;
	}
	help.base = base;
	help.steps = count;
}

aref AndroidSkillStepRow(string table, string key, string id, int index, int value)
{
	aref line;
	string name = key + "_" + index;
	DeleteAttribute(&GameInterface, "AndroidFacts.skillsteps." + name);
	makearef(line, GameInterface.AndroidFacts.skillsteps.(name));
	line.node = table;
	line.skill = id;
	line.value = value;
	line.label = "";
	return line;
}

// The line of a step SkillTraceAt recorded: its kind (the screen words it, hero.step_<kind>) and, for an item (worn,
// carried, the suit, the blade), the game's name of the item in the player's language (as the items screen names it).
void AndroidSkillStepSource(aref line, ref chr, string source, int idLngFile)
{
	string item = "";
	if (HasSubStr(source, "item:")) item = strcut(source, 5, strlen(source) - 1);
	if (source == "suit") item = GetCharacterEquipSuitID(chr);
	if (source == "blade") item = GetCharacterEquipByGroup(chr, "blade");
	if (item != "")
	{
		line.kind = "item";
		int idx = FindItem(item);
		if (idx < 0)
		{
			trace("ERROR: AndroidSkillStepSource: no item " + item + " (step " + source + ")");
			line.label = item;
			return;
		}
		line.label = LanguageConvertString(idLngFile, Items[idx].name);
		return;
	}
	line.kind = source; // health, mangarosa, curse, drunk, herotype, overload, limit
	if (source == "health") line.a1 = GetHealthNum(chr);
}

// The officer whose skill GetSummonSkillFromNameSimple took for the hero's: the one on a post with that value.
int AndroidSkillOfficer(string id, int value)
{
	ref mc = GetMainCharacter();
	int i, idx;
	string post;
	for (i = 1; i <= 6; i++)
	{
		post = GetOfficerTypeByNum(i);
		if (!CheckAttribute(mc, "Fellows.Passengers." + post)) continue;
		idx = sti(mc.Fellows.Passengers.(post));
		if (idx < 0) continue;
		if (GetCharacterSkillSimple(GetCharacter(idx), id) == value) return idx;
	}
	return -1;
}

// Whether the script of the open interface is still loaded: 1, 0, or -1 when the scripts do not
// say which interface is open. An interface that ends unloads its segment at once (interface.c
// EndCancelInterface) and stays on the screen for a few frames more with its state gone
// (xi_refCharacter: the facts below would come out empty); the engine publishes no model of it
// from then on (xinterface mobile_interface_leaving.hpp).
int AndroidInterfaceScript()
{
	if (CurrentInterface < 0) return -1;
	if (!CheckAttribute(&Interfaces[CurrentInterface], "SectionName")) return -1;
	string section = Interfaces[CurrentInterface].SectionName;
	if (section == "") return -1;
	return SegmentIsLoaded(section);
}

// The key skill of an officer post (character_all.c slot numbers, GetOfficerTypeByNum).
string AndroidPostSkill(ref chr, string post)
{
	if (post == "navigator") return "Sailing";
	if (post == "boatswain") return "Grappling";
	if (post == "cannoner") return "Cannons";
	if (post == "doctor") return "Defence";
	if (post == "treasurer") return "Commerce";
	if (post == "carpenter") return "Repair";
	// fighters: their best blade
	string best = "FencingL";
	if (GetSkillValue(chr, "Skill", "FencingS") > GetSkillValue(chr, "Skill", best)) best = "FencingS";
	if (GetSkillValue(chr, "Skill", "FencingH") > GetSkillValue(chr, "Skill", best)) best = "FencingH";
	return best;
}

// Loyalty word as interface_utils.c SetOTHERMiniTable shows it; the hero has none (his
// authority is a number, see AndroidHeroRow).
string AndroidLoyaltyWord(ref chr)
{
	return XI_ConvertString(GetLoyalityName(makeint(GetCharacterLoyality(chr) * 100 / MAX_LOYALITY)));
}

void AndroidHeroRow(aref row, ref chr)
{
	row.character = sti(chr.index);
	row.label = GetFullName(chr);
	row.face = chr.FaceId;
	row.rank = sti(chr.rank);
	if (sti(chr.index) == GetMainCharacterIndex())
	{
		// The value of the stock LOYALITY bar for the hero (character_all.c SetVariable).
		if (CheckAttribute(chr, "reputation.authority")) row.authority = sti(chr.reputation.authority);
	}
	else
	{
		row.loyalty = AndroidLoyaltyWord(chr);
		row.salary = GetMoneyForOfficerFull(chr);
	}
}

void AndroidHeroSubject(int subject)
{
	if (subject < 0) return;
	ref chr = GetCharacter(subject);
	aref row;
	makearef(row, GameInterface.AndroidFacts.subject.s0);
	AndroidHeroRow(row, chr);
	row.hero = "0";
	if (sti(chr.index) == GetMainCharacterIndex()) row.hero = "1";
	// Perk points and the experience towards the next one (character_all.c SetControlsTabMode).
	if (CheckAttribute(chr, "perks.FreePoints_self")) row.freeSelf = sti(chr.perks.FreePoints_self);
	if (CheckAttribute(chr, "perks.FreePoints_ship")) row.freeShip = sti(chr.perks.FreePoints_ship);
	if (CheckAttribute(chr, "perks.FreePoints_self_exp")) row.expSelf = sti(chr.perks.FreePoints_self_exp);
	if (CheckAttribute(chr, "perks.FreePoints_ship_exp")) row.expShip = sti(chr.perks.FreePoints_ship_exp);
	row.rateSelf = GetFreePoints_SelfRate(chr);
	row.rateShip = GetFreePoints_ShipRate(chr);
}

// The perk tree of the hero screens (engine hero_character.cpp, perk_tree.hpp): every perk of
// TABLE_PERKS and, recursively, every perk it requires, also of the other group (those are shown
// read-only; only TABLE_PERKS rows can be learned). Per perk: its stock caption and description
// (AbilityDescribe.txt), group, learned, held by an officer and the number of required perks. Per
// requirement an edge (source, target, met). Whether a perk can be learned is ChoosePerk's answer
// (PERK_ACCEPT), not decided here. ChoosePerk requires ALL condition attributes: rule "stock-all-v1".
// IDs stay apart from the localized text. The rule field marks a visited perk (cycles end); depth
// bounds modded graphs.
void AndroidHeroPerkGraph(ref chr, string perk, int depth)
{
	if (depth > 64) return;
	if (CheckAttribute(&GameInterface, "AndroidFacts.perks." + perk + ".rule")) return;
	if (!CheckAttribute(&ChrPerksList, "list." + perk)) return;
	aref row, conditions, edge;
	makearef(row, GameInterface.AndroidFacts.perks.(perk));
	row.rule = "stock-all-v1";
	row.requirements = 0;
	row.label = GetConvertStr(perk, "AbilityDescribe.txt");
	row.text = GetConvertStr("perk" + perk, "AbilityDescribe.txt");
	row.type = "self";
	if (CheckAttribute(&ChrPerksList, "list." + perk + ".BaseType")) row.type = ChrPerksList.list.(perk).BaseType;
	row.learned = "0";
	if (CheckCharacterPerk(chr, perk)) row.learned = "1";
	row.officer = "0";
	if (sti(chr.index) == GetMainCharacterIndex() && row.learned == "0" && GetOfficersPerkUsing(chr, perk)) row.officer = "1";
	string cond, key;
	int c;
	if (CheckAttribute(&ChrPerksList, "list." + perk + ".condition"))
	{
		makearef(conditions, ChrPerksList.list.(perk).condition);
		row.requirements = GetAttributesNum(conditions);
		for (c = 0; c < GetAttributesNum(conditions); c++)
		{
			cond = GetAttributeName(GetAttributeN(conditions, c));
			key = perk + "_" + c;
			makearef(edge, GameInterface.AndroidFacts.perkedges.(key));
			edge.source = cond;
			edge.target = perk;
			edge.rule = "stock-all-v1";
			edge.met = "0";
			if (CheckCharacterPerk(chr, cond)) edge.met = "1";
			AndroidHeroPerkGraph(chr, cond, depth + 1);
		}
	}
}

void AndroidHeroPerks(int subject)
{
	if (subject < 0) return;
	ref chr = GetCharacter(subject);
	int i;
	string tr, perk;
	for (i = 1; i <= 64; i++)
	{
		tr = "tr" + i;
		if (!CheckAttribute(&GameInterface, "TABLE_PERKS." + tr + ".UserData.ID")) break;
		perk = GameInterface.TABLE_PERKS.(tr).UserData.ID;
		AndroidHeroPerkGraph(chr, perk, 0);
	}
}

// Every slot of CHARACTERS_SCROLL as character_all.h FillCharactersScroll fills it: the captain, six posts and
// three fighters (a free one has no character), then companions, passengers and prisoners. Officers with the
// ByWorker perks hold several posts, so one person can fill several slots. role: the slot's caption (str1).
void AndroidHeroRoster()
{
	int i, idx;
	string pic, post, skill, caption;
	aref row;
	for (i = 0; i < sti(GameInterface.CHARACTERS_SCROLL.ListSize); i++)
	{
		pic = "pic" + (i + 1);
		idx = sti(GameInterface.CHARACTERS_SCROLL.(pic).character);
		post = GetOfficerTypeByNum(i);
		caption = GameInterface.CHARACTERS_SCROLL.(pic).str1;
		makearef(row, GameInterface.AndroidFacts.roster.(pic));
		row.slot = i;
		row.post = post;
		row.role = strcut(caption, 1, strlen(caption) - 1); // "#" marks a literal caption
		if (i > 0 && idx <= 0) continue; // a free post
		if (i == 0) idx = GetMainCharacterIndex();
		AndroidHeroRow(row, GetCharacter(idx));
		if (post != "")
		{
			skill = AndroidPostSkill(GetCharacter(idx), post);
			row.skill = XI_ConvertString(skill);
			row.skillValue = GetSkillValue(GetCharacter(idx), "Skill", skill);
		}
	}
}

// Passengers that hold no post (the stock screen lists them only inside its post chooser).
void AndroidHeroPassengers()
{
	ref mchr = GetMainCharacter();
	int i, idx;
	ref chr;
	aref row;
	int n = 0;
	string name;
	for (i = 0; i < GetPassengersQuantity(mchr); i++)
	{
		idx = GetPassenger(mchr, i);
		if (idx < 0) continue;
		chr = GetCharacter(idx);
		if (CheckAttribute(chr, "prisoned") && sti(chr.prisoned) == true) continue;
		if (CheckAttribute(chr, "isfree") && sti(chr.isfree) > 0) continue;
		name = "p" + n;
		makearef(row, GameInterface.AndroidFacts.passengers.(name));
		AndroidHeroRow(row, chr);
		n++;
	}
}

// The post chooser's candidates (PASSENGERSLIST, filled by character_all.c FillPassengerScroll
// for the slot in CHARACTERS_SCROLL.current) with the key skill of that post.
void AndroidHeroCandidates()
{
	if (!CheckAttribute(&GameInterface, "PASSENGERSLIST.ListSize")) return;
	int slot = sti(GameInterface.CHARACTERS_SCROLL.current);
	string post = GetOfficerTypeByNum(slot);
	int i, idx;
	string pic, skill;
	aref row;
	for (i = 0; i < sti(GameInterface.PASSENGERSLIST.ListSize); i++)
	{
		pic = "pic" + (i + 1);
		if (!CheckAttribute(&GameInterface, "PASSENGERSLIST." + pic + ".character")) continue;
		idx = sti(GameInterface.PASSENGERSLIST.(pic).character);
		makearef(row, GameInterface.AndroidFacts.candidates.(pic));
		AndroidHeroRow(row, GetCharacter(idx));
		row.index = i;
		skill = AndroidPostSkill(GetCharacter(idx), post);
		row.skill = XI_ConvertString(skill);
		row.skillValue = GetSkillValue(GetCharacter(idx), "Skill", skill);
	}
}

// The ship screen (ship.c, subject = the companion whose ship it shows): what its status bars,
// windows and buttons know but the exported nodes do not carry. ships.h SHIP_NOTUSED = 1000.
void AndroidShipFacts(int subject)
{
	if (subject < 0) return;
	ref chr = GetCharacter(subject);
	ref mc = GetMainCharacter();
	aref row;
	makearef(row, GameInterface.AndroidFacts.ship.s0);
	row.hero = "0";
	if (subject == GetMainCharacterIndex()) row.hero = "1";
	row.money = sti(mc.money);
	// «Обмен» (ShipChange): only for a companion's ship while the hero has one, and not from the
	// journal's ships at anchor. rule copy: ship.c ShipChange's outer condition. The game keeps it only inside
	// that handler, which opens no window when it fails, so the button cannot be offered from the game's word;
	// every other refusal is the game's own window.
	row.exchange = "0";
	if (!CheckAttribute(mc, "SystemInfo.ShowShip") && chr.id != mc.id && sti(mc.ship.type) != 1000) row.exchange = "1";
	int iShip = sti(chr.ship.type);
	if (iShip == 1000) return;
	ref realShip = GetRealShip(iShip);
	row.type = XI_ConvertString(realShip.BaseName);
	// CheckForRename: the name field takes focus only then.
	row.rename = "0";
	if (GetShipRemovable(chr)) row.rename = "1";
	row.morale = sti(chr.Ship.crew.morale);
	row.moraleprice = GetCharacterRaiseCrewMoraleMoney(chr);
	// SetCrewExpTable's BAR_Sailors / BAR_Cannoners / BAR_Soldiers.
	if (GetCrewQuantity(chr) > 0)
	{
		row.sailors = sti(chr.Ship.Crew.Exp.Sailors);
		row.cannoners = sti(chr.Ship.Crew.Exp.Cannoners);
		row.soldiers = sti(chr.Ship.Crew.Exp.Soldiers);
	}
	// The most guns each side takes (CANNONS_WINDOW arrows stop there).
	row.bow = GetBortCannonsQtyMax(chr, "cannonf");
	row.stern = GetBortCannonsQtyMax(chr, "cannonb");
	row.starboard = GetBortCannonsQtyMax(chr, "cannonr");
	row.port = GetBortCannonsQtyMax(chr, "cannonl");
}

// The crew's shares (ship.c ShowPartitionWindow writes them into one text): the month's shares and the debts.
int AndroidShipShare(ref mc, string path)
{
	if (!CheckAttribute(mc, path)) return 0;
	return sti(mc.(path));
}

void AndroidShipShares()
{
	ref mc = GetMainCharacter();
	aref row;
	makearef(row, GameInterface.AndroidFacts.shares.s0);
	row.captain = AndroidShipShare(mc, "Partition.MonthPart.Hero");
	row.officers = AndroidShipShare(mc, "Partition.MonthPart.Officers");
	row.crew = AndroidShipShare(mc, "Partition.MonthPart.Crew");
	row.month = AndroidShipShare(mc, "Partition.MonthPart");
	row.state = AndroidShipShare(mc, "Partition.MonthPart.Gower");
	row.last = AndroidShipShare(mc, "CrewPayment");
	if (CheckAttribute(mc, "paymentdate")) row.paid = mc.paymentdate;
}

// Looting a town (ColonyCapture.c): what a pack of each listed good is worth (Goods[].Cost, the good's base
// price: the loot is sold elsewhere, a looted town quotes none). The row attribute index is the Goods[] index
// (ColonyCapture.c AddToTable). The screen takes the most valuable per unit of hold space first.
void AndroidLootValues()
{
	int i, idx;
	string tr;
	aref row;
	for (i = 1; i <= 256; i++)
	{
		tr = "tr" + i;
		if (!CheckAttribute(&GameInterface, "TABLE_LIST." + tr + ".index")) break;
		idx = sti(GameInterface.TABLE_LIST.(tr).index);
		makearef(row, GameInterface.AndroidFacts.loot.(tr));
		row.cost = Goods[idx].Cost;
	}
}
