// SPDX-License-Identifier: GPL-3.0-only
// Authored adapter of the touch HUD's portrait and squadron taps and of event lines that lead
// to a screen (engine shortcuts characterscreen / shipscreen / journalscreen / itemsscreen,
// platform/android_shortcuts.hpp): opens the character, ship, journal or inventory screen itself,
// whatever section F2 would reopen. It presses F2 (the "Interface" control: ProcessControls and
// ProcessInterfaceKey refuse it in a dialogue, under water, for a dead hero, in fight mode, with an open
// command menu, a locked menu or in a boarding fight), with the screen named the way the screens name
// themselves for F2 (InterfaceStack.SelectMenu_node): the game has no other way to open one screen with
// F2's refusals. A refused opening puts the player's previous F2 screen back.
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
	int had = CheckAttribute(&InterfaceStack, "SelectMenu_node");
	string previous = "";
	if (had) previous = InterfaceStack.SelectMenu_node;
	InterfaceStack.SelectMenu_node = node;
	Event("Control Activation", "s", "Interface");
	if (sti(InterfaceStates.Launched)) return;
	if (had) InterfaceStack.SelectMenu_node = previous;
	else DeleteAttribute(&InterfaceStack, "SelectMenu_node");
}
