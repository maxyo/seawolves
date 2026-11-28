// A later game of the family as the touch interface's scripts meet it: its windows are its own buttons (the title's
// commands are nodes of its window). Only what the common readers (lineages/legacy/lib.c) and the set's adapters
// require of the game, added with them.
object Interfaces[6];
object GameInterface;
object Characters[1];

#event_handler("GameTick", "GameTick");

void GameTick()
{
}

void Main()
{
	Interfaces[0].IniFile = "RESOURCE\INI\INTERFACES\mainmenu.ini";
	Interfaces[1].IniFile = "RESOURCE\INI\INTERFACES\selectCharacter.ini";
	Interfaces[2].IniFile = "RESOURCE\INI\INTERFACES\save_load.ini";
	Interfaces[3].IniFile = "RESOURCE\INI\INTERFACES\option_screen.ini";
	Interfaces[4].IniFile = "RESOURCE\INI\INTERFACES\tutorial.ini";
	Interfaces[5].IniFile = "RESOURCE\INI\INTERFACES\message.ini";
}

ref GetCharacter(int index)
{
	return &Characters[index];
}

string GetFullName(ref chr)
{
	return chr.name;
}
