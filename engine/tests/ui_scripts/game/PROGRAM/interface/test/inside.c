// The touch interface's natives and screen events from inside a running script.
#libriary "UiScriptsTest"

native int TestBuildInside();

int ShowOutside()
{
	UiShow(&UiScreens);
	return 1;
}

int BuildInside()
{
	return TestBuildInside();
}

#event_handler("UiBuild", "BadBuild");

object BadView;
// What UiGameInt read of the game: through a global, an object's attribute and a reference's.
int InsideRead;
ref InsideRef;
// What UiGameText read: a string global, an attribute's text and an attribute holding an empty value.
string InsideText;

void BadBuild()
{
	aref ui = GetEventData();
	int two[2];
	int at = 5;
	object asked = {which = "save"};
	if (ui.capability == "command") UiCommand("nosuch", &asked);
	asked.extra = 1;
	if (ui.capability == "commandfield") UiCommand("quick", &asked);
	if (ui.capability == "fault") two[at] = 1; // a runtime error: the interpreter logs it and goes on
	if (ui.capability == "gameint")
	{
		makeref(InsideRef, GameInterface);
		InsideRead = UiGameInt("iHourWait") * 100 + UiGameInt("InsideRef.TABLE_LIST.tr2.index") * 10 +
		             UiGameInt("GameInterface.TABLE_LIST.tr3.index");
	}
	if (ui.capability == "gametexts")
	{
		makeref(InsideRef, GameInterface);
		GameInterface.TABLE_LIST.blank = "";
		InsideText = UiGameText("GameName") + "|" + UiGameText("InsideRef.TABLE_LIST.tr2.index") + "|" +
		             UiGameText("GameInterface.TABLE_LIST.blank") + "|";
	}
	if (ui.capability == "textint") UiGameText("iHourWait");
	if (ui.capability == "textpath") UiGameText("GameInterface.TABLE_LIST.nothing");
	if (ui.capability == "nogame")
	{
		UiGameInt("NoSuchGlobal");
		InsideRead = -1; // never runs: the build stops at the native that failed
	}
	if (ui.capability == "gametext") UiGameInt("GameName");
	if (ui.capability == "gamepath") UiGameInt("GameInterface.TABLE_LIST.nothing");
	if (ui.capability != "bad") return;
	BadView.nope = 1;
	UiShow(&BadView);
}
