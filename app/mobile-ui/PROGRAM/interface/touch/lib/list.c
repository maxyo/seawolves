// SPDX-License-Identifier: GPL-3.0-only
// Lists of the screens' views and of the adapters' states: entries named <prefix><n> in order, their head (columns
// to sort by, filters, figures) and the facts and buttons of an entry.

// The next entry of `list`, <prefix><n> after the n it has.
aref TouchLib_Add(aref list, string prefix)
{
	aref at;
	string slot = prefix + GetAttributesNum(list);
	makearef(at, list.(slot));
	return at;
}

// How many entries the list `path` of `at` has; an absent list has none.
int TouchLib_Count(aref at, string path)
{
	if (!CheckAttribute(at, path)) return 0;
	aref list;
	makearef(list, at.(path));
	return GetAttributesNum(list);
}

// Entry k of the list `path` of `at`; one it does not have is the caller's defect.
aref TouchLib_Item(aref at, string path, int k)
{
	if (k < 0 || k >= TouchLib_Count(at, path)) UiError("no entry " + k + " of " + path);
	aref list;
	makearef(list, at.(path));
	return GetAttributeN(list, k);
}

// The entries of `rows` copied to `out` with .k (their position in `rows`), in the order of their whole number
// `field` (stable; an entry without it counts as 0); `field` "" keeps the order.
void TouchLib_Order(aref rows, string field, bool descending, aref out)
{
	for (int k = 0; k < GetAttributesNum(rows); k++)
	{
		aref at = TouchLib_Add(out, "r");
		CopyAttributes(at, GetAttributeN(rows, k));
		at.k = k;
		at.by = 0;
		if (field != "") { if (CheckAttribute(at, field)) at.by = at.(field); }
	}
	if (field != "") UiOrder(out, "by", descending);
}

// The number a tap carries for an entry of a list of `count`: anything else is the view's defect.
int TouchLib_TapIndex(string value, int count, string what)
{
	int k = UiAmount(value, what);
	if (k < 0 || k >= count || "" + k != value) UiError(what + " " + value + " is not one of " + count);
	return k;
}

// A column of a list head, sortable: `sort` is the screen's {by (the column, -1 none), descending}.
void TouchLib_Column(aref list, string label, string icon, string iconTone, aref order)
{
	aref cols;
	makearef(cols, list.cols);
	int n = GetAttributesNum(cols);
	aref at = TouchLib_Add(cols, "c");
	at.label = label;
	at.icon = icon;
	at.iconTone = iconTone;
	at.sortable = true;
	at.sort = "";
	if (sti(order.by) != n) return;
	at.sort = "asc";
	if (sti(order.descending)) at.sort = "desc";
}

// A tap on column n: largest first, smallest first, the list's own order again.
void TouchLib_SortTap(aref order, int n)
{
	if (sti(order.by) != n)
	{
		order.by = n;
		order.descending = true;
		return;
	}
	if (sti(order.descending)) order.descending = false;
	else order.by = -1;
}

aref TouchLib_Filter(aref list, string label, bool selected)
{
	aref filters;
	makearef(filters, list.filters);
	aref at = TouchLib_Add(filters, "f");
	at.label = label;
	at.selected = selected;
	return at;
}

// A fact of an entry and a figure of a head; an empty icon, tone or label is none.
aref TouchLib_Fact(aref facts, string icon, string value, string tone)
{
	aref at = TouchLib_Add(facts, "f");
	if (icon != "") at.icon = icon;
	at.v = value;
	if (tone != "") at.tone = tone;
	return at;
}

aref TouchLib_Stat(aref stats, string label, string value, string icon)
{
	aref at = TouchLib_Add(stats, "s");
	if (label != "") at.label = label;
	at.value = value;
	if (icon != "") at.icon = icon;
	return at;
}

// The secondary buttons of a view from {label, can, reason} entries, ids action-<k>, bound.
void TouchLib_Actions(aref view, aref buttons)
{
	aref actions;
	makearef(actions, view.actions);
	for (int k = 0; k < GetAttributesNum(buttons); k++)
	{
		aref button = GetAttributeN(buttons, k);
		object action = {id = "action-" + k, label = button.label, variant = "secondary", disabled = !sti(button.can),
		                 reason = button.reason, busy = UiPending("action-" + k)};
		aref at = TouchLib_Add(actions, "a");
		CopyAttributes(at, &action);
		UiOn("action-" + k, "");
	}
}

// k of a tapped id action-<k>, -1 for another id.
int TouchLib_Action(string id)
{
	if (!TouchLib_StartsWith(id, "action-")) return -1;
	return UiAmount(TouchLib_Cut(id, 7, strlen(id)), "the action " + id);
}

// The words of `text` between spaces, tabs and line breaks, as {text} entries w<n> of `out`.
void TouchLib_Words(string text, aref out)
{
	string separators = " 	" + TouchLib_LineEnds();
	int from = 0;
	for (int at = 0; at <= strlen(text); at++)
	{
		bool end = at == strlen(text);
		if (!end) end = findSubStr(separators, strcut(text, at, at), 0) >= 0;
		if (end)
		{
			if (at > from)
			{
				aref word = TouchLib_Add(out, "w");
				word.text = TouchLib_Cut(text, from, at);
			}
			from = at + 1;
		}
	}
}

// The hero section a screen's header shows: its own (`own`), or the one tapped (TouchLib_SectionAsk) while the game
// opens it. The press ends this interface and the next opens in a later game frame with no model of this one between:
// every build awaits it meanwhile; a refused press shows the own section again. `state` is the screen's, kept
// across its builds.
string TouchLib_Section(aref state, string own)
{
	if (UiFirst()) DeleteAttribute(state, "asked");
	if (!CheckAttribute(state, "asked")) return own;
	string answer = UiAnswer();
	if (answer == "refused" || answer == "")
	{
		DeleteAttribute(state, "asked");
		return own;
	}
	UiAwait();
	return state.asked;
}

// A section tapped in the header: true when the screen asks the game for it now. Its own section asks nothing; while
// another opens, the game refuses every press of this ending interface, so nothing more is asked.
bool TouchLib_SectionAsk(aref state, string own, string tapped)
{
	if (tapped == own || CheckAttribute(state, "asked")) return false;
	state.asked = tapped;
	return true;
}

// The header's switch over the people a screen shows one of: `people` entries {label, meta, image ("" none: a picture
// {kind, group, image})}, k `shown`, its list open or not. Its events (TouchLib_SubjectOn in a build): header
// subject-prev, subject-next, subject-list, header-subject-<k>, the list's ✕ header-subjects-close and its dismissal.
void TouchLib_Subject(aref view, aref people, int shown, bool open)
{
	int count = GetAttributesNum(people);
	if (shown < 0 || shown >= count) return;
	aref person = GetAttributeN(people, shown);
	view.label = person.label;
	view.sub = "";
	if (count > 1) view.sub = (shown + 1) + " " + UiText("common.of") + " " + count;
	view.prev = shown > 0;
	view.next = shown + 1 < count;
	view.list = count > 1;
	// The list is written closed too: a prediction is a change laid over later views, whose list may be either.
	view.open = open && count > 1;
	if (count < 2) return;
	view.title = UiText("hero.subjects");
	DeleteAttribute(view, "rows");
	aref rows;
	makearef(rows, view.rows);
	for (int k = 0; k < count; k++)
	{
		person = GetAttributeN(people, k);
		aref at = TouchLib_Add(rows, "r");
		at.label = person.label;
		at.meta = person.meta;
		at.icon = "hero";
		at.image = "";
		if (CheckAttribute(person, "image.kind"))
		{
			aref image, from;
			makearef(image, at.image);
			makearef(from, person.image);
			CopyAttributes(image, from);
		}
		at.selected = k == shown;
	}
}

void TouchLib_SubjectOn(aref people, bool open)
{
	UiOn("header", "subject-prev");
	UiOn("header", "subject-next");
	UiOn("header", "subject-list");
	int count = GetAttributesNum(people);
	if (!open || count < 2) return;
	for (int k = 0; k < count; k++) UiOn("header-subject-" + k, "");
	UiOn("header-subjects-close", "");
	UiModal("header-subjects", "header-subjects-close");
}

// A tap on the switch: k of the person it picks, -1 none; `state.open` follows the list opened and closed.
int TouchLib_SubjectTap(aref state, string id, string verb, int shown, int count)
{
	if (id == "header" && verb == "subject-prev" && shown > 0) return shown - 1;
	if (id == "header" && verb == "subject-next" && shown + 1 < count) return shown + 1;
	if (id == "header" && verb == "subject-list") { state.open = true; UiRebuild(); return -1; }
	if (id == "header-subjects-close") { state.open = false; UiRebuild(); return -1; }
	if (!TouchLib_StartsWith(id, "header-subject-")) return -1;
	state.open = false;
	UiRebuild();
	int k = TouchLib_TapIndex(TouchLib_Cut(id, 15, strlen(id)), count, "the person");
	if (k == shown) return -1;
	return k;
}
