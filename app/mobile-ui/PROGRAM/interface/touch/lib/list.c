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
