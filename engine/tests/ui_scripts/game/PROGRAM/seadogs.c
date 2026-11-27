// A game's program as the touch interface's scripts meet one: an interface table filled by Main, globals, functions
// and an event handler.
object Interfaces[3];
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

void Main()
{
	Interfaces[0].IniFile = "RESOURCE\INI\INTERFACES\TavernWait.ini";
	Interfaces[1].IniFile = "RESOURCE\INI\INTERFACES\store.ini";
	Interfaces[2].IniFile = "RESOURCE\INI\INTERFACES\itemsbox.ini";
	GameValue = 7;
	GameName = "kept";
}

void OnLoad()
{
}
