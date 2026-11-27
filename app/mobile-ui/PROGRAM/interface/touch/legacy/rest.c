// SPDX-License-Identifier: GPL-3.0-only
// Rest (interface tavernwait): the game's hour slider as a segment of the usual spans in a dialog over the paused
// scene; «Отдыхать» is the game's OK, the close cross its Cancel.
#include "interface/touch/capabilities.h"

#event_handler("UiBuild", "Rest_Build");
#event_handler("UiTap", "Rest_Tap");

object RestView;

// The game reads its slider as a share of a day and truncates: hours = int(share * 24).
int Rest_Range()
{
	int range = sti(UiModel.HOURS_SLIDE.max) - sti(UiModel.HOURS_SLIDE.min);
	if (range < 1) return 1;
	return range;
}

int Rest_Span(string slot, int hours, string key, int current)
{
	aref item;
	makearef(item, RestView.hours.(slot));
	item.id = hours;
	item.label = UiText(key);
	item.selected = hours == current;
	return hours == current;
}

string Rest_Cancel()
{
	if (CheckAttribute(&UiModel, "CANCEL_BTN")) return "CANCEL_BTN";
	if (CheckAttribute(&UiModel, "EXIT_BTN")) return "EXIT_BTN";
	return "";
}

void Rest_Build()
{
	if (GetEventData() != UI_REST) return;
	int current = (sti(UiModel.HOURS_SLIDE.value) - sti(UiModel.HOURS_SLIDE.min)) * 24 / Rest_Range();
	DeleteAttribute(&RestView, "");
	RestView.title = UiText("rest.title");
	int preset = Rest_Span("h1", 1, "rest.h1", current) + Rest_Span("h3", 3, "rest.h3", current);
	preset = preset + Rest_Span("h6", 6, "rest.h6", current) + Rest_Span("h12", 12, "rest.h12", current);
	preset = preset + Rest_Span("day", 24, "rest.day", current);

	RestView.actions.go.id = "go";
	RestView.actions.go.label = UiText("rest.go");
	RestView.actions.go.variant = "primary";
	RestView.actions.go.disabled = !UiOffers("OK_BTN", "press");
	RestView.actions.go.reason = UiModel.OK_BTN.reason;
	RestView.actions.go.busy = UiPending("go");
	// A span the segment does not offer (set by the game) is named by the game's own text.
	if (preset == 0)
	{
		RestView.actions.go.sub = "";
		if (CheckAttribute(&UiModel, "INFO_TEXT")) RestView.actions.go.sub = UiModel.INFO_TEXT.label;
	}
	UiOn("hours", "select");
	UiOn("go", "");
	UiOn("rest-close", "");
	UiModal("rest", "rest-close");
	UiShow(&RestView);
}

void Rest_Tap()
{
	if (GetEventData() != UI_REST) return;
	string id = GetEventData();
	string verb = GetEventData();
	string value = GetEventData();
	if (id == "go")
	{
		UiSend("OK_BTN", "press", "");
		return;
	}
	if (id == "rest-close")
	{
		UiBack(Rest_Cancel());
		return;
	}
	// The smallest slider position that still gives the chosen hours.
	int at = (sti(value) * Rest_Range() + 23) / 24 + sti(UiModel.HOURS_SLIDE.min);
	if (at > sti(UiModel.HOURS_SLIDE.max)) at = sti(UiModel.HOURS_SLIDE.max);
	UiSend("HOURS_SLIDE", "set", "" + at);
}
