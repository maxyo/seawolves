// SPDX-License-Identifier: GPL-3.0-only
// Authored adapter of the touch HUD's portrait and squadron taps and of event lines that lead
// to a screen (engine shortcuts characterscreen / shipscreen / journalscreen / itemsscreen,
// platform/android_shortcuts.hpp): opens the character, ship, journal or inventory screen itself,
// whatever section F2 would reopen. It goes through the F2 key's own path
// (ProcessInterfaceKey: dead hero, fight mode, an open command menu, a locked menu, a boarding
// fight refuse it), with the screen named the way the screens name themselves for F2
// (InterfaceStack.SelectMenu_node); a refused opening keeps the player's previous F2 screen.
// The UI selects the tapped person or ship, the quest or the tab in the opened screen. No game
// rules here.
#event_handler("AndroidOpenScreen", "AndroidOpenScreen");
void AndroidOpenScreen()
{
	string screen = GetEventData();
	string node = "";
	if (screen == "character") node = "LaunchMainCharacter";
	if (screen == "ship") node = "LaunchShipState";
	if (screen == "journal") node = "LaunchQuestBook";
	if (screen == "items") node = "LaunchItems";
	if (node == "")
	{
		trace("AndroidOpenScreen: unknown screen " + screen);
		return;
	}
	// ProcessControls refuses F2 on an underwater walk before ProcessInterfaceKey.
	if (!bSeaActive && !IsEntity(&worldMap) && CheckAttribute(&loadedLocation, "type") && loadedLocation.type == "underwater") return;
	int had = CheckAttribute(&InterfaceStack, "SelectMenu_node");
	string previous = "";
	if (had) previous = InterfaceStack.SelectMenu_node;
	InterfaceStack.SelectMenu_node = node;
	ProcessInterfaceKey();
	if (sti(InterfaceStates.Launched)) return;
	if (had) InterfaceStack.SelectMenu_node = previous;
	else DeleteAttribute(&InterfaceStack, "SelectMenu_node");
}
