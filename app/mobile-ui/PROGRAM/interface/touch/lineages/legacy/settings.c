// SPDX-License-Identifier: GPL-3.0-only
// The settings of this lineage (option_screen): its *_CHECKBOX switches (a node of several rows is one choice) and
// *_SLIDE sliders, BTN_OK / BTN_CANCEL. The touch camera drag goes through the game's two mouse sensitivities: one
// row shows the vertical one and sets both. Not offered: the keyboard, colour correction (not in this renderer), the
// effects the engine's quality preset decides, «always run» (the move stick's travel decides), the game's HUD scale
// (the touch HUD replaces it). Implements contracts/settings.c.

#define TOUCH_LEG_SETTINGS_ROWS 13
// The offered nodes by group, with our name of those the window names with a caption node of its own ("" : the
// game's label of the row).
string TouchLegSettings_Node[TOUCH_LEG_SETTINGS_ROWS] = {
	"DIRECTSAIL_CHECKBOX", "ENHANCEDSAILING_CHECKBOX", "AUTOSAVE_CHECKBOX", "QUESTMARK_CHECKBOX", "SHIPMARK_CHECKBOX",
	"BATTLE_MODE_CHECKBOX", "sensitivity", "INVERT_MOUSE_CHECKBOX", "SEACAMPERSP_SLIDE", "CAMERASWING_CHECKBOX",
	"MUSIC_SLIDE", "SOUND_SLIDE", "DIALOG_SLIDE"};
string TouchLegSettings_Group[TOUCH_LEG_SETTINGS_ROWS] = {
	"sea", "sea", "sea", "marks", "marks", "marks", "camera", "camera", "camera", "camera", "sound", "sound", "sound"};
// The game's «Поворот камеры» reads as any turn; its «Инверсия» is the vertical one.
string TouchLegSettings_Label[TOUCH_LEG_SETTINGS_ROWS] = {
	"", "", "", "", "", "", "settings.camera_sensitivity", "settings.invert_vertical", "settings.sea_camera",
	"settings.door_camera_turn", "settings.music", "settings.sound_effects", "settings.voices"};

#define TOUCH_LEG_SETTINGS_V "VMOUSE_SENSITIVITY_SLIDE"
#define TOUCH_LEG_SETTINGS_H "HMOUSE_SENSITIVITY_SLIDE"

// A slider's position as 0..100 of its range.
int TouchLegSettings_Percent(aref slider)
{
	int low = sti(slider.min);
	int span = sti(slider.max) - low;
	if (span < 1) span = 1;
	int percent = makeint(100.0 * (UiAmount(slider.value, "the slider's position") - low) / span + 0.5);
	if (percent < 0) return 0;
	if (percent > 100) return 100;
	return percent;
}

// The slider's position at `percent` of its range, on its steps.
string TouchLegSettings_Position(aref slider, int percent)
{
	int low = sti(slider.min);
	int high = sti(slider.max);
	int step = sti(slider.step);
	if (step < 1) step = 1;
	float wanted = (high - low) * percent / 100.0;
	int position = low + makeint(wanted / step + 0.5) * step;
	if (position > high) position = high;
	if (position < low) position = low;
	return "" + position;
}

void TouchLegSettings_Row(aref model, aref out, int n)
{
	string node = TouchLegSettings_Node[n];
	string label = TouchLegSettings_Label[n];
	if (label != "") label = UiText(label);
	string slider = node;
	if (node == "sensitivity")
	{
		if (!CheckAttribute(model, TOUCH_LEG_SETTINGS_V) || !CheckAttribute(model, TOUCH_LEG_SETTINGS_H)) return;
		slider = TOUCH_LEG_SETTINGS_V;
	}
	if (!CheckAttribute(model, slider)) return;
	aref groups;
	makearef(groups, out.groups.(TouchLegSettings_Group[n]));
	aref at = TouchLib_Add(groups, "r");
	at.key = node;
	at.label = label;
	if (model.(slider).type == "slider")
	{
		aref widget;
		makearef(widget, model.(slider));
		at.kind = "slider";
		at.value = TouchLegSettings_Percent(widget);
		at.can = UiOffers(slider, "set");
		if (node == "sensitivity") at.can = sti(at.can) && UiOffers(TOUCH_LEG_SETTINGS_H, "set");
		at.reason = widget.reason;
		return;
	}
	int count = TouchLib_Count(model, node + ".rows");
	if (count == 0) UiError("settings: the switch " + node + " has no rows");
	aref first = TouchLib_Item(model, node + ".rows", 0);
	at.reason = first.reason;
	if (count == 1)
	{
		at.kind = "toggle";
		if (label == "") at.label = first.label;
		at.on = first.value == "1";
		at.can = CheckAttribute(first, "offers.toggle");
		return;
	}
	at.kind = "segment";
	at.selected = 0;
	at.can = false;
	for (int k = 0; k < count; k++)
	{
		aref option = TouchLib_Item(model, node + ".rows", k);
		at.options.("o" + k) = option.label;
		if (option.value == "1") at.selected = k;
		if (CheckAttribute(option, "offers.toggle")) at.can = true;
	}
}

void TouchAdpSettings_State(aref ui, aref out)
{
	aref model = TouchLegLib_Model(ui);
	for (int n = 0; n < TOUCH_LEG_SETTINGS_ROWS; n++) TouchLegSettings_Row(model, out, n);
	out.done.can = UiOffers("BTN_OK", "press");
	out.done.reason = model.BTN_OK.reason;
}

void TouchAdpSettings_Toggle(aref ui, string key, bool on)
{
	if (!CheckAttribute(ui, "model." + key + ".rows.r0.offers.toggle")) return;
	object action = {node = key, verb = "toggle", value = "0", row = 0};
	if (on) action.value = "1";
	UiSendTo(&action);
}

void TouchAdpSettings_Choose(aref ui, string key, int k)
{
	if (!CheckAttribute(ui, "model." + key + ".rows.r" + k + ".offers.toggle")) return;
	object action = {node = key, verb = "toggle", value = "1", row = k};
	UiSendTo(&action);
}

void TouchAdpSettings_Slide(aref ui, string key, int percent)
{
	aref model = TouchLegLib_Model(ui);
	aref slider;
	if (key == "sensitivity")
	{
		// Both mouse sensitivities to the same position.
		makearef(slider, model.(TOUCH_LEG_SETTINGS_V));
		UiSend(TOUCH_LEG_SETTINGS_V, "set", TouchLegSettings_Position(slider, percent));
		makearef(slider, model.(TOUCH_LEG_SETTINGS_H));
		UiSend(TOUCH_LEG_SETTINGS_H, "set", TouchLegSettings_Position(slider, percent));
		return;
	}
	if (!CheckAttribute(model, key)) UiError("settings: no slider " + key);
	makearef(slider, model.(key));
	UiSend(key, "set", TouchLegSettings_Position(slider, percent));
}

void TouchAdpSettings_Keep(aref ui)
{
	if (UiOffers("BTN_OK", "press")) UiSend("BTN_OK", "press", "");
}

void TouchAdpSettings_Leave(aref ui)
{
	UiBack(TouchLegLib_Exit(TouchLegLib_Model(ui), "BTN_CANCEL"));
}
