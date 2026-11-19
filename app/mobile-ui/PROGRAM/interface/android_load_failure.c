// SPDX-License-Identifier: GPL-3.0-only
// The engine's load watchdog (core load_watch.hpp) fires AndroidLoadFailed(record, reason)
// when a load the player asked for was refused or did not start in time: save_load.c
// ProcessLoad has closed the interface by then, and seadogs.c LoadGame may have dropped the
// load or torn the game down. The engine has logged the reason and the portable UI tells the
// player which record did not load; this brings the main menu back as save_load.c
// ProcessCancelExit does. A game that is still running (the load was dropped before anything
// was torn down) simply goes on.
#event_handler("AndroidLoadFailed", "AndroidLoadFailed");

void AndroidLoadFailed()
{
	string record = GetEventData();
	string reason = GetEventData();
	Trace("Load failed: " + record + ": " + reason);
	ReloadProgressEnd();
	// LoadGame got past its description guard (the engine refuses a record it cannot load
	// there, so only a record that broke in between gets here): DeleteEntities has run, the
	// world is gone, and the menu cannot be rebuilt in place (tried: building its scene then
	// aborted on a pure virtual call in InterfaceBackScene::ProcessMessage). The game ends;
	// the application's launcher starts it again.
	if (IsEntity(&reload_fader))
	{
		Trace("Load failed: the game was torn down; it ends, the launcher starts it again");
		ExitProgram();
		return;
	}
	if (bSeaActive || IsEntity(&worldMap) || FindLoadedLocation() != -1)
	{
		Trace("Load failed: the game in progress goes on");
		return;
	}
	Trace("Load failed: back to the main menu");
	ReturnToMainMenu();
}
