// A game's program as the touch interface's scripts meet one: an interface table filled by Main, globals, functions
// and an event handler.
object Interfaces[5];
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

void Main()
{
	Interfaces[0].IniFile = "RESOURCE\INI\INTERFACES\TavernWait.ini";
	Interfaces[1].IniFile = "RESOURCE\INI\INTERFACES\store.ini";
	Interfaces[2].IniFile = "RESOURCE\INI\INTERFACES\itemsbox.ini";
	Interfaces[3].IniFile = "RESOURCE\INI\INTERFACES\storage.ini";
	Interfaces[4].IniFile = "RESOURCE\INI\INTERFACES\NationRelation.ini";
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
	GameValue = 7;
	GameName = "kept";
}

void OnLoad()
{
}
