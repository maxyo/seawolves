// SPDX-License-Identifier: GPL-3.0-only
// Items, a hero section: every slot with what is worn in it (a tap lists what fits there), the things carried under
// the game's categories, the chosen one described with its figures against what is worn and the game's action on it,
// the game's map window; the header switches between the people whose things the game shows.

#event_handler("UiBuild", "TouchItems_Build");
#event_handler("UiTap", "TouchItems_Tap");

// The adapter's state of the last build (.game), the slot whose items the list shows (.filter, -1 all) and whose
// things they were (.who), the people's list open (.open), the rows listed (.listed.l<i>: k of game.rows).
object TouchItems;
object TouchItems_View;
object TouchItems_Header; // the section tapped while the game opens it (TouchLib_Section)

// A figure as the game's numbers are written: `digits` after the point.
string TouchItems_Number(float value, int digits)
{
	if (digits > 0) return UiDecimal(Sprintf("%.1f", value));
	return UiDecimal(Sprintf("%.0f", value));
}

// Whether row k is listed under the slot filter: what fits the slot, and of what is worn only what this slot holds.
bool TouchItems_Fits(aref game, aref at, int k)
{
	int filter = sti(TouchItems.filter);
	if (filter < 0) return true;
	aref slot = TouchLib_Item(game, "slots", filter);
	if (at.group != slot.group) return false;
	if (slot.place != "" && at.place != "" && slot.place != at.place) return false;
	if (!sti(at.worn)) return true;
	return sti(slot.row) == k;
}

void TouchItems_Slots(aref game, aref view)
{
	aref slots;
	makearef(slots, view.slots);
	for (int k = 0; k < TouchLib_Count(game, "slots"); k++)
	{
		aref slot = TouchLib_Item(game, "slots", k);
		aref at = TouchLib_Add(slots, "s");
		at.id = k;
		at.label = slot.label;
		if (slot.days != "") at.label = UiFill(UiText("items.days"), slot.days);
		at.icon = slot.icon;
		at.selected = sti(TouchItems.filter) == k;
		at.empty = !CheckAttribute(slot, "image");
		if (!sti(at.empty))
		{
			TouchLib_Copy(at, "image", slot, "image");
		}
	}
}

void TouchItems_Rows(aref game, aref view)
{
	aref rows, listed;
	makearef(rows, view.rows);
	makearef(listed, TouchItems.listed);
	for (int k = 0; k < TouchLib_Count(game, "rows"); k++)
	{
		aref row = TouchLib_Item(game, "rows", k);
		if (TouchItems_Fits(game, row, k))
		{
			aref at = TouchLib_Add(rows, "r");
			aref entry = TouchLib_Add(listed, "l");
			entry.k = k;
			at.id = "r" + k;
			at.selected = row.selected;
			aref name;
			makearef(name, at.cells.c0);
			name.text = row.label;
			TouchLib_Copy(name, "image", row, "image");
			if (sti(row.worn)) name.badge = "check";
			else { if (sti(row.quest)) name.badge = "seal"; }
			at.cells.c1 = "";
			if (sti(row.count) > 1) at.cells.c1 = "×" + row.count;
			at.cells.c2 = row.weight;
		}
	}
	view.empty = UiText("items.none");
	if (sti(TouchItems.filter) >= 0) view.empty = UiText("items.none_fits");
}

void TouchItems_Fact(aref facts, string key, string value)
{
	aref at = TouchLib_Add(facts, "f");
	at.k = key;
	at.v = value;
}

// The figures, each with its difference from the worn one: green where the item is better, red where worse; a figure
// with no better side keeps its difference plain.
void TouchItems_Figures(aref detail, aref facts)
{
	for (int n = 0; n < TouchLib_Count(detail, "figures"); n++)
	{
		aref figure = TouchLib_Item(detail, "figures", n);
		int digits = sti(figure.digits);
		float value = stf(figure.value);
		aref at = TouchLib_Add(facts, "f");
		at.k = UiText(figure.key);
		at.v = TouchItems_Number(value, digits);
		if (CheckAttribute(figure, "against"))
		{
			float change = value - stf(figure.against);
			string shown = TouchItems_Number(abs(change), digits);
			if (shown != TouchItems_Number(0.0, digits))
			{
				if (change > 0) at.d = "+" + shown;
				else at.d = "−" + shown;
				int better = sti(figure.better);
				if (better != 0)
				{
					at.dTone = "danger";
					if ((change > 0) == (better > 0)) at.dTone = "ok";
				}
			}
		}
	}
}

void TouchItems_Detail(aref game, aref view)
{
	if (!CheckAttribute(game, "detail")) return;
	aref detail;
	makearef(detail, game.detail);
	// Under a slot filter the parchment speaks for that slot only.
	aref row = TouchLib_Item(game, "rows", sti(detail.row));
	if (!TouchItems_Fits(game, row, sti(detail.row))) return;
	aref at;
	makearef(at, view.detail);
	at.title = detail.title;
	at.sub = detail.slot;
	if (sti(detail.worn))
	{
		if (at.sub != "") at.sub = at.sub + " · ";
		at.sub = at.sub + UiText("items.worn");
	}
	TouchLib_Copy(at, "image", detail, "image");
	aref facts;
	makearef(facts, at.facts);
	TouchItems_Figures(detail, facts);
	if (detail.kind != "") TouchItems_Fact(facts, UiText("items.kind"), detail.kind);
	if (detail.weight != "") TouchItems_Fact(facts, UiText("items.weight"), TouchItems_Number(stf(detail.weight), 1));
	if (detail.term != "")
	{
		string term = UiFill(UiText("items.days"), detail.term);
		if (detail.left != "") term = TouchLib_FillTwo(UiText("items.days_left"), detail.left, detail.term);
		TouchItems_Fact(facts, UiText("items.term"), term);
	}
	at.text = detail.text;
	if (detail.action.label == "") return;
	string reason = detail.action.reason;
	if (reason == "") reason = UiText("items.unavailable");
	bool warn = sti(detail.action.warn);
	string variant = "primary";
	if (warn) variant = "danger";
	object action = {id = "item-equip", label = detail.action.label, variant = variant, hold = warn,
	                 disabled = !sti(detail.action.can), reason = reason, busy = UiPending("item-equip")};
	aref button;
	makearef(button, at.action);
	CopyAttributes(button, &action);
}

void TouchItems_Show(aref game)
{
	DeleteAttribute(&TouchItems_View, "");
	DeleteAttribute(&TouchItems, "listed");
	aref view;
	makearef(view, TouchItems_View);
	view.title = game.title;
	view.headerSection = TouchLib_Section(&TouchItems_Header, "items");
	if (CheckAttribute(game, "map"))
	{
		view.headerSection = "";
		view.map.title = game.map.title;
		view.map.text = game.map.text;
		TouchLib_Copy(view, "map.image", game, "map.image");
		return;
	}
	aref subject, people;
	makearef(subject, view.subject);
	makearef(people, game.people);
	// The header follows the person picked while the game is still to show them.
	int head = sti(game.shown);
	if (sti(game.waiting)) head = sti(game.picked);
	TouchLib_Subject(subject, people, head, sti(TouchItems.open));
	if (game.load != "")
	{
		aref stats;
		makearef(stats, view.stats);
		aref stat = TouchLib_Add(stats, "s");
		stat.icon = "weight";
		stat.value = game.load;
	}
	TouchItems_Slots(game, view);
	aref tabs;
	makearef(tabs, view.tabs);
	for (int n = 0; n < TouchLib_Count(game, "tabs"); n++)
	{
		aref tab = TouchLib_Item(game, "tabs", n);
		aref at = TouchLib_Add(tabs, "t");
		at.id = n;
		at.label = tab.label;
		at.selected = tab.selected;
	}
	TouchItems_Rows(game, view);
	TouchItems_Detail(game, view);
}

void TouchItems_Build()
{
	aref ui = GetEventData();
	if (ui.screen != "items") return;
	if (UiFirst())
	{
		DeleteAttribute(&TouchItems, "");
		TouchItems.filter = -1;
		TouchItems.who = "";
		TouchItems.open = false;
	}
	DeleteAttribute(&TouchItems, "game");
	aref game;
	makearef(game, TouchItems.game);
	TouchAdpItems_State(ui, game);
	// Another person's things: the slot filter was the last person's.
	aref person = TouchLib_Item(game, "people", sti(game.shown));
	if (person.label != TouchItems.who) TouchItems.filter = -1;
	TouchItems.who = person.label;
	TouchItems_Show(game);
	aref people;
	makearef(people, game.people);
	TouchLib_SubjectOn(people, sti(TouchItems.open));
	UiOn("header", "section");
	UiOn("header-back", "");
	UiOn("item-tabs", "select");
	UiOn("items", "row");
	UiOn("item-equip", "");
	for (int k = 0; k < TouchLib_Count(game, "slots"); k++) UiOn("slot-" + k, "");
	// The game's map window is over the screen: back closes it, not the game's interface.
	if (CheckAttribute(game, "map"))
	{
		UiOn("map-close", "");
		UiModal("items-map", "map-close");
	}
	UiShow(&TouchItems_View);
}

void TouchItems_Tap()
{
	aref ui = GetEventData();
	if (ui.screen != "items") return;
	string id = GetEventData(), verb = GetEventData(), value = GetEventData();
	aref game;
	makearef(game, TouchItems.game);
	if (id == "map-close" || (id == "header-back" && CheckAttribute(game, "map")))
	{
		TouchAdpItems_CloseMap(ui);
		UiHold();
		return;
	}
	if (id == "header-back") { TouchAdpItems_Leave(ui); return; }
	if (id == "header" && verb == "section")
	{
		if (!TouchLib_SectionTap(&TouchItems_Header, &TouchItems_View, "items", value)) return;
		TouchAdpItems_Section(ui, value);
		UiHold();
		return;
	}
	int person = TouchLib_SubjectTap(&TouchItems, id, verb, sti(game.shown), TouchLib_Count(game, "people"));
	if (person >= 0)
	{
		// The person picked is named from this frame; the game shows their things a frame later.
		aref people, subject;
		makearef(people, game.people);
		makearef(subject, TouchItems_View.subject);
		TouchLib_Subject(subject, people, person, false);
		UiPredict(&TouchItems_View);
		TouchAdpItems_Person(ui, person);
		UiHold();
		return;
	}
	if (id == "header" || TouchLib_StartsWith(id, "header-subject")) return;
	if (TouchLib_StartsWith(id, "slot-"))
	{
		int slot = TouchLib_TapIndex(TouchLib_Cut(id, 5, strlen(id)), TouchLib_Count(game, "slots"), "the slot");
		if (sti(TouchItems.filter) == slot) { TouchItems.filter = -1; UiRebuild(); return; }
		TouchItems.filter = slot;
		// A slot that holds an item chooses its row: the parchment and its action are the slot's.
		aref held = TouchLib_Item(game, "slots", slot);
		int row = sti(held.row);
		if (row >= 0)
		{
			aref at = TouchLib_Item(game, "rows", row);
			if (!sti(at.selected)) { TouchAdpItems_Select(ui, row); UiHold(); return; }
		}
		UiRebuild();
		return;
	}
	if (id == "item-tabs")
	{
		int tab = TouchLib_TapIndex(value, TouchLib_Count(game, "tabs"), "the category");
		aref chosen = TouchLib_Item(game, "tabs", tab);
		if (sti(chosen.selected) || !sti(chosen.can)) return;
		// A game category replaces the slot filter; the category is drawn chosen from this frame, its list comes with
		// the game's answer.
		TouchItems.filter = -1;
		for (int n = 0; n < TouchLib_Count(&TouchItems_View, "tabs"); n++)
		{
			aref shownTab = TouchLib_Item(&TouchItems_View, "tabs", n);
			shownTab.selected = n == tab;
		}
		UiPredict(&TouchItems_View);
		TouchAdpItems_Tab(ui, tab);
		UiHold();
		return;
	}
	if (id == "items")
	{
		int i = TouchLib_TapIndex(value, TouchLib_Count(&TouchItems, "listed"), "the row");
		aref entry = TouchLib_Item(&TouchItems, "listed", i);
		int k = sti(entry.k);
		aref tapped = TouchLib_Item(game, "rows", k);
		// Choosing a row only describes it; the game's action is its own button.
		if (sti(tapped.selected)) return;
		for (int r = 0; r < TouchLib_Count(&TouchItems_View, "rows"); r++)
		{
			aref shownRow = TouchLib_Item(&TouchItems_View, "rows", r);
			shownRow.selected = r == i;
		}
		UiPredict(&TouchItems_View);
		TouchAdpItems_Select(ui, k);
		UiHold();
		return;
	}
	if (id == "item-equip")
	{
		if (!CheckAttribute(game, "detail.action")) return;
		if (!sti(game.detail.action.can)) return;
		TouchAdpItems_Act(ui);
		UiHold();
	}
}
