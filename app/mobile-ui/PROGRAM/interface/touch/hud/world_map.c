// SPDX-License-Identifier: GPL-3.0-only
// The world map's HUD: the squadron, the compass (a tap shows its details beside it), the helm, the sail lever, the
// camera, the time steps, the menu and the map's commands (going on, the fight, the storm). The compass, the clock,
// the sky and the lever move with the map every frame: they are readings the runtime lays in, not built here.

#event_handler("UiBuild", "TouchHudWorldMap_Build");
#event_handler("UiTap", "TouchHudWorldMap_Tap");

object TouchHudWorldMap;      // the scene's own state: details (the compass's details shown), next (the time step a
                              // tap asks for), slot.<element> (the command drawn there)
object TouchHudWorldMap_Play; // the gameplay
object TouchHudWorldMap_Map;  // the world map
object TouchHudWorldMap_View;

string TouchHudWorldMap_Flags[5] = {"holland", "england", "france", "spain", "pirates"};

// Where each of the engine's map commands sits, as `out` {element, icon, variant}: going on (to sea, a port, a ship)
// in the corner, the fight beside it, the storm further in; a command the HUD has no picture for (a mod's) takes the
// corner with a plain icon.
void TouchHudWorldMap_Slot(string id, aref out)
{
	out.element = "context";
	out.variant = "primary";
	out.icon = "actions";
	if (id == "EnterToAttack" || id == "EnterToEnemy") { out.element = "attack"; out.icon = "swords"; out.variant = "combat"; }
	if (id == "EnterToStorm") { out.element = "storm"; out.icon = "wind"; out.variant = "normal"; }
	if (id == "EnterToIsland") out.icon = "port";
	if (id == "EnterToShip") out.icon = "ship";
	if (id == "EnterToSea") out.icon = "sea-exit";
}

// The squadron as the map's ship signs, the flagship first: each picture ringed by its hull, crew and sails.
void TouchHudWorldMap_Fleet(aref view, aref map)
{
	if (!sti(map.shipsVisible)) return;
	int n = TouchLib_Count(map, "fleet");
	if (n == 0) return;
	aref ships, ship, cell, bars, bar;
	makearef(ships, view.elements.fleet.ships);
	for (int i = 0; i < n; i++)
	{
		ship = TouchLib_Item(map, "fleet", i);
		cell = TouchLib_Add(ships, "s");
		cell.name = ship.name;
		cell.texture = ship.texture;
		cell.uv = ship.uv;
		cell.tap = true;
		makearef(bars, cell.bars);
		if (sti(ship.hull) >= 0) { bar = TouchLib_Add(bars, "b"); bar.kind = "health"; bar.value = ship.hull; }
		if (sti(ship.crew) >= 0) { bar = TouchLib_Add(bars, "b"); bar.kind = "crew"; bar.value = ship.crew; }
		if (sti(ship.sails) >= 0) { bar = TouchLib_Add(bars, "b"); bar.kind = "sails"; bar.value = ship.sails; }
	}
	UiOn("fleet", "ship");
}

// One line of the compass's details.
aref TouchHudWorldMap_Row(aref rows, string key)
{
	aref row = TouchLib_Add(rows, "r");
	row.k = UiText(key);
	return row;
}

// The readings the compass draws as pictures, in words.
void TouchHudWorldMap_Details(aref view, aref map)
{
	view.elements.details = "";
	aref rows, row, wind;
	makearef(rows, view.elements.("detail-rows").items);
	string days = " " + UiText("common.days_short");
	row = TouchHudWorldMap_Row(rows, "hud.map.course");
	TouchLib_Reading(row, "v", "worldMap.course", "degreesText");
	row = TouchHudWorldMap_Row(rows, "hud.map.wind");
	wind = TouchLib_Reading(row, "v", "worldMap.windFrom worldMap.wind", "point percentText");
	wind.fill = UiText("hud.map.windFrom") + " · {2}";
	int storm = sti(map.storm);
	if (storm > 0)
	{
		row = TouchHudWorldMap_Row(rows, "hud.map.weather");
		if (storm == 2) row.v = UiText("hud.map.weather.tornado"); else row.v = UiText("hud.map.weather.storm");
	}
	if (sti(map.foodDays) >= 0) { row = TouchHudWorldMap_Row(rows, "hud.map.food"); row.v = map.foodDays + days; }
	if (sti(map.rumDays) >= 0) { row = TouchHudWorldMap_Row(rows, "hud.map.rum"); row.v = map.rumDays + days; }
	if (sti(map.morale) >= 0) { row = TouchHudWorldMap_Row(rows, "hud.map.morale"); row.v = map.morale + "%"; }
	int flag = sti(map.flag);
	if (flag >= 0)
	{
		row = TouchHudWorldMap_Row(rows, "hud.map.flag");
		row.v = UiText("hud.map.flag." + TouchHudWorldMap_Flags[flag]);
	}
	// The engine writes dashes while the fleet has no sextant.
	row = TouchHudWorldMap_Row(rows, "hud.map.coordinates");
	row.v = map.coordinates;
	if (map.coordinates == "" || findSubStr(map.coordinates, "--", 0) >= 0) row.v = UiText("hud.map.noSextant");
}

void TouchHudWorldMap_Compass(aref view, aref map)
{
	if (!sti(map.instruments) || !CheckAttribute(map, "readings.clock") || !CheckAttribute(map, "readings.course") ||
		!CheckAttribute(map, "readings.wind"))
	{
		TouchHudWorldMap.details = false;
		return;
	}
	aref compass;
	makearef(compass, view.elements.compass);
	compass.date = map.date;
	TouchLib_Reading(compass, "time", "worldMap.clock", "clock");
	TouchLib_Reading(compass, "course", "worldMap.course", "degrees");
	TouchLib_Reading(compass, "windFrom", "worldMap.windFrom", "degrees");
	TouchLib_Reading(compass, "wind", "worldMap.wind", "percent");
	TouchLib_Reading(compass, "sky", "worldMap.sky", "word");
	TouchLib_Reading(compass, "skyAt", "worldMap.skyAt", "whole");
	if (sti(map.moonPhase) >= 0) compass.moon = map.moonPhase;
	int storm = sti(map.storm);
	if (storm == 1) compass.weather = "storm";
	if (storm == 2) compass.weather = "tornado";
	if (sti(map.foodDays) >= 0) compass.days = map.foodDays;
	compass.expanded = TouchHudWorldMap.details;
	view.covers.compass = "map-labels";
	UiOn("compass", "toggle");
	if (sti(TouchHudWorldMap.details)) TouchHudWorldMap_Details(view, map);
}

void TouchHudWorldMap_Commands(aref view, aref map)
{
	if (!sti(map.commandsReady)) return;
	DeleteAttribute(&TouchHudWorldMap, "slot");
	object slot;
	aref command, button;
	int n = TouchLib_Count(map, "commands");
	for (int i = 0; i < n; i++)
	{
		command = TouchLib_Item(map, "commands", i);
		TouchHudWorldMap_Slot(command.id, &slot);
		makearef(button, view.elements.(slot.element));
		button.icon = slot.icon;
		button.label = command.label;
		button.variant = slot.variant;
		button.pressed = false;
		TouchHudWorldMap.slot.(slot.element) = command.id;
		UiOn(slot.element, "click");
	}
}

void TouchHudWorldMap_Build()
{
	aref ui = GetEventData();
	if (ui.screen != "world_map") return;
	if (UiFirst()) DeleteAttribute(&TouchHudWorldMap, "");
	if (!CheckAttribute(&TouchHudWorldMap, "details")) TouchHudWorldMap.details = false;
	UiState("gameplay", &TouchHudWorldMap_Play);
	UiState("worldMap", &TouchHudWorldMap_Map);
	aref view, play, map, sail;
	makearef(view, TouchHudWorldMap_View);
	makearef(play, TouchHudWorldMap_Play);
	makearef(map, TouchHudWorldMap_Map);
	DeleteAttribute(view, "");
	view.layout = "world-map";
	view.log = true;
	view.elements.pause = "";
	UiOn("pause", "click");
	view.elements.ship = "";
	UiOn("ship", "click");
	if (CheckAttribute(play, "controls.WMapCameraSwitch"))
	{
		view.controls.camera.kind = "hold";
		view.controls.camera.control = "WMapCameraSwitch";
	}
	if (CheckAttribute(play, "controls.WMapShipTurnLeft") || CheckAttribute(play, "controls.WMapShipTurnRight"))
	{
		view.controls.helm.kind = "helm";
		view.controls.helm.left = "WMapShipTurnLeft";
		view.controls.helm.right = "WMapShipTurnRight";
	}
	bool current = sti(map.current);
	if (CheckAttribute(play, "controls.WMapShipSailUp") || CheckAttribute(play, "controls.WMapShipSailDown"))
	{
		// The lever shows how fast the ship sails; pushed up or down it holds SailUp / SailDown.
		view.controls.sail.kind = "lever";
		view.controls.sail.up = "WMapShipSailUp";
		view.controls.sail.down = "WMapShipSailDown";
		if (current && CheckAttribute(map, "readings.speed"))
		{
			makearef(sail, view.elements.sail);
			TouchLib_Reading(sail, "value", "worldMap.speed", "percent");
		}
	}
	if (!current)
	{
		TouchHudWorldMap.details = false;
		UiShow(view);
		return;
	}
	TouchHudWorldMap.next = "";
	if (CheckAttribute(map, "timeScale")) TouchHudWorldMap.next = TouchLib_TimeScale(view, play, map.timeScale);
	TouchHudWorldMap_Fleet(view, map);
	TouchHudWorldMap_Compass(view, map);
	TouchHudWorldMap_Commands(view, map);
	UiShow(view);
}

void TouchHudWorldMap_Tap()
{
	aref ui = GetEventData();
	if (ui.screen != "world_map") return;
	string id = GetEventData(), verb = GetEventData(), value = GetEventData();
	if (id == "pause") { TouchLib_Command("shortcut", "name", "mainmenu"); return; }
	if (id == "ship") { TouchLib_Command("shortcut", "name", "shipscreen"); return; }
	if (id == "time") { TouchLib_Command("control", "control", TouchHudWorldMap.next); return; }
	if (id == "compass")
	{
		TouchHudWorldMap.details = value == "1";
		UiRebuild();
		return;
	}
	if (id == "fleet")
	{
		// A tap on a ship opens the ship screen on that ship; a ship gone since the build opens nothing.
		int k = UiAmount(value, "the tapped ship");
		if (k < 0 || k >= TouchLib_Count(&TouchHudWorldMap_Map, "fleet")) return;
		object note = {character = "-1", position = "0", tab = "", quest = ""};
		note.position = k;
		UiPass("ship", &note);
		TouchLib_Command("shortcut", "name", "shipscreen");
		return;
	}
	// A map command, exactly the one drawn: drawn pressed until the game answers it.
	TouchLib_Command("map", "id", TouchHudWorldMap.slot.(id));
	TouchHudWorldMap_View.elements.(id).pressed = true;
	UiPredict(&TouchHudWorldMap_View);
}
