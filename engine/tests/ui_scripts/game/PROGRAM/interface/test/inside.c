// The touch interface's natives and screen events from inside a running script.
#libriary "UiScriptsTest"

native int TestBuildInside();

int ShowOutside()
{
	UiShow(&UiModel);
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
	if (GetEventData() != "bad") return;
	BadView.nope = 1;
	UiShow(&BadView);
}
