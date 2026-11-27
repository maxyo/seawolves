// A game's program as the touch interface's scripts meet one: an interface table filled by Main, globals, functions
// and an event handler.
object Interfaces[36];
object GameInterface;
int GameValue;
string GameName;

#event_handler("GameTick", "GameTick");

void GameTick()
{
}

int GameFunction(int count, string name)
{
	return count;
}

// The hours of the rest interface (TavernWait.c's global there), set by the checks to the recorded state's.
int iHourWait;

// The game functions the trade adapter calls for a storage row's pack weight and the loot plan: TestGoods.g<good>.{units (pieces in a
// pack), weight (cwt a pack)}, every good packs of 10 of 1 cwt. Each started pack counts whole.
object TestGoods;

int GetGoodWeightByType(int good, int quantity)
{
	string slot = "g" + good;
	int units = sti(TestGoods.(slot).units);
	return (quantity + units - 1) / units * sti(TestGoods.(slot).weight);
}

// And for the loot plan: the pieces of whole packs that weigh at most `weight`.
int GetGoodQuantityByWeight(int good, int weight)
{
	string slot = "g" + good;
	return weight / sti(TestGoods.(slot).weight) * sti(TestGoods.(slot).units);
}

// The goods of a trade window's rows by name, and the open store's trade type of each (store.c's refStore, a global
// of that interface in the game): good 4 is imported here, good 5 exported, the others plain.
object Goods[30];
object TestStore;
ref refStore;

// The game's items, by the index of a chest's rows: their weight of one piece; item 4 (gold) weighs nothing.

// What the nations adapter calls of the game: five nations, their relations TestNations.r<a>_<b> (b's to a's), the
// hero's score with each TestNations.score<n> and its word: one word for 0..20, else the number.
#define MAX_NATIONS 5
#define RELATION_FRIEND 0
#define RELATION_NEUTRAL 1
#define RELATION_ENEMY 2
object TestNations;
object TestHero;
string TestNationNames[5] = {"England", "France", "Spain", "Holland", "Pirate"};
string TestNationShort[5] = {"eng", "fra", "spa", "hol", "pir"};

ref GetMainCharacter()
{
	return &TestHero;
}

string GetNationNameByType(int nation)
{
	return TestNationNames[nation];
}

string NationShortName(int nation)
{
	return TestNationShort[nation];
}

int GetNationRelation(int a, int b)
{
	string pair = "r" + a + "_" + b;
	return sti(TestNations.(pair));
}

string GetRelationName(int relation)
{
	if (relation == RELATION_FRIEND) return "Friend";
	if (relation == RELATION_ENEMY) return "Enemy";
	return "Neutral";
}

int ChangeCharacterNationReputation(ref hero, int nation, int change)
{
	string score = "score" + nation;
	return sti(TestNations.(score));
}

string GetNationReputationName(int nation, int score)
{
	if (score >= 0 && score <= 20) return "Neutral";
	return "Score " + score;
}

string XI_ConvertString(string key)
{
	return key;
}

string GetRPGText(string key)
{
	return "~
" + key + " text
~
more";
}

// What the crew adapter reads of the game: the characters whose ships the hire's scroll shows, the colony whose
// tavern it is, and the crews the window keeps from before the amount set (NullCharacter.TavernBak).
object Characters[32];
object Colonies[2];
object NullCharacter;
object TestLocation;
ref loadedLocation;

int FindColony(string id)
{
	for (int i = 0; i < 2; i++)
	{
		if (Colonies[i].id == id) return i;
	}
	return -1;
}

// What the items adapter calls of the game: Items[] (the rapier 7, the dagger 9, the potion 30, an amulet 31, a timed
// amulet 32), the people Characters[] (the captain 1 Anne Bonny, the navigator 9 Jack Rackham, the passenger 2
// Bartholomew de la Fontaine-Beaumarchais), what they wear (equip.<group>, equip_item.<slot>: each the rapier, three
// artefact slots) and the item texts: their keys.
object Items[40];

ref GetCharacter(int index)
{
	return &Characters[index];
}

string GetFullName(ref chr)
{
	return chr.name + " " + chr.lastname;
}

int GetItemIndex(string id)
{
	for (int i = 0; i < 40; i++)
	{
		if (CheckAttribute(&Items[i], "id"))
		{
			if (Items[i].id == id) return i;
		}
	}
	return -1;
}

ref GetColonyByIndex(int colony)
{
	return &Colonies[colony];
}

// A crew of `men` with its experience and morale.
void TestCrew(ref at, int men, int sailors, int cannoners, int soldiers, int morale)
{
	at.Ship.Crew.Quantity = men;
	at.Ship.Crew.Exp.Sailors = sailors;
	at.Ship.Crew.Exp.Cannoners = cannoners;
	at.Ship.Crew.Exp.Soldiers = soldiers;
	at.Ship.Crew.Morale = morale;
}

// What the self repair adapter reads of the game: the goods by name and what the squadron's holds carry of
// each (TestHold.g<good>).
string TestGoodNames[2] = {"Planks", "Sailcloth"};
object TestHold;

int FindGood(string name)
{
	for (int i = 0; i < 2; i++)
	{
		if (TestGoodNames[i] == name) return i;
	}
	return -1;
}

int GetSquadronGoods(ref chr, int good)
{
	string slot = "g" + good;
	return sti(TestHold.(slot));
}

bool IsEquipCharacterByItem(ref chr, string id)
{
	aref equip;
	makearef(equip, chr.equip);
	for (int i = 0; i < GetAttributesNum(equip); i++)
	{
		if (GetAttributeValue(GetAttributeN(equip, i)) == id) return true;
	}
	return false;
}

bool IsEquipCharacterByArtefact(ref chr, string id)
{
	return GetCharacterEquipSlot(chr, id) != "";
}

string GetCharacterEquipByGroup(ref chr, string group)
{
	if (CheckAttribute(chr, "equip." + group)) return chr.equip.(group);
	return "";
}

string GetCharacterEquipBySlot(ref chr, string slot)
{
	if (CheckAttribute(chr, "equip_item." + slot)) return chr.equip_item.(slot);
	return "";
}

string GetCharacterEquipSlot(ref chr, string id)
{
	aref equip;
	makearef(equip, chr.equip_item);
	for (int i = 0; i < GetAttributesNum(equip); i++)
	{
		if (GetAttributeValue(GetAttributeN(equip, i)) == id) return GetAttributeName(GetAttributeN(equip, i));
	}
	return "";
}

string GetCharacterSlotRemainTime(ref chr, string slot)
{
	if (!CheckAttribute(chr, "equip_item." + slot + ".time")) return "";
	if (sti(chr.equip_item.(slot).time) < 0) return "";
	return chr.equip_item.(slot).time;
}

int LanguageOpenFile(string name)
{
	return 1;
}

string LanguageConvertString(int file, string key)
{
	return key;
}

void LanguageCloseFile(int file)
{
}

string GetAssembledString(string text, aref item)
{
	return text;
}

void TestItem(int index, string id, string group, string weight, string describe)
{
	Items[index].id = id;
	if (group != "") Items[index].groupID = group;
	Items[index].Weight = weight;
	Items[index].describe = describe;
}

void TestPerson(int index, string name, string lastname, string face)
{
	Characters[index].index = index;
	Characters[index].name = name;
	Characters[index].lastname = lastname;
	Characters[index].FaceId = face;
	Characters[index].equip.blade = "rapier";
	Characters[index].equip_item.slot1 = "";
	Characters[index].equip_item.slot2 = "";
	Characters[index].equip_item.slot3 = "";
}

void TestItems()
{
	TestItem(7, "rapier", "blade", "2.4", "A light blade.");
	Items[7].Attack = 60;
	Items[7].Balance = 1.4;
	Items[7].FencingType = "Rapiers";
	TestItem(9, "dagger", "blade", "1.5", "A short blade.");
	Items[9].Attack = 34.5;
	Items[9].Balance = 2.0;
	Items[9].FencingType = "Rapiers";
	TestItem(30, "potion1", "", "0.2", "Heals wounds.");
	TestItem(31, "amulet1", "slot", "0.5", "A charm.");
	Items[31].time = -1;
	TestItem(32, "amulet2", "slot", "0.5", "A charm that wears out.");
	Items[32].time = 30;
	TestPerson(1, "Anne", "Bonny", "7");
	TestPerson(9, "Jack", "Rackham", "12");
	TestPerson(2, "Bartholomew", "de la Fontaine-Beaumarchais", "3");
	// The captain wears the timed amulet in her first artefact slot, 12 of its 30 days left.
	Characters[1].equip_item.slot1 = "amulet2";
	Characters[1].equip_item.slot1.time = 12;
	GameInterface.CHARACTERS_SCROLL.pic1.character = 1;
	GameInterface.CHARACTERS_SCROLL.pic1.str1 = "#Captain";
	GameInterface.CHARACTERS_SCROLL.pic2.character = 9;
	GameInterface.CHARACTERS_SCROLL.pic2.str1 = "#Navigator";
	// The window's rows: tr1 the rapier, tr2 the dagger, tr3 the potion, tr4 and tr5 the amulets.
	GameInterface.TABLE_ITEMS.tr1.index = 7;
	GameInterface.TABLE_ITEMS.tr2.index = 9;
	GameInterface.TABLE_ITEMS.tr3.index = 30;
	GameInterface.TABLE_ITEMS.tr4.index = 31;
	GameInterface.TABLE_ITEMS.tr5.index = 32;
}

// The sail emblem window's price of the work (SailsGerald.c's global there, set by the checks to the recorded state's)
// and the hero's ship (RealShips[1]) with the emblem on its sails.
int price;
object RealShips[2];

ref GetRealShip(int type)
{
	return &RealShips[type];
}

int GetCharacterShipType(ref chr)
{
	return sti(chr.Ship.Type);
}

// The ship interface of the recorded models (ui/tests/models/ship*.json): ship.c's xi_refCharacter, the captain whose
// ship it shows (Characters[3] Peter Marsh, a companion on a sloop, RealShips[0]), and iShipQty, what the hold had
// of the good whose overboard window is open (set by the checks to the recorded state's). A captain's gun ports per
// side are TestPorts.<side>.
#define SHIP_NOTUSED 1000
ref xi_refCharacter;
int iShipQty;

bool GetShipRemovable(ref chr)
{
	return true;
}

// Whether the game offers the exchange with chr's ship (a script fix's function); a case sets TestShipChange.
int TestShipChange = 1;
bool IsShipChangeOffered(ref chr)
{
	return TestShipChange;
}

int GetBortCannonsQtyMax(ref chr, string side)
{
	return sti(chr.TestPorts.(side));
}

void Main()
{
	Interfaces[0].IniFile = "RESOURCE\INI\INTERFACES\TavernWait.ini";
	Interfaces[1].IniFile = "RESOURCE\INI\INTERFACES\store.ini";
	Interfaces[2].IniFile = "RESOURCE\INI\INTERFACES\itemsbox.ini";
	Interfaces[3].IniFile = "RESOURCE\INI\INTERFACES\storage.ini";
	Interfaces[4].IniFile = "RESOURCE\INI\INTERFACES\NationRelation.ini";
	Interfaces[5].IniFile = "RESOURCE\INI\INTERFACES\contraband.ini";
	Interfaces[6].IniFile = "RESOURCE\INI\INTERFACES\cannons.ini";
	Interfaces[7].IniFile = "RESOURCE\INI\INTERFACES\itemstrade.ini";
	Interfaces[8].IniFile = "RESOURCE\INI\INTERFACES\hirecrew.ini";
	Interfaces[9].IniFile = "RESOURCE\INI\INTERFACES\SailsGerald.ini";
	Interfaces[10].IniFile = "RESOURCE\INI\INTERFACES\repair.ini";
	Interfaces[11].IniFile = "RESOURCE\INI\INTERFACES\map.ini";
	Interfaces[12].IniFile = "RESOURCE\INI\INTERFACES\DirSailEnc.ini";
	Interfaces[13].IniFile = "RESOURCE\INI\INTERFACES\LeaveBattle.ini";
	Interfaces[14].IniFile = "RESOURCE\INI\INTERFACES\salary.ini";
	Interfaces[15].IniFile = "RESOURCE\INI\INTERFACES\ColonyInfo.ini";
	Interfaces[16].IniFile = "RESOURCE\INI\INTERFACES\NationLegend.ini";
	Interfaces[17].IniFile = "RESOURCE\INI\INTERFACES\boal_frame.ini";
	Interfaces[18].IniFile = "RESOURCE\INI\INTERFACES\fortcapture.ini";
	Interfaces[19].IniFile = "RESOURCE\INI\INTERFACES\mapview.ini";
	Interfaces[20].IniFile = "RESOURCE\INI\INTERFACES\MapBest.ini";
	Interfaces[21].IniFile = "RESOURCE\INI\INTERFACES\PaperMap.ini";
	Interfaces[22].IniFile = "RESOURCE\INI\INTERFACES\tradebook.ini";
	Interfaces[23].IniFile = "RESOURCE\INI\INTERFACES\QuestBook.ini";
	Interfaces[24].IniFile = "RESOURCE\INI\INTERFACES\ColonyCapture.ini";
	Interfaces[25].IniFile = "RESOURCE\INI\INTERFACES\items.ini";
	Interfaces[26].IniFile = "RESOURCE\INI\INTERFACES\shipyard.ini";
	Interfaces[27].IniFile = "RESOURCE\INI\INTERFACES\boal_dice.ini";
	Interfaces[28].IniFile = "RESOURCE\INI\INTERFACES\boal_cards.ini";
	Interfaces[29].IniFile = "RESOURCE\INI\INTERFACES\transfer_main.ini";
	Interfaces[30].IniFile = "RESOURCE\INI\INTERFACES\ship.ini";
	Interfaces[31].IniFile = "RESOURCE\INI\INTERFACES\mainmenu.ini";
	Interfaces[32].IniFile = "RESOURCE\INI\INTERFACES\game_menu.ini";
	Interfaces[33].IniFile = "RESOURCE\INI\INTERFACES\quick_save.ini";
	Interfaces[34].IniFile = "RESOURCE\INI\INTERFACES\LSCchar_all.ini";
	Interfaces[35].IniFile = "RESOURCE\INI\INTERFACES\character.ini";
	// The goods of a trade window's rows: row n shows good n - 1.
	string row;
	for (int n = 1; n <= 30; n++)
	{
		row = "tr" + n;
		GameInterface.TABLE_LIST.(row).index = n - 1;
		row = "g" + (n - 1);
		TestGoods.(row).units = 10;
		TestGoods.(row).weight = 1;
		Goods[n - 1].name = "Good" + (n - 1);
		row = "Good" + (n - 1);
		TestStore.Goods.(row).TradeType = 0;
	}
	TestStore.Goods.Good4.TradeType = 4;
	TestStore.Goods.Good5.TradeType = 3;
	makeref(refStore, TestStore);
	Items[0].Weight = 0.1;
	Items[1].Weight = 3;
	Items[2].Weight = 2;
	Items[3].Weight = 0.5;
	for (int i = 4; i < 7; i++) Items[i].Weight = 0.0;
	Items[5].Weight = 0.45;
	for (int a = 0; a < 5; a++)
	{
		string score = "score" + a;
		TestNations.(score) = 5;
		for (int b = 0; b < 5; b++)
		{
			row = "r" + a + "_" + b;
			TestNations.(row) = RELATION_NEUTRAL;
			if (a == 4 || b == 4) TestNations.(row) = RELATION_ENEMY;
		}
	}
	TestNations.r1_3 = RELATION_FRIEND;
	GameInterface.strings.titul = "Reputation with the nations";
	TestNations.score2 = -40;
	TestNations.score1 = 35;
	// the crew hire of the recorded models (ui/tests/models/hirecrew*.json): the hero's ship of 40 men, a tavern of 25
	Colonies[0].id = "Elsewhere";
	Colonies[1].id = "Tortuga";
	makeref(loadedLocation, TestLocation);
	loadedLocation.fastreload = "Tortuga";
	TestCrew(&Characters[1], 40, 71, 34, 18, 62);
	TestCrew(&Characters[2], 12, 50, 50, 50, 50);
	TestCrew(&Colonies[1], 25, 22, 30, 12, 55);
	GameInterface.SHIPS_SCROLL.current = 0;
	GameInterface.SHIPS_SCROLL.pic1.companionIndex = 1;
	GameInterface.SHIPS_SCROLL.pic2.companionIndex = 2;
	NullCharacter.TavernBak.Hero.Quantity = 40;
	NullCharacter.TavernBak.Tavern.Quantity = 25;
	// the self repair of the recorded model (ui/tests/models/repair.json): 13 % of the hull ordered, 16 planks for it
	Characters[1].repair.hull = 13;
	Characters[1].repair.mast = 0;
	Characters[1].repair.sail = 0;
	Characters[1].repair.sailmax = 100;
	Characters[1].repair.preq = 16;
	Characters[1].repair.sreq = 0;
	TestHold.g0 = 40;
	TestHold.g1 = 0;
	TestItems();
	// the shipyard of the recorded models (ui/tests/models/shipyard-*.json): FillShipsScroll's places, our ketch
	// first (its captain Characters[1]), the other four free
	GameInterface.SHIPS_SCROLL.pic1.character = 1;
	GameInterface.SHIPS_SCROLL.pic1.str1 = "#Класс 5";
	GameInterface.SHIPS_SCROLL.pic1.str3 = "#11 575";
	for (n = 2; n <= 5; n++)
	{
		row = "pic" + n;
		GameInterface.SHIPS_SCROLL.(row).character = -1;
		GameInterface.SHIPS_SCROLL.(row).str1 = "#";
		GameInterface.SHIPS_SCROLL.(row).str2 = "";
	}
	// the table games of the recorded models (ui/tests/models/boal_*.json): the opponent, what the cards they show
	// count (a deck whose jack counts 3)
	TestPerson(11, "Jacques", "Dubois", "377");
	NullCharacter.Cards.c1.pic = "spades_A";
	NullCharacter.Cards.c1.count = 11;
	NullCharacter.Cards.c2.pic = "diamonds_10";
	NullCharacter.Cards.c2.count = 10;
	NullCharacter.Cards.c3.pic = "hearts_9";
	NullCharacter.Cards.c3.count = 9;
	NullCharacter.Cards.c4.pic = "hearts_J";
	NullCharacter.Cards.c4.count = 3;
	NullCharacter.Cards.c5.pic = "spades_7";
	NullCharacter.Cards.c5.count = 7;
	// the sail emblem of the recorded models (ui/tests/models/sailsgerald*.json)
	price = 1250;
	TestHero.Money = 12480;
	TestHero.Ship.Type = 1;
	RealShips[1].ShipSails.Gerald_Name = "emblem_01.tga.tx";
	// the ship interface of the recorded models
	TestHero.id = "Blaze";
	RealShips[0].BaseName = "Sloop";
	TestPerson(3, "Peter", "Marsh", "21");
	Characters[3].id = "Marsh";
	Characters[3].Ship.Type = 0;
	TestCrew(&Characters[3], 40, 62, 18, 30, 45);
	Characters[3].TestPorts.cannonf = 2;
	Characters[3].TestPorts.cannonb = 2;
	Characters[3].TestPorts.cannonl = 4;
	Characters[3].TestPorts.cannonr = 4;
	makeref(xi_refCharacter, Characters[3]);
	iShipQty = 220;
	TestCharacters();
	GameValue = 7;
	GameName = "kept";
}

void OnLoad()
{
}

// What the character adapter calls of the game: the company of the recorded models (ui/tests/models/hero*.json): the
// captain 1 Anne Bonny, the navigator 9 Jack Rackham, 14 Maximiliane on three posts, the fighter 12 Israel Hands, the
// companion 20 Edward Teach, the passenger 2, the prisoner 22 Charles Vane, the treasurer 13 Jean Bart off the list,
// the chooser's candidates 30 and 31; the card of 3 Claude Durand (character.json, xi_refCharacter); the perks of
// the personal and the ship group with their conditions; a skill's recorded steps (SkillTrace).
#define MAX_LOYALITY 35
object ChrPerksList;
object TestPassengers;

int GetMainCharacterIndex()
{
	return 1;
}

int GetMoneyForOfficerFull(ref chr)
{
	if (CheckAttribute(chr, "TestSalary")) return sti(chr.TestSalary);
	return 0;
}

string GetLoyalityName(int loyalty)
{
	return "Good";
}

int GetCharacterLoyality(ref chr)
{
	return 20;
}

int GetSkillValue(ref chr, string type, string skill)
{
	if (CheckAttribute(chr, "TestSkill." + skill)) return sti(chr.TestSkill.(skill));
	return 10;
}

string GetOfficerTypeByNum(int slot)
{
	if (slot == 1) return "navigator";
	if (slot == 2) return "boatswain";
	if (slot == 3) return "cannoner";
	if (slot == 4) return "doctor";
	if (slot == 5) return "treasurer";
	if (slot == 6) return "carpenter";
	if (slot >= 7 && slot <= 9) return "fighter";
	return "";
}

int GetPassengersQuantity(ref chr)
{
	return GetAttributesNum(&TestPassengers);
}

int GetPassenger(ref chr, int k)
{
	return sti(GetAttributeValue(GetAttributeN(&TestPassengers, k)));
}

void DelBakSkillAttr(ref chr)
{
}

int GetFreePoints_SelfRate(ref chr)
{
	return 1000;
}

int GetFreePoints_ShipRate(ref chr)
{
	return 1000;
}

bool CheckCharacterPerk(ref chr, string perk)
{
	return CheckAttribute(chr, "perks.list." + perk);
}

bool GetOfficersPerkUsing(ref chr, string perk)
{
	return perk == "Tireless";
}

string GetConvertStr(string key, string file)
{
	return key + " (" + file + ")";
}

// The value of a skill as the game computes it, its steps recorded while chr.SkillTrace exists.
int TestSkillSteps(ref chr, string id)
{
	int value = GetSkillValue(chr, "Skill", id);
	if (!CheckAttribute(chr, "SkillTrace")) return value;
	chr.SkillTrace.s0 = "base";
	chr.SkillTrace.s0.value = value;
	if (id == "Agility")
	{
		value++;
		chr.SkillTrace.s1 = "mangarosa";
		chr.SkillTrace.s1.value = value;
	}
	return value;
}

int GetCharacterSPECIALSimple(ref chr, string id)
{
	return TestSkillSteps(chr, id);
}

int GetCharacterSkillSimple(ref chr, string id)
{
	return TestSkillSteps(chr, id);
}

int GetSummonSkillFromNameSimple(ref chr, string id)
{
	int value = GetSkillValue(chr, "Skill", id);
	if (id == "Commerce" && sti(chr.index) == 1) return value + 2; // the treasurer's higher skill
	return value;
}

int ApplayNavyPenaltyToSkill(ref chr, string id, int value)
{
	if (id == "Commerce" && sti(chr.index) == 1) return value - 2;
	return value;
}

int ApplayNavyPenalty(ref chr, string id, int value)
{
	return value;
}

int GetCharacterShipClass(ref chr)
{
	return 3;
}

int GetShipClassNavySkill(int shipClass)
{
	return 65;
}

string GetCharacterEquipSuitID(ref chr)
{
	return "";
}

int FindItem(string id)
{
	return GetItemIndex(id);
}

int GetHealthNum(ref chr)
{
	return 6;
}

void TestPerk(string id, string type, string needs, string also)
{
	ChrPerksList.list.(id).BaseType = type;
	if (needs != "") ChrPerksList.list.(id).condition.(needs) = true;
	if (also != "") ChrPerksList.list.(id).condition.(also) = true;
}

void TestOfficer(int index, string name, string lastname, string face, int rank, int salary)
{
	TestPerson(index, name, lastname, face);
	Characters[index].rank = rank;
	Characters[index].TestSalary = salary;
}

void TestSlot(int slot, int character, string caption)
{
	string pic = "pic" + (slot + 1);
	GameInterface.CHARACTERS_SCROLL.(pic).character = character;
	GameInterface.CHARACTERS_SCROLL.(pic).str1 = "#" + caption;
}

void TestCharacters()
{
	TestPerk("BasicDefense", "self", "", "");
	TestPerk("AdvancedDefense", "self", "BasicDefense", "");
	TestPerk("CriticalHit", "self", "", "");
	TestPerk("Tireless", "self", "", "");
	TestPerk("SwordplayProfessional", "self", "CriticalHit", "AdvancedDefense");
	ChrPerksList.list.SwordplayProfessional.condition.Tireless = true;
	TestPerk("IronWill", "self", "", "");
	TestPerk("Trustworthy", "self", "AdvancedCommerce", "IronWill");
	TestPerk("BasicCommerce", "ship", "", "");
	TestPerk("AdvancedCommerce", "ship", "BasicCommerce", "");
	Characters[1].rank = 4;
	Characters[1].reputation.authority = 63;
	Characters[1].perks.FreePoints_self = 1;
	Characters[1].perks.FreePoints_ship = 0;
	Characters[1].perks.FreePoints_self_exp = 340;
	Characters[1].perks.FreePoints_ship_exp = 0;
	Characters[1].TestSkill.Agility = 7;
	Characters[1].TestSkill.Commerce = 5;
	Characters[1].TestSkill.Sailing = 12;
	TestOfficer(9, "Jack", "Rackham", "12", 5, 1500);
	Characters[9].TestSkill.Sailing = 34;
	TestOfficer(14, "Maximiliane", "von Hohenzollern-Sigmaringen", "30", 14, 1500);
	TestOfficer(12, "Israel", "Hands", "14", 5, 1500);
	TestOfficer(20, "Edward", "Teach", "41", 9, 0);
	TestOfficer(22, "Charles", "Vane", "25", 7, 0);
	TestOfficer(13, "Jean", "Bart", "16", 6, 1100);
	Characters[13].TestSkill.Commerce = 7;
	TestOfficer(30, "Mary", "Read", "15", 6, 1200);
	Characters[30].TestSkill.Grappling = 14;
	TestOfficer(31, "Calico", "Jack", "19", 5, 1000);
	Characters[31].TestSkill.Grappling = 19;
	Characters[2].rank = 3;
	TestOfficer(3, "Клод", "Дюран", "21", 9, 2350);
	Characters[3].perks.FreePoints_self = 1;
	Characters[3].perks.FreePoints_ship = 0;
	Characters[3].perks.FreePoints_self_exp = 120;
	Characters[3].perks.list.BasicDefense = true;
	Characters[3].perks.list.IronWill = true;
	Characters[3].perks.list.BasicCommerce = true;
	makeref(xi_refCharacter, Characters[3]);
	TestSlot(0, 1, "Captain");
	TestSlot(1, 9, "Navigator");
	TestSlot(2, 0, "Boatswain");
	TestSlot(3, 14, "Cannoner");
	TestSlot(4, 0, "Doctor");
	TestSlot(5, 14, "Treasurer");
	TestSlot(6, 14, "Carpenter");
	TestSlot(7, 12, "Fighter");
	TestSlot(8, 0, "Fighter");
	TestSlot(9, 0, "Fighter");
	TestSlot(10, 20, "Companion");
	TestSlot(11, 2, "Passenger");
	TestSlot(12, 22, "Prisoner");
	GameInterface.CHARACTERS_SCROLL.current = 2;
	GameInterface.PASSENGERSLIST.pic1.character = 30;
	GameInterface.PASSENGERSLIST.pic2.character = 31;
	TestPassengers.p0 = 2;
	TestHero.Fellows.Passengers.treasurer = 13;
}
