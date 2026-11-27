// SPDX-License-Identifier: GPL-3.0-only
// Rest: how long to rest, as a segment of the usual spans in a dialog over the paused scene; a span the segment does
// not offer is named by the game's own text.
#include "interface/touch/capabilities.h"

#event_handler("UiBuild", "TouchRest_Build");
#event_handler("UiTap", "TouchRest_Tap");

object TouchRest;
int TouchRest_Spans[5] = {1, 3, 6, 12, 24};
string TouchRest_Labels[5] = {"rest.h1", "rest.h3", "rest.h6", "rest.h12", "rest.day"};

void TouchRest_View(aref game, int hours)
{
	DeleteAttribute(&TouchRest, "");
	TouchRest.title = UiText("rest.title");
	bool preset = false;
	aref at;
	string slot;
	for (int i, int span: TouchRest_Spans)
	{
		slot = "s" + i;
		makearef(at, TouchRest.hours.(slot));
		at.id = span;
		at.label = UiText(TouchRest_Labels[i]);
		at.selected = span == hours;
		preset = preset || span == hours;
	}
	makearef(at, TouchRest.actions.go);
	at.id = "go";
	at.label = UiText("rest.go");
	at.variant = "primary";
	at.disabled = !sti(game.canGo);
	at.reason = game.reason;
	at.busy = UiPending("go");
	if (!preset) at.sub = game.note;
}

void TouchRest_Build()
{
	if (GetEventData() != TOUCH_CAP_REST) return;
	object game;
	TouchAdpRest_State(&game);
	TouchRest_View(&game, sti(game.hours));
	UiOn("hours", "select");
	UiOn("go", "");
	UiOn("rest-close", "");
	UiModal("rest", "rest-close");
	UiShow(&TouchRest);
}

void TouchRest_Tap()
{
	if (GetEventData() != TOUCH_CAP_REST) return;
	string id = GetEventData(), verb = GetEventData(), value = GetEventData();
	if (id == "go") { TouchAdpRest_Go(); return; }
	if (id == "rest-close") { TouchAdpRest_Close(); return; }
	// The tapped span is drawn selected from this frame; the game's answer confirms it.
	object game;
	TouchAdpRest_State(&game);
	TouchRest_View(&game, sti(value));
	UiPredict(&TouchRest);
	TouchAdpRest_Choose(sti(value));
}
