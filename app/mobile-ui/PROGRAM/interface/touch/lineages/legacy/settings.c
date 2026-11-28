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
	TouchLegLib_Option(model, slider, at);
	if (node == "sensitivity") at.can = sti(at.can) && UiOffers(TOUCH_LEG_SETTINGS_H, "set");
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
	TouchLegLib_OptionToggle(ui, key, 0, on);
}

void TouchAdpSettings_Choose(aref ui, string key, int k)
{
	TouchLegLib_OptionToggle(ui, key, k, true);
}

// This lineage's options are switches and sliders, all in one window.
void TouchAdpSettings_Step(aref ui, string key, bool right)
{
	UiError("settings: the game offers no stepper " + key);
}

void TouchAdpSettings_Show(aref ui, string group)
{
}

void TouchAdpSettings_Slide(aref ui, string key, int percent)
{
	aref model = TouchLegLib_Model(ui);
	if (key != "sensitivity")
	{
		TouchLegLib_OptionSlide(model, key, percent);
		return;
	}
	// Both mouse sensitivities to the same position.
	TouchLegLib_OptionSlide(model, TOUCH_LEG_SETTINGS_V, percent);
	TouchLegLib_OptionSlide(model, TOUCH_LEG_SETTINGS_H, percent);
}

void TouchAdpSettings_Keep(aref ui)
{
	if (UiOffers("BTN_OK", "press")) UiSend("BTN_OK", "press", "");
}

void TouchAdpSettings_Leave(aref ui)
{
	UiBack(TouchLegLib_Exit(TouchLegLib_Model(ui), "BTN_CANCEL"));
}
