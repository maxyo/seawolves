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

void BadBuild()
{
	aref ui = GetEventData();
	int two[2];
	int at = 5;
	if (ui.capability == "fault") two[at] = 1; // a runtime error: the interpreter logs it and goes on
	if (ui.capability != "bad") return;
	BadView.nope = 1;
	UiShow(&BadView);
}
