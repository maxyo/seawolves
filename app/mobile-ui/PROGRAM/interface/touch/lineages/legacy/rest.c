// SPDX-License-Identifier: GPL-3.0-only
// Rest from the interface tavernwait: the game's hour slider, its OK and its Cancel. Implements contracts/rest.c,
// which it does not include: a segment cannot define what it declares extern.

// rule copy: tavernwait.c reads its slider as a share of a day and truncates, hours = int(share * 24).
int TouchLegRest_Range()
{
	int range = sti(UiModel.HOURS_SLIDE.max) - sti(UiModel.HOURS_SLIDE.min);
	if (range < 1) return 1;
	return range;
}

void TouchAdpRest_State(aref out)
{
	out.hours = (sti(UiModel.HOURS_SLIDE.value) - sti(UiModel.HOURS_SLIDE.min)) * 24 / TouchLegRest_Range();
	out.canGo = UiOffers("OK_BTN", "press");
	out.reason = UiModel.OK_BTN.reason;
	out.note = "";
	if (CheckAttribute(&UiModel, "INFO_TEXT")) out.note = UiModel.INFO_TEXT.label;
}

// The smallest slider position that still gives the chosen hours.
void TouchAdpRest_Choose(int hours)
{
	int at = (hours * TouchLegRest_Range() + 23) / 24 + sti(UiModel.HOURS_SLIDE.min);
	if (at > sti(UiModel.HOURS_SLIDE.max)) at = sti(UiModel.HOURS_SLIDE.max);
	UiSend("HOURS_SLIDE", "set", "" + at);
}

void TouchAdpRest_Go()
{
	UiSend("OK_BTN", "press", "");
}

void TouchAdpRest_Close()
{
	if (CheckAttribute(&UiModel, "CANCEL_BTN")) { UiBack("CANCEL_BTN"); return; }
	if (CheckAttribute(&UiModel, "EXIT_BTN")) { UiBack("EXIT_BTN"); return; }
	UiBack("");
}
