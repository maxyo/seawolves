// SPDX-License-Identifier: GPL-3.0-only
// Rest from the interface tavernwait: the game's hour slider, its OK and its Cancel. Implements contracts/rest.c,
// which it does not include: a segment cannot define what it declares extern.

// rule copy: tavernwait.c reads its slider as a share of a day and truncates, hours = int(share * 24).
int TouchLegRest_Range(aref slider)
{
	int range = sti(slider.max) - sti(slider.min);
	if (range < 1) UiError("rest: HOURS_SLIDE has no range");
	return range;
}

void TouchAdpRest_State(aref ui, aref out)
{
	aref slider;
	makearef(slider, ui.model.HOURS_SLIDE);
	out.hours = (sti(slider.value) - sti(slider.min)) * 24 / TouchLegRest_Range(slider);
	out.canGo = UiOffers("OK_BTN", "press");
	out.reason = ui.model.OK_BTN.reason;
	out.note = "";
	if (CheckAttribute(ui, "model.INFO_TEXT")) out.note = ui.model.INFO_TEXT.label;
}

// The smallest slider position that still gives the chosen hours.
void TouchAdpRest_Choose(aref ui, int hours)
{
	aref slider;
	makearef(slider, ui.model.HOURS_SLIDE);
	int at = (hours * TouchLegRest_Range(slider) + 23) / 24 + sti(slider.min);
	if (at > sti(slider.max)) at = sti(slider.max);
	UiSend("HOURS_SLIDE", "set", "" + at);
}

void TouchAdpRest_Go(aref ui)
{
	UiSend("OK_BTN", "press", "");
}

void TouchAdpRest_Close(aref ui)
{
	if (CheckAttribute(ui, "model.CANCEL_BTN")) { UiBack("CANCEL_BTN"); return; }
	if (CheckAttribute(ui, "model.EXIT_BTN")) { UiBack("EXIT_BTN"); return; }
	UiBack("");
}
