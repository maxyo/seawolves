// SPDX-License-Identifier: GPL-3.0-only
// The settings of Caribbean Legend's set (option_screen): its window shows one page of options at a time (BTN_<page>),
// an option a switch (*_CHECKBOX), a slider (*_SLIDE) or a ‹ › choice (LEFTCHANGE_<key> / RIGHTCHANGE_<key> around
// DESCRIP_TEXT_<key>) captioned by a row of the page's caption list; BTN_OK / BTN_CANCEL. The island foliage density
// is a slider of the game's picture preview (BTN_GRAPHICS, then BTN_IMAGE_SETTINGS), which applies it at once and keeps
// it with BTN_GRAPHICS_APPLY. Not offered: the keyboard, the rest of the preview (the application's graphics
// settings), the game's HUD and its hints (the touch HUD replaces them), the autosave counts. Implements
// contracts/settings.c.

#define TOUCH_CL_SETTINGS_ROWS 17
string TouchClSettings_Node[TOUCH_CL_SETTINGS_ROWS] = {
	"NAVAL_COMBAT", "BEST_COURSE", "WORLDMAP_ICONS", "PROMO_CONTENT_CHECKBOX", "QUESTMARK_CHECKBOX",
	"CHAR_STRING_CHECKBOX", "SHIPMARKS", "TARGET", "MAP_LABELS", "TUTORIAL_CHECKBOX", "LOC_MOUSE_SENSITIVITY_SLIDE",
	"SEA_MOUSE_SENSITIVITY_SLIDE", "INVERT_MOUSE_CHECKBOX", "MUSIC_SLIDE", "SOUND_SLIDE", "DIALOG_SLIDE",
	"ISLAND_FOLIAGE_DENSITY_SLIDE"};
string TouchClSettings_Group[TOUCH_CL_SETTINGS_ROWS] = {
	"sea", "sea", "sea", "sea", "marks", "marks", "marks", "marks", "marks", "marks", "camera", "camera", "camera",
	"sound", "sound", "sound", "graphics"};
// The caption: "" the switch's own, "<node>:<k>" row k of the page's caption list, else a caption node.
string TouchClSettings_Caption[TOUCH_CL_SETTINGS_ROWS] = {
	"TITLES_STR_ACCESSIBILITY:1", "TITLES_STR_ACCESSIBILITY:4", "TITLES_STR_ACCESSIBILITY:6", "", "", "",
	"TITLES_STR_INTERFACE:8", "TITLES_STR_INTERFACE:10", "TITLES_STR_INTERFACE:12", "", "TITLES_STR_CONTROLS:2",
	"TITLES_STR_CONTROLS:3", "", "TITLES_STR_AUDIO:1", "TITLES_STR_AUDIO:2", "TITLES_STR_AUDIO:3",
	"ISLAND_FOLIAGE_DENSITY_TITLE"};

// The button that turns the window to a group's page.
string TouchClSettings_Page(aref model, string group)
{
	if (group == "graphics")
	{
		if (CheckAttribute(model, "BTN_IMAGE_SETTINGS")) return "BTN_IMAGE_SETTINGS";
		return "BTN_GRAPHICS";
	}
	if (group == "sea") return "BTN_ACCESSIBILITY";
	if (group == "marks") return "BTN_INTERFACE";
	if (group == "camera") return "BTN_CONTROLS";
	if (group == "sound") return "BTN_AUDIO";
	UiError("settings: no page of the group " + group);
	return "";
}

string TouchClSettings_Label(aref model, string caption)
{
	if (caption == "") return "";
	int colon = findsubstr(caption, ":", 0);
	if (colon < 0) return TouchLegLib_Label(model, caption);
	string node = TouchLib_Cut(caption, 0, colon);
	string path = node + ".rows.r" + TouchLib_Cut(caption, colon + 1, strlen(caption));
	if (!CheckAttribute(model, path)) UiError("settings: no caption " + caption);
	return model.(path).label;
}

// Row n of the table when its page is shown: true when it was.
bool TouchClSettings_Row(aref model, aref out, int n)
{
	string key = TouchClSettings_Node[n];
	string value = "DESCRIP_TEXT_" + key;
	bool step = CheckAttribute(model, value);
	if (!step && !CheckAttribute(model, key)) return false;
	aref groups;
	makearef(groups, out.groups.(TouchClSettings_Group[n]));
	aref at = TouchLib_Add(groups, "r");
	at.key = key;
	at.label = TouchClSettings_Label(model, TouchClSettings_Caption[n]);
	if (!step)
	{
		TouchLegLib_Option(model, key, at);
		return true;
	}
	at.kind = "step";
	at.text = model.(value).label;
	at.left = UiOffers("LEFTCHANGE_" + key, "press");
	at.right = UiOffers("RIGHTCHANGE_" + key, "press");
	at.can = sti(at.left) || sti(at.right);
	at.reason = model.(value).reason;
	return true;
}

// «done» keeps the picture preview while it is open, else the options.
string TouchClSettings_Done(aref model)
{
	if (CheckAttribute(model, "BTN_GRAPHICS_APPLY")) return "BTN_GRAPHICS_APPLY";
	return "BTN_OK";
}

void TouchAdpSettings_State(aref ui, aref out)
{
	aref model = TouchLegLib_Model(ui);
	object shown;
	for (int n = 0; n < TOUCH_CL_SETTINGS_ROWS; n++)
	{
		if (TouchClSettings_Row(model, out, n)) shown.(TouchClSettings_Group[n]) = true;
	}
	for (n = 0; n < TOUCH_CL_SETTINGS_ROWS; n++)
	{
		string group = TouchClSettings_Group[n];
		if (!CheckAttribute(&shown, group) && CheckAttribute(model, TouchClSettings_Page(model, group)))
			out.hidden.(group) = 1;
	}
	string done = TouchClSettings_Done(model);
	out.done.can = UiOffers(done, "press");
	out.done.reason = model.(done).reason;
}

void TouchAdpSettings_Toggle(aref ui, string key, bool on)
{
	TouchLegLib_OptionToggle(ui, key, 0, on);
}

void TouchAdpSettings_Choose(aref ui, string key, int k)
{
	TouchLegLib_OptionToggle(ui, key, k, true);
}

void TouchAdpSettings_Step(aref ui, string key, bool right)
{
	string node = "LEFTCHANGE_" + key;
	if (right) node = "RIGHTCHANGE_" + key;
	if (UiOffers(node, "press")) UiSend(node, "press", "");
}

void TouchAdpSettings_Show(aref ui, string group)
{
	string page = TouchClSettings_Page(TouchLegLib_Model(ui), group);
	if (UiOffers(page, "press")) UiSend(page, "press", "");
}

void TouchAdpSettings_Slide(aref ui, string key, int percent)
{
	TouchLegLib_OptionSlide(TouchLegLib_Model(ui), key, percent);
}

void TouchAdpSettings_Keep(aref ui)
{
	string done = TouchClSettings_Done(TouchLegLib_Model(ui));
	if (UiOffers(done, "press")) UiSend(done, "press", "");
}

void TouchAdpSettings_Leave(aref ui)
{
	UiBack(TouchLegLib_Exit(TouchLegLib_Model(ui), "BTN_GRAPHICS_CANCEL BTN_CANCEL"));
}
