// SPDX-License-Identifier: GPL-3.0-only
// The sweep's main menu call for a menu that takes its commands by name (mainmenu.c ProcCommand).

void SceneSweep_NewGame()
{
	Event("backgroundcommand", "s", "New");
}
