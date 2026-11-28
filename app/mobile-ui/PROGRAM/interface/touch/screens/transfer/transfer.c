// SPDX-License-Identifier: GPL-3.0-only
// The exchange with another ship (pack screen s-transfer): the cargo, crew and ships tabs (the ships: both ships'
// figures side by side, the other's plainly better ones green, worse red), the other ship's card in the dock, the
// game's questions as a modal and the captain choice as a side sheet. An amount is set on the screen first and the
// bar's «Взять / Отдать N» sends it as the game's arrows; the build then shows where the game stopped them. A
// dangerous answer is held, never asked twice; a question whose way out decides something is answered by its
// buttons only.

#event_handler("UiBuild", "TouchTransfer_Build");
#event_handler("UiTap", "TouchTransfer_Tap");

// The screen's state while its window is open: the ships tab chosen (.ships), the defectors seen (.hired), the goods'
// order (.sort), the amount set and not yet sent (.draft: {key, amount} of a cargo row or key "crew"), the amount sent
// and not yet answered (.sent: {key, from, asked}) and the adapter's state of the last build in .game.
object TouchTransfer;
object TouchTransfer_View;

// The sort keys of the goods' columns.
string TouchTransfer_Keys[2] = {"own", "other"};

void TouchTransfer_Build()
{
	aref ui = GetEventData();
	if (ui.screen != "transfer") return;
	if (UiFirst() || !CheckAttribute(&TouchTransfer, "ships"))
	{
		object fresh = {ships = false, hired = false};
		fresh.sort.by = -1;
		fresh.sort.descending = true;
		fresh.draft.key = "";
		fresh.draft.amount = 0;
		fresh.sent.key = "";
		DeleteAttribute(&TouchTransfer, "");
		CopyAttributes(&TouchTransfer, &fresh);
	}
	aref game;
	makearef(game, TouchTransfer.game);
	TouchAdpTransfer_State(ui, game);
	TouchTransfer_Stopped(game);
	aref view;
	makearef(view, TouchTransfer_View);
	DeleteAttribute(view, "");
	view.title = game.title;
	if (view.title == "") view.title = UiText(ui.look.title);
	bool crew = CheckAttribute(game, "crew");
	bool figures = TouchLib_Count(game, "figures") > 0;
	if (!figures) TouchTransfer.ships = false;
	bool ships = sti(TouchTransfer.ships) && !crew;
	TouchTransfer_Tabs(game, view, crew, figures);
	TouchTransfer_Stats(game, view);
	TouchTransfer_Cargo(game, view);
	view.ships = "";
	if (ships) TouchTransfer_Ships(game, view);
	view.prize = "";
	if (figures) TouchTransfer_Prize(game, view);
	view.crew = "";
	if (crew) TouchTransfer_Crew(game, view);
	view.actions = "";
	aref buttons;
	makearef(buttons, game.buttons);
	TouchLib_Actions(view, buttons);
	TouchTransfer_Primary(game, view, crew);
	view.modal = "";
	view.sheet = "";
	if (CheckAttribute(game, "question")) TouchTransfer_Question(game, view);
	else
	{
		if (CheckAttribute(game, "captains")) TouchTransfer_Captains(game, view);
	}
	UiOn("header-back", "");
	UiShow(view);
}

// Cargo, the crew (the game's crew window) and the ships (our view of the two figure tables the game always sends).
void TouchTransfer_Tabs(aref game, aref view, bool crew, bool figures)
{
	view.tabs = "";
	if (!CheckAttribute(game, "crewTab") && !figures) return;
	aref tabs;
	makearef(tabs, view.tabs);
	bool ships = sti(TouchTransfer.ships);
	TouchTransfer_Tab(tabs, "cargo", "transfer.cargo", !crew && !ships);
	if (CheckAttribute(game, "crewTab"))
	{
		aref at = TouchTransfer_Tab(tabs, "crew", "transfer.crew", crew && !ships);
		at.disabled = !sti(game.crewTab.can);
		at.reason = game.crewTab.reason;
	}
	if (figures) TouchTransfer_Tab(tabs, "ships", "transfer.ships", ships);
	UiOn("header", "tab");
}

aref TouchTransfer_Tab(aref tabs, string id, string label, bool selected)
{
	aref at = TouchLib_Add(tabs, "t");
	at.id = id;
	at.label = UiText(label);
	at.selected = selected;
	return at;
}

// Each ship's free room, or the other ship left without a captain.
void TouchTransfer_Stats(aref game, aref view)
{
	aref stats;
	makearef(stats, view.stats);
	if (sti(game.ours.room) >= 0)
		TouchLib_Stat(stats, game.ours.name, game.ours.room + " " + UiText("transfer.free"), "");
	if (sti(game.theirs.vacant))
	{
		aref at = TouchLib_Stat(stats, game.theirs.name, UiText("transfer.no_captain"), "");
		at.tone = "danger";
	}
	else
	{
		if (sti(game.theirs.room) >= 0)
			TouchLib_Stat(stats, game.theirs.name, game.theirs.room + " " + UiText("transfer.free"), "");
	}
	if (GetAttributesNum(stats) == 0) view.stats = "";
}

// The cargo of the two holds, the columns named by the ships and sortable; the selected row's amount in the dock is
// set here and sent by the bar's button as the game's arrows, each way while the game shows that arrow.
void TouchTransfer_Cargo(aref game, aref view)
{
	aref list;
	makearef(list, view.list);
	list.label = game.cargo.headers.name;
	aref sorting;
	makearef(sorting, TouchTransfer.sort);
	TouchLib_Column(list, game.ours.name, "capacity", "brass", sorting);
	TouchLib_Column(list, game.theirs.name, "ship", "brass", sorting);
	list.quick = "hand";
	aref rows;
	makearef(rows, list.rows);
	view.deal = "";
	view.dealing = false;
	string key = "";
	if (sti(sorting.by) >= 0) key = TouchTransfer_Keys[sti(sorting.by)];
	object order;
	aref cargo;
	makearef(cargo, game.cargo.rows);
	TouchLib_Order(cargo, key, sti(sorting.descending), &order);
	for (int n = 0; n < GetAttributesNum(&order); n++)
	{
		aref row = GetAttributeN(&order, n);
		aref at = TouchLib_Add(rows, "r");
		bool selected = sti(row.selected);
		at.id = row.key;
		at.name = row.name;
		aref image, from;
		makearef(image, at.image);
		makearef(from, row.image);
		CopyAttributes(image, from);
		at.cells.c0 = row.ownText;
		at.cells.c1 = row.otherText;
		at.selected = selected;
		at.disabled = !sti(row.can);
		at.reason = row.reason;
		at.quick = sti(row.other) > 0 && sti(row.can);
		if (selected) at.quick = sti(row.other) > 0 && sti(game.cargo.move.take);
		if (selected) TouchTransfer_Deal(game, row, at, view);
	}
	if (GetAttributesNum(rows) == 0) list.rows = "";
	UiOn("goods", "sort");
	UiOn("goods", "row");
	UiOn("goods", "quick");
	UiOn("goods", "change");
	UiOn("goods", "commit");
}

// The selected row's amount: ours, from none (the arrow giving shown) to both holds (the arrow taking shown), in the
// game's packs; what a pack weighs and how many pieces it is under its name.
void TouchTransfer_Deal(aref game, aref row, aref at, aref view)
{
	view.dealing = true;
	int own = sti(row.own);
	int pack = sti(row.pack);
	object deal = {value = own, min = own, max = own, zones = true, origin = own, lessLabel = UiText("trade.give"),
	               moreLabel = UiText("trade.take"), label = game.ours.name, summary = "-", disabled = false};
	if (sti(game.cargo.move.give)) deal.min = 0;
	if (sti(game.cargo.move.take)) deal.max = own + sti(row.other);
	deal.disabled = deal.min == deal.max;
	deal.value = TouchTransfer_Drafted(row.key, own);
	deal.steps.s0 = pack;
	deal.steps.s1 = pack * 10;
	deal.steps.s2 = pack * 100;
	aref to;
	makearef(to, view.deal);
	CopyAttributes(to, &deal);
	aref facts;
	makearef(facts, at.facts);
	if (row.weight == "") return;
	TouchLib_Fact(facts, "weight", row.weight, "");
	if (pack > 1) TouchLib_Fact(facts, "", UiFill(UiText("trade.per_units"), row.pack), "dim");
}

// The two ships row by row; the other's figure toned where a larger last number is plainly better.
void TouchTransfer_Ships(aref game, aref view)
{
	object icons = {Hull = "hull", Sails = "sail-full", Speed = "speed", Maneuver = "maneuver", AgainstWind = "upwind",
	                Capacity = "capacity", Crew = "crew", sCannons = "caliber", CannonType = "cannon"};
	object larger = {Hull = true, Speed = true, Maneuver = true, Capacity = true, Crew = true};
	view.ships.ours.title = game.ours.name;
	view.ships.ours.sub = TouchTransfer_Kind(game.ours.type, game.ours.rank);
	view.ships.theirs.title = game.theirs.name;
	view.ships.theirs.sub = TouchTransfer_Kind(game.theirs.type, game.theirs.rank);
	aref rows;
	makearef(rows, view.ships.rows);
	for (int k = 0; k < TouchLib_Count(game, "figures"); k++)
	{
		aref figure = TouchLib_Item(game, "figures", k);
		string key = figure.key;
		aref at = TouchLib_Add(rows, "r");
		at.k = figure.label;
		at.a = figure.ours;
		at.b = figure.theirs;
		at.icon = "ship";
		if (CheckAttribute(&icons, key)) at.icon = icons.(key);
		if (!CheckAttribute(&larger, key)) continue;
		float ours = TouchTransfer_Last(figure.ours);
		float theirs = TouchTransfer_Last(figure.theirs);
		if (ours < 0.0 || theirs < 0.0 || ours == theirs) continue;
		at.tone = "danger";
		if (theirs > ours) at.tone = "ok";
	}
}

// "Бриг · класс 4"
string TouchTransfer_Kind(string type, string rank)
{
	if (rank == "") return type;
	string kind = type;
	if (kind != "") kind += " · ";
	return kind + UiText("yard.class") + " " + rank;
}

// The last number of a figure ("610 / 610": 610, "14.59", "40 : 9 / 40": 40); -1 without one.
float TouchTransfer_Last(string text)
{
	int end = strlen(text);
	while (end > 0 && !TouchLib_Digit(text, end - 1)) end--;
	if (end == 0) return -1.0;
	int begin = end - 1;
	while (begin > 0 && (TouchLib_Digit(text, begin - 1) || strcut(text, begin - 1, begin - 1) == ".")) begin--;
	return UiNumber(TouchLib_Cut(text, begin, end), "a ship's figure");
}

// The other ship's card: what it is, who commands it, its hull and sails as a share of whole and its crew.
void TouchTransfer_Prize(aref game, aref view)
{
	aref prize;
	makearef(prize, view.prize);
	prize.title = game.theirs.name;
	prize.sub = TouchTransfer_Kind(game.theirs.type, game.theirs.rank);
	prize.image = "";
	TouchLib_Copy(prize, "image", game, "theirs.image");
	aref facts;
	makearef(facts, prize.facts);
	aref at;
	if (game.theirs.captain != "")
	{
		at = TouchLib_Add(facts, "f");
		at.k = UiText("transfer.captain");
		at.v = game.theirs.captain;
	}
	for (int k = 0; k < TouchLib_Count(game, "figures"); k++)
	{
		aref figure = TouchLib_Item(game, "figures", k);
		string key = figure.key;
		if (key != "Hull" && key != "Sails" && key != "Crew") continue;
		float whole = TouchTransfer_Last(figure.theirs);
		if (whole <= 0.0) continue;
		int now = UiAmount(figure.theirs, "the other ship's " + key);
		int percent = makeint(now * 100.0 / whole + 0.5);
		at = TouchLib_Add(facts, "f");
		at.k = figure.label;
		at.v = percent + " %";
		if (key == "Crew") at.v = now + " / " + makeint(whole);
		at.tone = "";
		if (percent < 50) at.tone = "danger";
	}
}

// Both crews: our number moves by the game's arrows, a man a press, and stops where the game stops them; the marks
// are each ship's least crew (theirs where what we leave them is it).
void TouchTransfer_Crew(aref game, aref view)
{
	aref crew;
	makearef(crew, view.crew);
	int ours = sti(game.crew.ours);
	int theirs = sti(game.crew.theirs);
	int minOurs = sti(game.crew.minOurs);
	int minTheirs = sti(game.crew.minTheirs);
	crew.label = UiText("transfer.crew");
	crew.cols.c0.label = game.ours.name;
	crew.cols.c0.icon = "hero";
	crew.cols.c1.label = game.theirs.name;
	crew.cols.c1.icon = "exchange";
	aref row;
	makearef(row, crew.rows.r0);
	row.id = "0";
	row.name = UiText("transfer.sailors");
	row.image.kind = "icon";
	row.image.icon = "crew";
	TouchTransfer_Men(row, "c0", ours, minOurs);
	TouchTransfer_Men(row, "c1", theirs, minTheirs);
	row.selected = true;
	object q = {value = TouchTransfer_Drafted("crew", ours), min = 0, max = ours + theirs, zones = true, origin = ours,
	            lessLabel = UiText("trade.give"), moreLabel = UiText("trade.take"), label = game.ours.name,
	            unit = UiText("transfer.men"), summary = "-",
	            disabled = !sti(game.crew.take) && !sti(game.crew.give)};
	q.steps.s0 = 1;
	q.steps.s1 = 10;
	aref at;
	makearef(at, row.q);
	CopyAttributes(at, &q);
	aref marks;
	makearef(marks, at.marks);
	TouchTransfer_Mark(marks, minOurs, minOurs, game.ours.name);
	TouchTransfer_Mark(marks, ours + theirs - minTheirs, minTheirs, game.theirs.name);
	if (GetAttributesNum(marks) == 0) at.marks = "";
	TouchTransfer_Side(crew, game.ours.name, game.crew.morale.ours, "ourFacts", game);
	TouchTransfer_Side(crew, game.theirs.name, game.crew.morale.theirs, "theirFacts", game);
	UiOn("crew", "change");
	UiOn("crew", "commit");
	UiOn("crew", "row");
}

void TouchTransfer_Men(aref row, string cell, int men, int least)
{
	row.cells.(cell).text = men;
	if (least > 0) row.cells.(cell).sub = UiFill(UiText("transfer.min_mark"), "" + least);
}

// A ship's least crew at `at` of our crew's scale, named by its ship: the scale is ours.
void TouchTransfer_Mark(aref marks, int at, int least, string ship)
{
	if (least <= 0 || at < 0) return;
	aref mark = TouchLib_Add(marks, "m");
	mark.at = at;
	mark.label = UiFill(UiText("transfer.min_mark"), "" + least);
	if (ship != "") mark.label = ship + " " + mark.label;
}

void TouchTransfer_Side(aref crew, string name, string morale, string facts, aref game)
{
	aref sides;
	makearef(sides, crew.sides);
	aref side = TouchLib_Add(sides, "s");
	side.name = name;
	side.morale = morale;
	side.facts = "";
	aref list;
	makearef(list, side.facts);
	for (int k = 0; k < TouchLib_Count(game, "crew." + facts); k++)
	{
		aref fact = TouchLib_Item(game, "crew." + facts, k);
		aref at = TouchLib_Add(list, "f");
		at.k = fact.label;
		at.v = fact.value;
	}
}

// The amount set for `key` (a cargo row's or "crew"), else what the game holds now (`now`).
int TouchTransfer_Drafted(string key, int now)
{
	if (TouchTransfer.draft.key != key) return now;
	return sti(TouchTransfer.draft.amount);
}

// The amount set and not sent: what the game holds now for its row (the open row's or the crew), else none (-1, the
// draft dropped: its row is no longer the open one or the game holds that amount already).
int TouchTransfer_Pending(aref game, bool crew)
{
	string key = TouchTransfer.draft.key;
	int now = -1;
	if (crew && key == "crew") now = sti(game.crew.ours);
	int open = TouchLib_Selected(game, "cargo.rows");
	if (!crew && open >= 0)
	{
		aref row = TouchLib_Item(game, "cargo.rows", open);
		if (row.key == key) now = sti(row.own);
	}
	if (now < 0 || now == sti(TouchTransfer.draft.amount))
	{
		TouchTransfer.draft.key = "";
		return -1;
	}
	return now;
}

// The bar: «Взять / Отдать N» of the amount set, else «Перегрузить всё» of the cargo while the other hold has
// something.
void TouchTransfer_Primary(aref game, aref view, bool crew)
{
	view.primary = "";
	int now = TouchTransfer_Pending(game, crew);
	if (now >= 0)
	{
		int change = sti(TouchTransfer.draft.amount) - now;
		object send = {label = UiText("trade.take") + " " + abs(change), sub = "", disabled = false, reason = "",
		               busy = UiPending("primary")};
		if (change < 0) send.label = UiText("trade.give") + " " + abs(change);
		if (crew) send.sub = UiText("transfer.men");
		aref to;
		makearef(to, view.primary);
		CopyAttributes(to, &send);
		UiOn("primary", "");
		return;
	}
	if (crew || !CheckAttribute(game, "takeAll")) return;
	bool there = false;
	for (int k = 0; k < TouchLib_Count(game, "cargo.rows"); k++)
	{
		aref row = TouchLib_Item(game, "cargo.rows", k);
		if (sti(row.other) > 0) there = true;
	}
	object all = {label = UiText("transfer.take_all"), sub = "", disabled = !sti(game.takeAll.can) || !there,
	              reason = game.takeAll.reason, busy = UiPending("primary")};
	aref at;
	makearef(at, view.primary);
	CopyAttributes(at, &all);
	UiOn("primary", "");
}

// The answer that leaves things as they are, by the question's kind ("" none: every answer decides something).
string TouchTransfer_Neutral(string kind)
{
	if (kind == "captain") return "cancel";
	if (kind == "ship" || kind == "ask") return "no";
	if (kind == "message") return "ok";
	return "";
}

// The way out of a question: its neutral answer, else the one deciding least (the defectors refused, the prisoners
// let go).
int TouchTransfer_WayOut(aref question)
{
	string out = TouchTransfer_Neutral(question.kind);
	if (question.kind == "hire") out = "none";
	if (question.kind == "crew") out = "free";
	for (int k = 0; k < TouchLib_Count(question, "answers"); k++)
	{
		aref answer = TouchLib_Item(question, "answers", k);
		if (answer.key == out) return k;
	}
	return -1;
}

// The game's question as a modal: a dangerous answer held, the defectors' number as the window's amount.
void TouchTransfer_Question(aref game, aref view)
{
	aref question;
	makearef(question, game.question);
	string kind = question.kind;
	aref modal;
	makearef(modal, view.modal);
	modal.title = question.title;
	modal.text = question.text;
	if (kind == "ship") modal.title = "«" + game.theirs.name + "»: " + UiText("transfer.fate");
	if (kind == "hire")
	{
		TouchTransfer.hired = true;
		modal.step = "1/2";
	}
	if (kind == "crew" && sti(TouchTransfer.hired)) modal.step = "2/2";
	TouchLib_Copy(modal, "image", question, "image");
	aref actions;
	makearef(actions, modal.actions);
	for (int k = 0; k < TouchLib_Count(question, "answers"); k++)
		TouchTransfer_Answer(question, TouchLib_Item(question, "answers", k), k, TouchLib_Add(actions, "a"));
	if (kind == "hire") TouchTransfer_Hire(question, modal);
	string cancel = "";
	int out = TouchTransfer_WayOut(question);
	if (out >= 0 && TouchTransfer_Neutral(kind) != "") cancel = "answer-" + out;
	string window = "modal";
	if (kind == "hire") window = "amount";
	UiModal(window, cancel);
}

void TouchTransfer_Answer(aref question, aref answer, int k, aref at)
{
	string key = answer.key;
	string kind = question.kind;
	object action = {id = "answer-" + k, label = answer.label, variant = "secondary", hold = false,
	                 disabled = !sti(answer.can), reason = answer.reason, busy = UiPending("answer-" + k)};
	if (key == "kill")
	{
		action.variant = "danger";
		action.hold = true;
	}
	if (key == "free" || key == "ok") action.variant = "primary";
	if (key == "prison" && kind == "captain") action.variant = "primary";
	if (key == "cancel" || key == "none") action.variant = "quiet";
	if (key == "none") action.label = UiText("transfer.nobody");
	if (kind == "ask")
	{
		if (key == "no") action.variant = "quiet";
		if (key == "yes") action.variant = "primary";
	}
	if (kind == "ship")
	{
		if (key == "no")
		{
			action.label = UiText("transfer.keep");
			action.variant = "primary";
		}
		if (key == "yes" && question.leave == "sink")
		{
			action.label = UiText("transfer.sink");
			action.variant = "danger";
			action.hold = true;
		}
		if (key == "yes" && question.leave == "release") action.label = UiText("transfer.release");
	}
	CopyAttributes(at, &action);
	UiOn(action.id, "");
}

// The defectors: the men the game has taken so far of everyone willing; «Взять N» with our crew before and with them.
void TouchTransfer_Hire(aref question, aref modal)
{
	int men = sti(question.men);
	int crew = sti(question.crew);
	int most = sti(question.most);
	for (int k = 0; k < TouchLib_Count(modal, "actions"); k++)
	{
		aref action = TouchLib_Item(modal, "actions", k);
		aref answer = TouchLib_Item(question, "answers", k);
		if (answer.key != "ok") continue;
		action.label = UiText("transfer.take") + " " + men;
		action.sub = UiText("transfer.crew") + " " + (crew - men) + " › " + crew;
		action.disabled = !sti(answer.can) || men == 0;
	}
	object q = {value = men, min = 0, max = most, unit = UiText("transfer.men"), all = true,
	            disabled = !sti(question.adjust)};
	q.steps.s0 = 1;
	q.steps.s1 = 10;
	aref at;
	makearef(at, modal.q);
	CopyAttributes(at, &q);
	UiOn("hire", "change");
	UiOn("hire", "commit");
}

// The captain choice, the shipyard's captain sheet: a tap selects a passenger in the game's list, «Назначить» is the
// game's; ✕, the scrim and back its «Отмена».
void TouchTransfer_Captains(aref game, aref view)
{
	aref from;
	makearef(from, game.captains);
	aref sheet;
	makearef(sheet, view.sheet);
	sheet.kind = "captain";
	sheet.title = from.title;
	sheet.closable = true;
	TouchLib_Captains(from, sheet);
	UiOn("sheet-close", "");
	UiModal("sheet", "sheet-close");
}

void TouchTransfer_Tap()
{
	aref ui = GetEventData();
	if (ui.screen != "transfer") return;
	string id = GetEventData(), verb = GetEventData(), value = GetEventData();
	aref game;
	makearef(game, TouchTransfer.game);
	if (id == "header-back") { TouchTransfer_Back(ui, game); return; }
	if (id == "header") { TouchTransfer_TabTap(ui, game, value); return; }
	if (id == "primary") { TouchTransfer_Send(ui, game); UiHold(); return; }
	if (TouchLib_Action(id) >= 0)
	{
		TouchAdpTransfer_Press(ui, TouchLib_Action(id));
		UiHold();
		return;
	}
	if (TouchLib_StartsWith(id, "answer-"))
	{
		int answer = TouchLib_TapIndex(TouchLib_Cut(id, 7, strlen(id)), TouchLib_Count(game, "question.answers"),
		                               "the answer");
		TouchAdpTransfer_Answer(ui, answer);
		// A held answer is the end of a long press, through which the game may not run: the window it answers
		// closes with the game's answer, nothing of it is kept.
		aref action = TouchLib_Item(&TouchTransfer_View, "modal.actions", answer);
		if (!sti(action.hold)) UiHold();
		return;
	}
	if (TouchLib_StartsWith(id, "candidate-"))
	{
		int person = TouchLib_Find(game, "captains.candidates", "key", TouchLib_Cut(id, 10, strlen(id)));
		if (person < 0) return; // the captain tapped is no longer listed
		aref chosen = TouchLib_Item(game, "captains.candidates", person);
		if (sti(chosen.selected)) return;
		TouchAdpTransfer_Pick(ui, person);
		UiHold();
		return;
	}
	if (id == "sheet-ok") { TouchAdpTransfer_Appoint(ui); UiHold(); return; }
	if (id == "sheet-close") { TouchAdpTransfer_Dismiss(ui); UiHold(); return; }
	if (id == "hire") { TouchAdpTransfer_Hire(ui, UiAmount(value, "the defectors")); UiHold(); return; }
	if (id == "crew")
	{
		if (verb == "row") return;
		TouchTransfer_Set("crew", UiAmount(value, "our crew"));
		return;
	}
	if (id != "goods") UiError("transfer: no tap of " + id);
	int open = TouchLib_Selected(game, "cargo.rows");
	if (verb == "change" || verb == "commit")
	{
		if (open < 0) UiError("transfer: an amount is set with no cargo row selected");
		aref chosen = TouchLib_Item(game, "cargo.rows", open);
		TouchTransfer_Set(chosen.key, UiAmount(value, "the cargo amount"));
		return;
	}
	if (verb == "sort")
	{
		aref sorting;
		makearef(sorting, TouchTransfer.sort);
		TouchLib_SortTap(sorting, TouchLib_TapIndex(value, 2, "the goods column"));
		UiRebuild();
		return;
	}
	// The row by its identity: the list may have changed since the frame the player tapped; a row gone ends the tap.
	int k = TouchLib_Find(game, "cargo.rows", "key", value);
	if (k < 0) return;
	aref row = TouchLib_Item(game, "cargo.rows", k);
	if (verb == "quick") TouchAdpTransfer_Take(ui, k);
	if (verb == "row")
	{
		bool arrowless = !sti(game.cargo.move.take) && !sti(game.cargo.move.give);
		if ((k == open && !arrowless) || !sti(row.can)) return;
		TouchAdpTransfer_Select(ui, k);
	}
	UiHold();
}

// An amount set on the screen (ours of a cargo row or the crew): shown until it is sent or its row is left.
void TouchTransfer_Set(string key, int amount)
{
	TouchTransfer.draft.key = key;
	TouchTransfer.draft.amount = amount;
	UiRebuild();
}

// The bar's button: the amount set, as the game's arrows (the build shows where the game stopped them); else the
// game's «take all».
void TouchTransfer_Send(aref ui, aref game)
{
	bool crew = CheckAttribute(game, "crew");
	int now = TouchTransfer_Pending(game, crew);
	if (now < 0)
	{
		if (crew || !CheckAttribute(game, "takeAll")) UiError("transfer: the bar's button tapped with nothing to send");
		TouchAdpTransfer_TakeAll(ui);
		return;
	}
	int amount = sti(TouchTransfer.draft.amount);
	TouchTransfer.sent.key = TouchTransfer.draft.key;
	TouchTransfer.sent.from = now;
	TouchTransfer.sent.asked = amount;
	TouchTransfer.draft.key = "";
	if (crew) { TouchAdpTransfer_Men(ui, amount); return; }
	TouchAdpTransfer_Move(ui, TouchLib_Selected(game, "cargo.rows"), amount);
}

// The amount sent, once the game has answered: where its arrows stopped short of it (the other ship's room, a hold
// full) is said, else the player sees the old number and nothing else.
void TouchTransfer_Stopped(aref game)
{
	string key = TouchTransfer.sent.key;
	if (key == "") return;
	string answer = UiAnswer();
	if (answer == "waiting") return;
	TouchTransfer.sent.key = "";
	if (answer != "applied" && answer != "late") return;
	int now = -1;
	if (key == "crew")
	{
		if (CheckAttribute(game, "crew")) now = sti(game.crew.ours);
	}
	else
	{
		int k = TouchLib_Find(game, "cargo.rows", "key", key);
		if (k >= 0)
		{
			aref row = TouchLib_Item(game, "cargo.rows", k);
			now = sti(row.own);
		}
	}
	int from = sti(TouchTransfer.sent.from);
	int asked = sti(TouchTransfer.sent.asked);
	if (now < 0 || now == asked) return;
	UiToast(TouchLib_FillTwo(UiText("transfer.stopped"), "" + abs(now - from), "" + abs(asked - from)));
}

// Back answers the game's question by its way out, puts away the captains or the crew, else leaves.
void TouchTransfer_Back(aref ui, aref game)
{
	if (CheckAttribute(game, "question"))
	{
		aref question;
		makearef(question, game.question);
		int out = TouchTransfer_WayOut(question);
		if (out < 0) return;
		TouchAdpTransfer_Answer(ui, out);
		UiHold();
		return;
	}
	if (CheckAttribute(game, "captains")) { TouchAdpTransfer_Dismiss(ui); UiHold(); return; }
	if (CheckAttribute(game, "crew")) { TouchAdpTransfer_Crew(ui, false); UiHold(); return; }
	TouchAdpTransfer_Leave(ui);
}

// The crew tab opens the game's crew window, another tab closes it; the ships tab is ours and waits for that window
// to close (the models between still have it open).
void TouchTransfer_TabTap(aref ui, aref game, string value)
{
	aref tabs;
	makearef(tabs, TouchTransfer_View.tabs);
	int k = TouchLib_TapIndex(value, GetAttributesNum(tabs), "the tab");
	aref tab = GetAttributeN(tabs, k);
	string id = tab.id;
	bool crew = CheckAttribute(game, "crew");
	TouchTransfer.ships = id == "ships";
	if (id == "crew" && !crew && sti(game.crewTab.can)) { TouchAdpTransfer_Crew(ui, true); UiHold(); return; }
	if (id != "crew" && crew) { TouchAdpTransfer_Crew(ui, false); UiHold(); return; }
	UiRebuild();
}
