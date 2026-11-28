// SPDX-License-Identifier: GPL-3.0-only
// The sweep's main menu call for a menu whose buttons are interface nodes (mainmenu.c ProcessCommandExecute).

void SceneSweep_NewGame()
{
	Event("ievnt_command", "ss", "click", "NEW_GAME");
}
