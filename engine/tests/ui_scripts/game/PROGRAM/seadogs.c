// A game's program as the touch interface's scripts meet one: an interface table filled by Main, globals, functions
// and an event handler.
object Interfaces[9];
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

// The game function the trade adapter calls for a storage row's pack weight: TestGoods.g<good>.{units (pieces in a
// pack), weight (cwt a pack)}, every good packs of 10 of 1 cwt. Each started pack counts whole.
object TestGoods;

int GetGoodWeightByType(int good, int quantity)
{
	string slot = "g" + good;
	int units = sti(TestGoods.(slot).units);
	return (quantity + units - 1) / units * sti(TestGoods.(slot).weight);
}

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
object Characters[3];
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
	// The goods of a trade window's rows: row n shows good n - 1.
	string row;
	for (int n = 1; n <= 30; n++)
	{
		row = "tr" + n;
		GameInterface.TABLE_LIST.(row).index = n - 1;
		row = "g" + (n - 1);
		TestGoods.(row).units = 10;
		TestGoods.(row).weight = 1;
	}
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
	GameValue = 7;
	GameName = "kept";
}

void OnLoad()
{
}
