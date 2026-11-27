// SPDX-License-Identifier: GPL-3.0-only
// Authored presentation adapter for the native land command tree. No dispatch or game rules here.
#event_handler("AndroidLandRefresh", "AndroidLandRefresh");
void AndroidLandRefresh()
{
	DeleteAttribute(&objLandInterface, "AndroidLand");
	string pending = GetEventData();
	BLI_SetPossibleCommands();
	int pendingAllowed = true;
	if (pending != "")
	{
		pendingAllowed = false;
		aref commands, command;
		makearef(commands, objLandInterface.Commands);
		for (int ci = 0; ci < GetAttributesNum(commands); ci++)
		{
			command = GetAttributeN(commands, ci);
			if (CheckAttribute(command, "event") && command.event == pending)
				pendingAllowed = sti(command.enable);
		}
		Event("BI_CommandEndChecking", "s", pending);
	}
	// SendMessage returns the interpreter's pointer-sized value. As in the
	// stock command handler, convert through int before writing string attributes.
	int dialogTarget = SendMessage(pchar,"ls",MSG_CHARACTER_EX_MSG,"FindDialogCharacter");
	int officerTarget = LAi_FindNearestVisCharacter(pchar, 1);
	int bodyTarget = Dead_FindCloseBody();
	string reloadTarget = chrWaitReloadLocator;
	// The place of the town's fast-travel table the hero stands in: the game leaves it out of the
	// travel list (CheckFastJump), the touch HUD shows its tile marked. -1: in no place of the table.
	int herePicture = -1;
	int hereTexture = -1;
	string hereLabel = "";
	int hereLocation = FindLoadedLocation();
	if (hereLocation >= 0 && CheckAttribute(&Locations[hereLocation], "fastreload"))
	{
		string hereTown = Locations[hereLocation].fastreload;
		if (CheckAttribute(&objFastReloadTable, "table." + hereTown))
		{
			aref places, place;
			makearef(places, objFastReloadTable.table.(hereTown));
			for (int pi = 0; pi < GetAttributesNum(places); pi++)
			{
				place = GetAttributeN(places, pi);
				if (CheckAttribute(place, "location") && place.location == Locations[hereLocation].id)
				{
					herePicture = sti(place.pic);
					hereTexture = 1 + sti(place.tex);
					hereLabel = GetNodeForIcon(hereTexture, herePicture);
				}
			}
		}
	}
	aref icons, icon, item;
	makearef(icons, objLandInterface.UserIcons);
	for (int ui = 0; ui < GetAttributesNum(icons); ui++)
	{
		icon = GetAttributeN(icons, ui);
		if (CheckAttribute(icon, "potion") && Items_FindItem(icon.potion, &item) >= 0)
			icon.note = GetConvertStr(item.name, "ItemsDescribe.txt");
	}
	objLandInterface.AndroidLand.dialog = dialogTarget;
	objLandInterface.AndroidLand.officer = officerTarget;
	objLandInterface.AndroidLand.body = bodyTarget;
	objLandInterface.AndroidLand.reload = reloadTarget;
	objLandInterface.AndroidLand.pendingAllowed = pendingAllowed;
	objLandInterface.AndroidLand.herePicture = herePicture;
	objLandInterface.AndroidLand.hereTexture = hereTexture;
	objLandInterface.AndroidLand.hereLabel = hereLabel;
	// Completion is last. Native also rejects any script error in this event.
	objLandInterface.AndroidLand.version = 1;
}
