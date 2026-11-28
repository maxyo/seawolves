// A later game of the family as the touch interface's scripts meet it: its windows are its own buttons (the title's
// commands are nodes of its window). Only what the common readers (lineages/legacy/lib.c) and the set's adapters
// require of the game, added with them.
object Interfaces[4];
object GameInterface;
object Characters[1];

#event_handler("GameTick", "GameTick");

void GameTick()
{
}

void Main()
{
	Interfaces[0].IniFile = "RESOURCE\INI\INTERFACES\mainmenu.ini";
}

ref GetCharacter(int index)
{
	return &Characters[index];
}

string GetFullName(ref chr)
{
	return chr.name;
}
