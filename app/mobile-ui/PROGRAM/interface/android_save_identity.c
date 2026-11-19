// SPDX-License-Identifier: GPL-3.0-only
// What the engine's menus need about the game in progress: the hero and the location that
// name a record («<hero> · <location>»), the play time of the record about to be written
// and the in-game time and date ("HH:MM  DD.MM.YYYY", as in a save's description). The
// engine stores hero and location in every save's description, shows them for the new
// record and on the pause menu (storm engine xinterface mobile_save_list.hpp). Uses the
// game's own GetFullName (quests.c), GetCurLocationName, GetPlayTime, GetStringTime and
// GetStringDate (interface.c).
#event_handler("AndroidSaveIdentity", "AndroidSaveIdentity");

string AndroidSaveIdentity()
{
	if (nMainCharacterIndex < 0) return "";
	return GetFullName(pchar) + "@" + GetCurLocationName() + "@" + GetPlayTime() + "@" +
		GetStringTime(GetTime()) + "  " + GetStringDate(GetDataDay(), GetDataMonth(), GetDataYear());
}
