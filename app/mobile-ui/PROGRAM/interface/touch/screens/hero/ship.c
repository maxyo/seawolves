// SPDX-License-Identifier: GPL-3.0-only
// The ship, a hero section (pack screen s-ship): the squadron's ships in the header, three tabs of ours over the one
// game window. «Обзор»: the ship's card and the game's figures, the guns row opening the guns' window as a sheet
// (a calibre from the hold fitted on every side, or the guns of a side one arrow press per gun); «Команда»: the crew,
// its quality and morale, food and upkeep, the shares and the morale's raise; «Трюм»: the goods, the open one with the
// game's overboard window as its amount. The game's questions are dialogs with its own words.

#event_handler("UiBuild", "TouchShip_Build");
#event_handler("UiTap", "TouchShip_Tap");

// The adapter's state of the last build (.game), the tab, the squadron's list open, the good whose window is open as
// it was when the window opened (.held: the game takes the amount out of the hold while it is set, so its row may
// leave the list), the good to open once the game closed another's window (.reopen), the rows of the hold
// (.listed.l<i>.key: the good's, "" the open one).
object TouchShip;
object TouchShip_View;
object TouchShip_Header; // the section tapped while the game opens it (TouchLib_Section)

// s: how long a move waits for the game's answer; past it the view shows what the game has.
float TouchShip_Wait = 0.5;

int TouchShip_Percent(int now, int most)
{
	if (most <= 0) return 0;
	int percent = now * 100 / most;
	if (percent < 0) return 0;
	if (percent > 100) return 100;
	return percent;
}

// The figure of the adapter's `key`, absent: an object with no `now`.
aref TouchShip_Figure(aref game, string key)
{
	for (int k = 0; k < TouchLib_Count(game, "figures"); k++)
	{
		aref figure = TouchLib_Item(game, "figures", k);
		if (figure.key == key) return figure;
	}
	aref none;
	makearef(none, TouchShip.none);
	return none;
}

aref TouchShip_Row(aref rows, string icon, string label, string value, string sub)
{
	aref row = TouchLib_Add(rows, "r");
	row.cells.c0.text = label;
	row.cells.c0.icon = icon;
	row.cells.c0.lines = 2;
	if (sub != "") row.cells.c0.sub = sub;
	row.cells.c1.text = value;
	row.cells.c1.bold = true;
	return row;
}

void TouchShip_Card(aref game, aref view)
{
	aref card;
	makearef(card, view.card);
	if (!CheckAttribute(game, "ship"))
	{
		card.title = "";
		view.empty = UiText("ship.no_ship");
		return;
	}
	aref ship;
	makearef(ship, game.ship);
	card.title = ship.name;
	card.sub = ship.type;
	if (ship.rank != "") card.sub = ship.type + " · " + UiText("yard.class") + " " + ship.rank;
	card.captain = UiText("ship.captain") + " " + ship.captain;
	if (sti(ship.companion)) card.captain = card.captain + " · " + UiText("ship.companion");
	card.rename = ship.rename;
	TouchLib_Copy(card, "image", ship, "image");
	string part;
	for (int p = 0; p < 2; p++)
	{
		part = "hull";
		if (p == 1) part = "sails";
		aref figure = TouchShip_Figure(game, part);
		if (CheckAttribute(figure, "now"))
		{
			int percent = TouchShip_Percent(sti(figure.now), sti(figure.most));
			card.(part).value = percent;
			card.(part).text = percent + "%";
		}
	}
	card.crew.value = TouchShip_Percent(sti(game.crew.count), sti(game.crew.full));
	card.crew.text = game.crew.count;
	card.crew.mark = TouchShip_Percent(sti(game.crew.least), sti(game.crew.full));
	if (sti(ship.exchange))
	{
		card.exchange.id = "ship-exchange";
		card.exchange.label = UiText("ship.exchange");
		card.exchange.busy = UiPending("ship-exchange");
		UiOn("ship-exchange", "");
	}
	UiOn("ship-rename", "");
}

void TouchShip_Overview(aref game, aref view)
{
	view.empty = "";
	TouchShip_Card(game, view);
	if (!CheckAttribute(game, "ship")) return;
	aref left, right;
	makearef(left, view.left);
	makearef(right, view.right);
	string of = " " + UiText("common.of") + " ";
	object icons = {speed = "speed", turn = "maneuver", upwind = "upwind", capacity = "capacity"};
	for (int k = 0; k < TouchLib_Count(game, "figures"); k++)
	{
		aref figure = TouchLib_Item(game, "figures", k);
		string key = figure.key;
		if (!CheckAttribute(&icons, key)) continue;
		string value = figure.now;
		if (key == "speed") value += " " + UiText("common.knots_short");
		if (key == "upwind") value += "°";
		else if (sti(figure.most) >= 0) value += of + figure.most;
		if (key == "capacity") value += " " + UiText("transfer.cwt");
		TouchShip_Row(left, icons.(key), figure.label, value, "");
	}
	TouchShip_Row(left, "crew", UiText("ship.min_crew"),
	              game.crew.least + " · " + UiText("ship.full_crew") + " " + game.crew.full, "");
	if (CheckAttribute(game, "guns.text"))
	{
		// "6 lb / 12": the calibre and the count of the fitted guns
		string guns = game.guns.text;
		int slash = findSubStr(guns, "/", 0);
		if (slash >= 0)
			guns = TouchLib_Trim(TouchLib_Cut(guns, slash + 1, strlen(guns))) + " × " + TouchLib_Trim(TouchLib_Cut(guns, 0, slash));
		if (sti(game.guns.can)) guns += "  ›";
		TouchShip_Row(right, "cannon", game.guns.label, guns, UiText("ship.by_sides"));
		UiOn("stats-right", "row");
	}
	if (CheckAttribute(game, "guns.caliber"))
		TouchShip_Row(right, "caliber", UiText("ship.caliber"), UiText("ship.up_to") + " " + game.guns.caliber, "");
	for (int f = 0; f < TouchLib_Count(game, "facts"); f++)
	{
		aref fact = TouchLib_Item(game, "facts", f);
		if (fact.icon == "money") TouchShip_Row(right, "money", UiText("ship.upkeep"), fact.title, UiText("hero.per_month"));
	}
}

void TouchShip_Crew(aref game, aref view)
{
	view.empty = "";
	if (CheckAttribute(game, "ship"))
	{
		aref crew;
		makearef(crew, view.crew);
		int full = sti(game.crew.full);
		crew.count = game.crew.count;
		crew.limits = UiText("common.of") + " " + full + " · " + UiText("ship.min_short") + " " + game.crew.least;
		crew.value = TouchShip_Percent(sti(game.crew.count), full);
		crew.mark = TouchShip_Percent(sti(game.crew.least), full);
		TouchLib_Copy(crew, "groups", game, "quality.groups");
		crew.moraleLabel = UiText("ship.morale");
		crew.morale = game.quality.morale;
		crew.moraleWord = game.quality.moraleWord;
	}
	else view.empty = UiText("ship.no_ship");
	TouchLib_Copy(view, "facts", game, "facts");
	aref actions, from2;
	makearef(actions, view.crewActions);
	if (CheckAttribute(game, "shares"))
	{
		makearef(from2, game.shares);
		TouchLib_Button(actions, "crew-shares", UiText("ship.shares"), "secondary", from2);
	}
	if (CheckAttribute(game, "morale"))
	{
		makearef(from2, game.morale);
		TouchLib_Button(actions, "crew-morale", UiText("ship.raise_morale"), "primary", from2);
	}
}

// A good's pack and its weight, as the hold's columns name them; "" for goods counted one by one.
string TouchShip_Pack(aref game, aref good)
{
	if (good.pack == "" || good.pack == "1") return "";
	return game.heads.pack + " " + good.pack + " · " + game.heads.packWeight + " " + good.packWeight;
}

// The row of the open good: what stays in the hold as its amount.
void TouchShip_Open(aref game, aref rows)
{
	aref held;
	makearef(held, TouchShip.held);
	int total = sti(game.drop.total);
	int dropping = sti(game.drop.dropping);
	aref row = TouchLib_Add(rows, "r");
	row.id = "open";
	row.name = held.label;
	row.meta = held.meta;
	TouchLib_Copy(row, "image", held, "image");
	row.cells.c0 = total;
	row.cells.c1 = held.weight;
	row.selected = true;
	string summary = "";
	if (dropping > 0) summary = "−" + dropping;
	object q = {value = total - dropping, min = 0, max = total, label = UiText("ship.in_hold"), summary = summary,
	            all = false, disabled = !sti(game.drop.can)};
	aref to;
	makearef(to, row.q);
	CopyAttributes(to, &q);
	aref listed;
	makearef(listed, TouchShip.listed);
	aref entry = TouchLib_Add(listed, "l");
	entry.key = "";
}

void TouchShip_Cargo(aref game, aref view)
{
	DeleteAttribute(&TouchShip, "listed");
	bool open = CheckAttribute(game, "drop");
	int k;
	aref good;
	if (!open) DeleteAttribute(&TouchShip, "held");
	if (open && !CheckAttribute(&TouchShip, "held"))
	{
		// The window just opened: its good's row as it is now, before an amount is set.
		for (k = 0; k < TouchLib_Count(game, "goods"); k++)
		{
			good = TouchLib_Item(game, "goods", k);
			if (good.label == game.drop.label)
			{
				object held = {label = good.label, index = k, meta = TouchShip_Pack(game, good), weight = good.weight};
				aref to;
				makearef(to, TouchShip.held);
				CopyAttributes(to, &held);
				TouchLib_Copy(&TouchShip, "held.image", good, "image");
				aref capacity = TouchShip_Figure(game, "capacity");
				if (CheckAttribute(capacity, "now")) TouchShip.held.capacity = capacity.now;
			}
		}
		if (!CheckAttribute(&TouchShip, "held")) UiError("ship: the overboard window is open on " + game.drop.label + ", which the hold does not list");
	}
	view.list.label = UiText("ship.goods");
	view.list.cols.c0.label = game.heads.count;
	view.list.cols.c0.icon = "capacity";
	view.list.cols.c0.w = 88;
	view.list.cols.c1.label = game.heads.weight;
	view.list.cols.c1.icon = "weight";
	view.list.cols.c1.w = 80;
	aref rows;
	makearef(rows, view.list.rows);
	bool listed = false;
	for (k = 0; k < TouchLib_Count(game, "goods"); k++)
	{
		good = TouchLib_Item(game, "goods", k);
		if (open && !listed && k == sti(TouchShip.held.index) && good.label != TouchShip.held.label)
		{
			TouchShip_Open(game, rows);
			listed = true;
		}
		if (open && good.label == TouchShip.held.label)
		{
			TouchShip_Open(game, rows);
			listed = true;
			continue;
		}
		aref row = TouchLib_Add(rows, "r");
		row.id = GetAttributesNum(rows) - 1;
		row.name = good.label;
		row.meta = TouchShip_Pack(game, good);
		TouchLib_Copy(row, "image", good, "image");
		row.cells.c0 = good.count;
		row.cells.c1 = good.weight;
		row.disabled = !open && !sti(good.can);
		row.reason = "";
		aref listed;
		makearef(listed, TouchShip.listed);
		aref entry = TouchLib_Add(listed, "l");
		entry.key = good.key;
	}
	if (open && !listed) TouchShip_Open(game, rows);
	view.empty = "";
	if (TouchLib_Count(game, "goods") == 0 && !open) view.empty = UiText("ship.no_cargo");
	UiOn("goods", "row");
	TouchShip_Load(game, view);
	if (!open) return;
	UiOn("goods", "change");
	UiOn("goods", "commit");
	aref actions, from2;
	makearef(actions, view.cargoActions);
	if (CheckAttribute(game, "drop.mine"))
	{
		makearef(from2, game.drop.mine);
		TouchLib_Button(actions, "goods-mine", from2.label, "secondary", from2);
	}
	int dropping = sti(game.drop.dropping);
	string label = UiText("ship.overboard");
	if (dropping > 0) label += " " + dropping;
	makearef(from2, game.drop.ok);
	aref drop = TouchLib_Button(actions, "goods-drop", label, "danger", from2);
	drop.hold = true;
	drop.disabled = !sti(game.drop.ok.can) || dropping <= 0;
	// The window takes the back key: it closes, the ship screen stays.
	UiBackTo("window-close");
}

// The deadweight, with what the hold had when the open good's window opened.
void TouchShip_Load(aref game, aref view)
{
	aref capacity = TouchShip_Figure(game, "capacity");
	if (!CheckAttribute(capacity, "now")) return;
	int now = sti(capacity.now);
	int most = sti(capacity.most);
	view.load.value = TouchShip_Percent(now, most);
	string text = now + " " + UiText("common.of") + " " + most + " " + UiText("transfer.cwt");
	if (CheckAttribute(game, "drop") && sti(game.drop.dropping) > 0 && CheckAttribute(&TouchShip, "held.capacity"))
	{
		int before = sti(TouchShip.held.capacity);
		view.load.value = TouchShip_Percent(before, most);
		view.load.after = TouchShip_Percent(now, most);
		text = before + " › " + text;
	}
	view.load.text = "";
	if (most > 0) view.load.text = text;
}

// The game's question: its caption and text; the shares as their lines ("label: value"), in the groups the game
// parts with its rule of asterisks.
void TouchShip_Dialog(aref game, aref view)
{
	if (!CheckAttribute(game, "question")) return;
	aref question;
	makearef(question, game.question);
	aref dialog;
	makearef(dialog, view.dialog);
	dialog.kind = question.kind;
	dialog.title = question.title;
	dialog.text = question.text;
	if (question.kind == "shares")
	{
		dialog.title = UiText("ship.shares");
		aref groups;
		makearef(groups, dialog.groups);
		aref group = TouchLib_Add(groups, "g");
		aref items;
		makearef(items, group.items);
		for (int k = 0; k < TouchLib_Count(question, "lines"); k++)
		{
			aref line = TouchLib_Item(question, "lines", k);
			string text = TouchLib_Trim(line.text);
			if (TouchLib_StartsWith(text, "***"))
			{
				if (GetAttributesNum(items) > 0)
				{
					group = TouchLib_Add(groups, "g");
					makearef(items, group.items);
				}
				continue;
			}
			aref item = TouchLib_Add(items, "i");
			item.k = text;
			item.v = "";
			int colon = findSubStr(text, ":", 0);
			if (colon > 0)
			{
				item.k = TouchLib_Trim(TouchLib_Cut(text, 0, colon));
				item.v = TouchLib_AfterColon(text);
			}
		}
		if (GetAttributesNum(items) == 0) DeleteAttribute(groups, "g" + (GetAttributesNum(groups) - 1));
	}
	aref actions;
	makearef(actions, dialog.actions);
	aref close = TouchLib_Add(actions, "a");
	close.id = "dialog-close";
	close.label = UiText("common.cancel");
	close.variant = "quiet";
	aref accept;
	makearef(accept, question.accept);
	if (accept.label != "") TouchLib_Button(actions, "dialog-accept", accept.label, "primary", accept);
	UiOn("dialog-close", "");
	UiModal("dialog", "dialog-close");
}

void TouchShip_Sheet(aref game, aref view)
{
	if (!CheckAttribute(game, "cannons")) return;
	aref cannons;
	makearef(cannons, game.cannons);
	aref sheet;
	makearef(sheet, view.sheet);
	sheet.title = cannons.title;
	string limit = "";
	if (CheckAttribute(game, "guns.caliber"))
		limit = UiText("ship.caliber") + " " + UiText("ship.up_to") + " " + game.guns.caliber;
	sheet.subtitle = limit;
	if (cannons.fitted != "")
	{
		sheet.subtitle = UiText("ship.guns_fitted") + " " + cannons.fitted;
		if (limit != "") sheet.subtitle = sheet.subtitle + " · " + limit;
	}
	sheet.calibres.label = cannons.stock.label;
	sheet.calibres.count = cannons.stock.count;
	sheet.calibres.empty = UiText("ship.no_spare_guns");
	aref rows;
	makearef(rows, sheet.calibres.rows);
	// «Fit all» fits the calibre picked in the game's table, else the fitted one again.
	string picked = cannons.fitted;
	for (int k = 0; k < TouchLib_Count(cannons, "calibres"); k++)
	{
		aref calibre = TouchLib_Item(cannons, "calibres", k);
		if (sti(calibre.selected)) picked = calibre.label;
		aref row = TouchLib_Add(rows, "r");
		row.cells.c0.text = calibre.label;
		row.cells.c0.sub = calibre.sub;
		TouchLib_Copy(row, "cells.c0.image", calibre, "image");
		row.cells.c1 = calibre.count;
		row.selected = calibre.selected;
	}
	UiOn("cannons-stock", "row");
	aref sides;
	makearef(sides, sheet.sides);
	for (int s = 0; s < TouchLib_Count(cannons, "sides"); s++)
	{
		aref side = TouchLib_Item(cannons, "sides", s);
		int now = sti(side.now);
		int most = sti(side.most);
		int ports = most;
		if (now > ports) ports = now;
		aref at = TouchLib_Add(sides, "s");
		at.id = "guns-" + side.key;
		object q = {value = now, min = 0, max = ports, label = UiText("yard." + side.key), all = false, dense = true,
		            disabled = most <= 0 || !sti(side.can)};
		q.steps.s0 = 1;
		aref to;
		makearef(to, at.q);
		CopyAttributes(to, &q);
		UiOn(at.id, "change");
		UiOn(at.id, "commit");
	}
	aref actions, from2;
	makearef(actions, sheet.actions);
	if (CheckAttribute(cannons, "fit"))
	{
		makearef(from2, cannons.fit);
		aref fit = TouchLib_Button(actions, "cannons-fit", from2.label, "secondary", from2);
		fit.sub = picked;
	}
	if (CheckAttribute(cannons, "remove"))
	{
		makearef(from2, cannons.remove);
		TouchLib_Button(actions, "cannons-remove", from2.label, "secondary", from2);
	}
	if (CheckAttribute(cannons, "ok"))
	{
		makearef(from2, cannons.ok);
		TouchLib_Button(actions, "cannons-ok", from2.label, "primary", from2);
	}
	UiOn("sheet-close", "");
	UiModal("sheet", "sheet-close");
}

// The squadron's ships in the header, none for a ship alone.
void TouchShip_Subject(aref game, aref view, int shown, bool open)
{
	aref ships;
	makearef(ships, game.ships);
	if (GetAttributesNum(ships) < 2) return;
	object people;
	for (int k = 0; k < GetAttributesNum(ships); k++)
	{
		aref ship = GetAttributeN(ships, k);
		aref person = TouchLib_Add(&people, "p");
		person.label = ship.label;
		person.meta = "";
		person.image = "";
	}
	aref subject;
	makearef(subject, view.subject);
	TouchLib_Subject(subject, &people, shown, open);
	subject.title = UiText("ship.squadron");
	for (k = 0; k < TouchLib_Count(subject, "rows"); k++)
	{
		aref row = TouchLib_Item(subject, "rows", k);
		row.icon = "ship";
	}
	TouchLib_SubjectOn(&people, open);
}

int TouchShip_Shown(aref game)
{
	int k = TouchLib_Selected(game, "ships");
	if (k < 0) return 0;
	return k;
}

bool TouchShip_Window(aref game)
{
	return CheckAttribute(game, "drop") || CheckAttribute(game, "question") || CheckAttribute(game, "cannons");
}

void TouchShip_Build()
{
	aref ui = GetEventData();
	if (ui.screen != "ship") return;
	if (UiFirst())
	{
		DeleteAttribute(&TouchShip, "");
		TouchShip.tab = "overview";
		TouchShip.open = false;
	}
	DeleteAttribute(&TouchShip, "game");
	aref game;
	makearef(game, TouchShip.game);
	TouchAdpShip_State(ui, game);
	// A squadron ship tapped on the world map opened the screen: the game shows that one.
	object asked;
	if (UiTake("ship", &asked))
	{
		int position = sti(asked.position);
		int count = TouchLib_Count(game, "ships");
		if (position < 0 || (position >= count && !(count == 0 && position == 0)))
			UiError("ship: the HUD asks for squadron ship " + position + " of " + count);
		if (count > 0)
		{
			aref wanted = TouchLib_Item(game, "ships", position);
			if (!sti(wanted.selected))
			{
				TouchAdpShip_Ship(ui, position);
				UiHold(TouchShip_Wait);
			}
		}
	}
	// A good tapped while another's window was open: it opens once the game closed that one.
	if (CheckAttribute(&TouchShip, "reopen") && !CheckAttribute(game, "drop"))
	{
		string key = TouchShip.reopen;
		DeleteAttribute(&TouchShip, "reopen");
		for (int g = 0; g < TouchLib_Count(game, "goods"); g++)
		{
			aref good = TouchLib_Item(game, "goods", g);
			if (good.key == key && sti(good.can))
			{
				TouchAdpShip_Good(ui, g);
				UiHold(TouchShip_Wait);
			}
		}
	}
	DeleteAttribute(&TouchShip_View, "");
	aref view;
	makearef(view, TouchShip_View);
	view.title = game.title;
	TouchLib_SectionHead(view, &TouchShip_Header, "ship", sti(game.sections));
	string tab = TouchShip.tab;
	aref tabs;
	makearef(tabs, view.tabs);
	object ids = {t0 = "overview", t1 = "crew", t2 = "cargo"};
	for (int t = 0; t < 3; t++)
	{
		string id = GetAttributeValue(GetAttributeN(&ids, t));
		aref at = TouchLib_Add(tabs, "t");
		at.id = id;
		at.label = UiText("ship.tab_" + id);
		at.selected = id == tab;
	}
	view.tab = tab;
	UiOn("header-tabs", "select");
	TouchShip_Subject(game, view, TouchShip_Shown(game), sti(TouchShip.open));
	if (tab == "overview") TouchShip_Overview(game, view);
	if (tab == "crew") TouchShip_Crew(game, view);
	if (tab == "cargo") TouchShip_Cargo(game, view);
	TouchShip_Dialog(game, view);
	TouchShip_Sheet(game, view);
	UiOn("header-back", "");
	UiOn("window-close", "");
	UiShow(view);
}

void TouchShip_Tap()
{
	aref ui = GetEventData();
	if (ui.screen != "ship") return;
	string id = GetEventData(), verb = GetEventData(), value = GetEventData();
	aref game;
	makearef(game, TouchShip.game);
	bool window = TouchShip_Window(game);
	if (id == "header" && verb == "section")
	{
		if (!TouchLib_SectionTap(&TouchShip_Header, &TouchShip_View, "ship", value)) return;
		TouchAdpShip_Section(ui, value);
		UiHold(TouchShip_Wait);
		return;
	}
	if (id == "header-back" || id == "window-close" || id == "dialog-close" || id == "sheet-close")
	{
		if (window) { TouchAdpShip_Close(ui); UiHold(TouchShip_Wait); return; }
		if (id == "header-back") TouchAdpShip_Leave(ui);
		return;
	}
	if (id == "header-tabs")
	{
		if (value == TouchShip.tab) return;
		TouchShip.tab = value;
		if (window) { TouchAdpShip_Close(ui); UiHold(TouchShip_Wait); return; }
		UiRebuild();
		return;
	}
	int ship = TouchLib_SubjectTap(&TouchShip, id, verb, TouchShip_Shown(game), TouchLib_Count(game, "ships"));
	if (ship >= 0)
	{
		if (window) { TouchAdpShip_Close(ui); UiHold(TouchShip_Wait); return; }
		TouchAdpShip_Ship(ui, ship);
		UiHold(TouchShip_Wait);
		return;
	}
	if (id == "header" || TouchLib_StartsWith(id, "header-subject")) return;
	if (id == "ship-rename") { TouchAdpShip_Rename(ui, UiText("ship.rename")); return; }
	if (id == "stats-right")
	{
		if (value != "0" || !sti(game.guns.can)) return;
		TouchAdpShip_Guns(ui);
		UiHold(TouchShip_Wait);
		return;
	}
	if (id == "goods" && verb == "row")
	{
		int i = TouchLib_TapIndex(value, TouchLib_Count(&TouchShip, "listed"), "the good");
		aref entry = TouchLib_Item(&TouchShip, "listed", i);
		if (window)
		{
			// The window holds the list: another good opens once the game closed it.
			if (entry.key != "") TouchShip.reopen = entry.key;
			TouchAdpShip_Close(ui);
			UiHold(TouchShip_Wait);
			return;
		}
		for (int g = 0; g < TouchLib_Count(game, "goods"); g++)
		{
			aref good = TouchLib_Item(game, "goods", g);
			if (good.key == entry.key && sti(good.can)) { TouchAdpShip_Good(ui, g); UiHold(TouchShip_Wait); }
		}
		return;
	}
	if (id == "goods")
	{
		// The amount control shows what stays in the hold; the game's field takes what goes.
		int total = sti(game.drop.total);
		int keep = UiAmount(value, "the goods kept");
		if (keep < 0) keep = 0;
		if (keep > total) keep = total;
		if (total - keep == sti(game.drop.dropping)) { UiRebuild(); return; }
		TouchAdpShip_Drop(ui, total - keep);
		UiHold(TouchShip_Wait);
		return;
	}
	if (TouchLib_StartsWith(id, "guns-"))
	{
		string key = TouchLib_Cut(id, 5, strlen(id));
		for (int s = 0; s < TouchLib_Count(game, "cannons.sides"); s++)
		{
			aref side = TouchLib_Item(game, "cannons.sides", s);
			if (side.key == key)
			{
				int guns = UiAmount(value, "the guns of a side");
				if (guns == sti(side.now)) { UiRebuild(); return; }
				TouchAdpShip_Side(ui, s, guns);
				UiHold(TouchShip_Wait);
				return;
			}
		}
		UiError("ship: no side " + key);
	}
	if (id == "cannons-stock")
	{
		int c = TouchLib_TapIndex(value, TouchLib_Count(game, "cannons.calibres"), "the calibre");
		aref calibre = TouchLib_Item(game, "cannons.calibres", c);
		if (sti(calibre.selected) || !sti(calibre.can)) return;
		TouchAdpShip_Calibre(ui, c);
		UiHold(TouchShip_Wait);
		return;
	}
	if (id == "ship-exchange") TouchAdpShip_Exchange(ui);
	else if (id == "crew-shares") TouchAdpShip_Shares(ui);
	else if (id == "crew-morale") TouchAdpShip_Morale(ui);
	else if (id == "dialog-accept") TouchAdpShip_Accept(ui);
	else if (id == "goods-drop") TouchAdpShip_Overboard(ui);
	else if (id == "goods-mine") TouchAdpShip_Mine(ui);
	else if (id == "cannons-fit") TouchAdpShip_Fit(ui);
	else if (id == "cannons-remove") TouchAdpShip_RemoveGuns(ui);
	else if (id == "cannons-ok") TouchAdpShip_GunsOk(ui);
	else UiError("ship: no tap of " + id);
	UiHold(TouchShip_Wait);
}
