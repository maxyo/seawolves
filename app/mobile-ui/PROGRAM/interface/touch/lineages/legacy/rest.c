// SPDX-License-Identifier: GPL-3.0-only
// Rest from the interface tavernwait: the game's hour slider, its OK and its Cancel. Implements contracts/rest.c,
// which it does not include: a segment cannot define what it declares extern.

// A slider moved: the game answers in its next frame (its eSlideChange is a posted event, and iHourWait and the text
// follow it), so the build of the frame the move was applied in is not drawn.
object TouchLegRest;

int TouchLegRest_Range(aref slider)
{
	int range = sti(slider.max) - sti(slider.min);
	if (range < 1) UiError("rest: HOURS_SLIDE has no range");
	return range;
}

void TouchAdpRest_State(aref ui, aref out)
{
	// the hours the game's slider handler made of its position (TavernWait.c), a global of the interface
	out.hours = UiGameInt("iHourWait");
	out.canGo = UiOffers("OK_BTN", "press");
	out.reason = ui.model.OK_BTN.reason;
	out.note = "";
	if (CheckAttribute(ui, "model.INFO_TEXT")) out.note = ui.model.INFO_TEXT.label;
	string answer = UiAnswer();
	if (answer == "" || answer == "refused") TouchLegRest.slid = false;
	if (answer == "applied" && sti(TouchLegRest.slid))
	{
		TouchLegRest.slid = false;
		UiAwait();
	}
}

// The slider spans a day: the smallest position of its share that still gives the hours (the game exposes no position
// for an hour count; State reads back what the game made of it).
bool TouchAdpRest_Choose(aref ui, int hours)
{
	aref slider;
	makearef(slider, ui.model.HOURS_SLIDE);
	int at = (hours * TouchLegRest_Range(slider) + 23) / 24 + sti(slider.min);
	if (at > sti(slider.max)) at = sti(slider.max);
	if (at == sti(slider.value)) return false;
	UiSend("HOURS_SLIDE", "set", "" + at);
	TouchLegRest.slid = true;
	return true;
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
