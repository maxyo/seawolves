// SPDX-License-Identifier: GPL-3.0-only
// What the HUD scenes share: a reading laid into a prop, the time acceleration button.

// Field `field` of `at` is a reading (one or several, space-separated, each with its format) the runtime lays in.
aref TouchLib_Reading(aref at, string field, string reading, string as)
{
	aref value;
	makearef(value, at.(field));
	value.reading = reading;
	value.as = as;
	return value;
}

// The game's time acceleration in four steps: the "time" button shows the scale the engine runs at (`scale`, the
// state's figure), a tap asks for the next of x1, x2, x4, x8 (the game's scripts clamp it to their limits). Offered
// when the gameplay binds all four; returns the control a tap sends, "" for no button.
string TouchLib_TimeScale(aref view, aref play, string scale)
{
	if (scale == "") return "";
	string steps[4] = {"TimeScaleX1", "TimeScaleX2", "TimeScaleX4", "TimeScaleX8"};
	for (int i = 0; i < 4; i++)
		if (!CheckAttribute(play, "controls." + steps[i])) return "";
	float figure = stf(scale);
	int current = 3;
	if (figure < 6.0) current = 2;
	if (figure < 3.0) current = 1;
	if (figure < 1.5) current = 0;
	view.elements.time.badge = "×" + scale;
	view.elements.time.variant = "normal";
	if (current > 0) view.elements.time.variant = "active";
	UiOn("time", "click");
	return steps[(current + 1) % 4];
}
