// SPDX-License-Identifier: GPL-3.0-only
// Autosaves. Three records per profile, «Автосохранение 1..3», written in rotation: every
// autosave replaces the one written longest ago. The engine chooses the record and keeps
// these names apart from the player's own records, so an autosave never replaces a save the
// player made.
//
// When the game autosaves:
// - the application leaves the foreground (HOME, recents, screen off): a suspended app may
//   be killed without notice, so it is written at once, inside AppWillEnterBackground (no
//   frame runs before the thread is suspended);
// - the hero lands from the ship, once the land location is on screen, unless the player
//   turned «Автосохранение» off;
// - the game's own autosave before a boarding (the script patch `autosave-rotation` gives it
//   the rotation's record).
//
// It saves only where the game's own quick save is allowed and writes with the game's
// SaveGame. Every autosave and every skipped one is a line in compile.log.
#event_handler("AppWillEnterBackground", "AndroidAutosaveBackground");
// The sea is entered (sea.c SeaLogin) or a save made at sea is loaded (sea.c Sea_Load).
#event_handler("EventSeaLogin", "AndroidAutosaveAtSea");
#event_handler("Sea_FirstInit", "AndroidAutosaveAtSea");
#event_handler("EventLoadLocation", "AndroidAutosaveLocationLoaded");
#event_handler("AndroidAutosaveLanding", "AndroidAutosaveLanding");

// MSG_INTERFACE_AUTOSAVE_RECORD (storm engine shared/interface/messages.h): "lse" profile,
// out name of the record the rotation writes next.
#define ANDROID_MSG_AUTOSAVE_RECORD 45301
// A landing waits for the location to appear (the loading screen, a scene that starts with
// a conversation): asked every half second, given up after a minute.
#define ANDROID_AUTOSAVE_LANDING_STEP 500
#define ANDROID_AUTOSAVE_LANDING_TRIES 120

// What is remembered lives in the hero's attributes, so it is saved with the game and this
// segment needs no global variables (it is loaded after a save's variables are read):
// pchar.AndroidAutosave.sea      the ship is at sea; the next location the ship lies at is a landing
// pchar.AndroidAutosave.landing  the number of the landing whose autosave is waited for

string AndroidAutosaveBlocker()
{
	if (bPlayVideoNow) return "video";
	if (dialogRun) return "dialogue";
	aref arTmp;
	if (GetEntity(arTmp, "fader")) return "loading";
	if (InterfaceStates.Launched != 0) return "interface open";
	// A save of the player or of the game waits one frame for its screenshot: do not run inside it.
	if (GetEntity(arTmp, "scrshoter")) return "another save in progress";
	if (!CheckSaveGameEnabled()) return "the game disallows saving here";
	if (!QuickSaveGameEnabledHardcore()) return "hardcore game: saving only in churches";
	return "";
}

// The record this autosave writes: the rotation's next one, as the game names a save
// ("<file>" in SAVE\<profile>).
string AndroidAutosaveRecord()
{
	string record = "";
	SendMessage(&GameInterface, "lse", ANDROID_MSG_AUTOSAVE_RECORD, PlayerProfile.name, &record);
	return record;
}

void AndroidAutosaveWrite(string cause)
{
	string saveName = "SAVE\" + PlayerProfile.name + "\" + AndroidAutosaveRecord();
	string saveData = GetSaveDataString(GetCurLocationName());
	// The engine takes the thumbnail from the last rendered frame when SaveGame asks for it.
	CreateScreenShoter();
	// Flag 0: the handler must be live now, not from the next frame.
	SetEventHandler("evntSave", "SaveGame", 0);
	Event("evntSave", "ss", saveName, saveData);
	Trace("Autosave (" + cause + "): " + saveName);
}

void AndroidAutosaveBackground()
{
	string blocker = AndroidAutosaveBlocker();
	if (blocker != "")
	{
		Trace("Autosave (background) skipped: " + blocker);
		return;
	}
	AndroidAutosaveWrite("background");
}

void AndroidAutosaveAtSea()
{
	pchar.AndroidAutosave.sea = 1;
}

// locations_loader.c LoadLocation posts EventLoadLocation for every location. A landing is
// the first location after the sea that is the place the ship lies at
// (pchar.location.from_sea): not the cabin or a deck, not a boarding.
void AndroidAutosaveLocationLoaded()
{
	if (!CheckAttribute(pchar, "AndroidAutosave.sea") || sti(pchar.AndroidAutosave.sea) == 0) return;
	if (!CheckAttribute(&loadedLocation, "id")) return;
	// Every location entered from the sea (landing, cabin, deck) with the place the game thinks
	// the ship lies at: quests and dialogs test pchar.location.from_sea (e.g. "LeFransua_port").
	string fromSea = "(absent)";
	if (CheckAttribute(pchar, "location.from_sea")) fromSea = "'" + pchar.location.from_sea + "'";
	Trace("Location after the sea: " + loadedLocation.id + ", pchar.location.from_sea = " + fromSea);
	if (!CheckAttribute(pchar, "location.from_sea") || loadedLocation.id != pchar.location.from_sea) return;
	pchar.AndroidAutosave.sea = 0;
	if (CheckAttribute(&InterfaceStates, "EnabledAutoSaveMode") && sti(InterfaceStates.EnabledAutoSaveMode) == 0)
	{
		Trace("Autosave (landing at " + loadedLocation.id + ") skipped: autosave is off in the settings");
		return;
	}
	int landing = 1;
	if (CheckAttribute(pchar, "AndroidAutosave.landing")) landing = sti(pchar.AndroidAutosave.landing) + 1;
	pchar.AndroidAutosave.landing = landing;
	PostEvent("AndroidAutosaveLanding", ANDROID_AUTOSAVE_LANDING_STEP, "sll", loadedLocation.id, landing, ANDROID_AUTOSAVE_LANDING_TRIES);
}

void AndroidAutosaveLanding()
{
	string location = GetEventData();
	int landing = GetEventData();
	int tries = GetEventData();
	// A later landing has its own wait.
	if (!CheckAttribute(pchar, "AndroidAutosave.landing") || sti(pchar.AndroidAutosave.landing) != landing) return;
	string cause = "landing at " + location;
	if (bSeaActive || !CheckAttribute(&loadedLocation, "id") || loadedLocation.id != location)
	{
		Trace("Autosave (" + cause + ") skipped: the hero left before the game could be saved");
		return;
	}
	string blocker = AndroidAutosaveBlocker();
	if (blocker == "")
	{
		AndroidAutosaveWrite(cause);
		return;
	}
	if (tries <= 1)
	{
		Trace("Autosave (" + cause + ") skipped: " + blocker);
		return;
	}
	PostEvent("AndroidAutosaveLanding", ANDROID_AUTOSAVE_LANDING_STEP, "sll", location, landing, tries - 1);
}
