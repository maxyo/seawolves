// A game's program as the touch interface's scripts meet one: an interface table filled by Main, globals, functions
// and an event handler.
object Interfaces[4];
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

// Every good in packs of 10 pieces, a cwt a pack, each started pack counted whole.
int GetGoodWeightByType(int good, int quantity)
{
	return (quantity + 9) / 10;
}

void Main()
{
	Interfaces[0].IniFile = "RESOURCE\INI\INTERFACES\TavernWait.ini";
	Interfaces[1].IniFile = "RESOURCE\INI\INTERFACES\store.ini";
	Interfaces[2].IniFile = "RESOURCE\INI\INTERFACES\itemsbox.ini";
	Interfaces[3].IniFile = "RESOURCE\INI\INTERFACES\storage.ini";
	// The goods of a trade window's rows: row n shows good n - 1.
	string row;
	for (int n = 1; n <= 30; n++)
	{
		row = "tr" + n;
		GameInterface.TABLE_LIST.(row).index = n - 1;
	}
	GameValue = 7;
	GameName = "kept";
}

void OnLoad()
{
}
