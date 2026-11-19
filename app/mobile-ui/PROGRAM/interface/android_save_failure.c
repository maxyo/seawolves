// SPDX-License-Identifier: GPL-3.0-only
// The engine fires AndroidSaveFailed(record, reason) one frame after a save could not be
// written (core CoreImpl::ReportFailedSave): the previous record is unchanged, the engine has
// logged the reason and the portable UI tells the player. A save of the player was made from
// the save screen, which save_load.c closed before seadogs.c SaveGame ran: it comes back, so
// the player can free space or try again. Autosaves (the rotation's «Автосохранение N», the
// stock boarding autosave) and quick saves are only told.
#event_handler("AndroidSaveFailed", "AndroidSaveFailed");

bool AndroidSaveOfThePlayer(string record)
{
	if (findsubstr(record, "\Автосохранение ", 0) != -1) return false;
	if (findsubstr(record, " AutoSave", 0) != -1) return false;
	if (findsubstr(record, " QuickSave ", 0) != -1) return false;
	return true;
}

void AndroidSaveFailed()
{
	string record = GetEventData();
	string reason = GetEventData();
	Trace("Save failed: " + record + ": " + reason);
	if (!AndroidSaveOfThePlayer(record)) return;
	if (InterfaceStates.Launched != 0)
	{
		Trace("Save failed: another interface is open; the save screen is not brought back");
		return;
	}
	Trace("Save failed: the save screen comes back");
	LaunchSaveGame();
}
