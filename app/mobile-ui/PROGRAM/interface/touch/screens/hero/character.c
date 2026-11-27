// SPDX-License-Identifier: GPL-3.0-only
// The character, a hero section (and the card of one person): the identity rail with the game's figures, the skill
// tables with their points and the game's description of each, the game's two perk groups as a tree each with the
// chosen perk described and learned by the game, and the officers on their posts with the game's chooser and
// dismissal; the header switches between the people of the company.

#event_handler("UiBuild", "TouchCharacter_Build");
#event_handler("UiTap", "TouchCharacter_Tap");

// The adapter's state of the last build (.game), the tab (skills, perks, officers), the perk described (.perk), the
// skill row described (.help.{table, row}, absent none), the group asked for until the game shows it (.wanted, -1
// none, 0 personal, 1 ship), the post tapped (.posted, -1), the people's list open (.open), a refused learn tapped
// (.nudge: its build shows what is missing), the person the HUD asked for (.asked, -1).
object TouchCharacter;
object TouchCharacter_View;
object TouchCharacter_Header; // the section tapped while the game opens it (TouchLib_Section)

// s: how long a move waits for the game's answer; past it the view shows what the game has.
float TouchCharacter_Wait = 0.5;

string TouchCharacter_FillThree(string text, string first, string second, string third)
{
	int at = findSubStr(text, "{3}", 0);
	if (at >= 0) text = TouchLib_Cut(text, 0, at) + third + TouchLib_Cut(text, at + 3, strlen(text));
	return TouchLib_FillTwo(text, first, second);
}

string TouchCharacter_Signed(int value)
{
	if (value < 0) return "−" + (-value);
	return "+" + value;
}

void TouchCharacter_Tab(aref tabs, string id, string label, bool selected, int badge)
{
	aref at = TouchLib_Add(tabs, "t");
	at.id = id;
	at.label = label;
	at.selected = selected;
	at.badge = "";
	if (badge > 0) at.badge = badge;
}

void TouchCharacter_Tabs(aref game, aref view)
{
	aref tabs;
	makearef(tabs, view.tabs);
	string tab = TouchCharacter.tab;
	bool ship = sti(game.groupShip);
	TouchCharacter_Tab(tabs, "skills", UiText("hero.tab_skills"), tab == "skills", sti(game.free.skill));
	string label = game.groups.self;
	if (label == "") label = UiText("hero.tab_perks");
	TouchCharacter_Tab(tabs, "perks1", label, tab == "perks" && !ship, sti(game.free.self));
	if (game.groups.ship != "") TouchCharacter_Tab(tabs, "perks2", game.groups.ship, tab == "perks" && ship, sti(game.free.ship));
	if (sti(game.full)) TouchCharacter_Tab(tabs, "officers", UiText("hero.tab_officers"), tab == "officers", 0);
}

void TouchCharacter_Bar(aref bar, aref figure)
{
	bar.value = 100;
	if (sti(figure.max) > 0) bar.value = makeint(sti(figure.now) * 100 / sti(figure.max));
	if (sti(bar.value) > 100) bar.value = 100;
	if (sti(bar.value) < 0) bar.value = 0;
	bar.text = figure.now;
}

aref TouchCharacter_Line(aref lines, string k, string v, string tone)
{
	aref line = TouchLib_Add(lines, "l");
	line.k = k;
	line.v = v;
	if (tone != "") line.tone = tone;
	return line;
}

void TouchCharacter_Card(aref game, aref view)
{
	aref from, card;
	makearef(from, game.card);
	makearef(card, view.card);
	card.title = from.name;
	card.sub = "";
	if (from.rank != "") card.sub = from.rank + " " + from.rankValue;
	card.roles = "";
	if (sti(game.shown) >= 0)
	{
		aref person = TouchLib_Item(game, "people", sti(game.shown));
		card.roles = person.meta;
	}
	aref picture, portrait;
	makearef(portrait, card.portrait);
	makearef(picture, from.portrait);
	CopyAttributes(portrait, picture);
	aref bar, figure;
	makearef(bar, card.health);
	makearef(figure, from.health);
	TouchCharacter_Bar(bar, figure);
	makearef(bar, card.energy);
	makearef(figure, from.energy);
	TouchCharacter_Bar(bar, figure);
	card.free = "";
	if (TouchCharacter.tab == "skills" && sti(game.free.skill) > 0) card.free = game.free.skill;
	aref lines;
	makearef(lines, card.lines);
	if (from.healthWord != "")
	{
		string tone = "ok";
		if (sti(from.healthBad)) tone = "danger";
		TouchCharacter_Line(lines, from.healthCaption, from.healthWord, tone);
	}
	if (from.standing.value != "")
	{
		string standing = from.standing.value;
		if (sti(from.standing.authority)) standing = standing + " / 100";
		TouchCharacter_Line(lines, from.standing.caption, standing, "");
	}
	if (sti(from.salary) > 0)
	{
		aref line = TouchCharacter_Line(lines, UiText("hero.salary"), TouchLib_Thousands(sti(from.salary)), "gold");
		line.after = " " + UiText("hero.per_month");
	}
	aref actions;
	makearef(actions, card.actions);
	for (int k = 0; k < TouchLib_Count(game, "actions"); k++)
	{
		aref action = TouchLib_Item(game, "actions", k);
		aref at = TouchLib_Add(actions, "a");
		at.id = action.id;
		at.label = action.label;
		at.variant = "secondary";
		if (sti(action.primary)) at.variant = "primary";
		at.disabled = !sti(action.can);
		at.reason = "";
		at.busy = UiPending(action.id);
	}
}

// The rest of the game's figures with the reputation, for the portrait's sheet.
string TouchCharacter_Facts(aref game)
{
	string text = "";
	string newline = TouchLib_Newline();
	for (int k = 0; k < TouchLib_Count(game, "card.facts"); k++)
	{
		aref fact = TouchLib_Item(game, "card.facts", k);
		text += fact.caption + ": " + fact.value + newline;
	}
	if (game.card.reputation != "") text += UiText("hero.reputation") + ": " + game.card.reputation + newline;
	return text;
}

void TouchCharacter_Column(aref columns, string label, string w, string align)
{
	aref at = TouchLib_Add(columns, "c");
	at.label = label;
	at.w = w;
	if (align != "") at.align = align;
}

// One skill table (characteristics, personal, ship); with points its rows take the game's arrows.
void TouchCharacter_Table(aref game, aref view, string table, bool points)
{
	aref from;
	makearef(from, game.tables.(table));
	bool special = table == "special";
	aref at, columns, rows;
	makearef(at, view.(table));
	makearef(columns, at.columns);
	makearef(rows, at.rows);
	string heading = from.heading;
	if (special) heading = UiText("hero.special");
	TouchCharacter_Column(columns, heading, "1fr", "");
	if (points) TouchCharacter_Column(columns, "", "112px", "right");
	else
	{
		if (!special) TouchCharacter_Column(columns, UiText("hero.exp"), "40px", "");
		if (special) TouchCharacter_Column(columns, "", "32px", "right");
		else TouchCharacter_Column(columns, "", "28px", "right");
	}
	for (int k = 0; k < TouchLib_Count(from, "rows"); k++)
	{
		aref row = TouchLib_Item(from, "rows", k);
		aref cells;
		aref line = TouchLib_Add(rows, "r");
		makearef(cells, line.cells);
		// The game names blade and gun skills by pairs ("Rapiers/Swords"): the first word fits the column.
		string name = row.name;
		int slash = findSubStr(name, "/", 0);
		if (!special && slash > 0) name = TouchLib_Trim(TouchLib_Cut(name, 0, slash));
		aref first = TouchLib_Add(cells, "c");
		first.text = name;
		first.lines = 2;
		if (!points && row.icon != "") { first.icon = row.icon; first.iconSize = 24; }
		aref cell;
		if (points)
		{
			// Points raise the base: while they are spent the row shows it.
			cell = TouchLib_Add(cells, "c");
			cell.text = row.base;
			cell.arrows = true;
			cell.noLeft = !(sti(row.added) > 0 || (special && TouchLib_Count(game, "actions") > 0));
			cell.tone = "gold";
			if (sti(row.added) > 0) cell.tone = "ok";
			cell.bold = true;
			continue;
		}
		if (!special)
		{
			cell = TouchLib_Add(cells, "c");
			cell.bar = 100;
			if (row.experience != "") cell.bar = UiAmount(row.experience, "a skill's experience");
			cell.barKind = "xp";
			cell.text = "";
		}
		// The value the game uses, green above the base, red below it.
		int bonus = sti(row.bonus);
		cell = TouchLib_Add(cells, "c");
		cell.text = sti(row.base) + bonus;
		cell.bold = true;
		if (bonus < 0) cell.tone = "danger";
		if (bonus > 0) cell.tone = "ok";
		if (bonus == 0 && !special) cell.tone = "gold";
	}
	UiOn(table, "inc");
	UiOn(table, "dec");
	UiOn(table, "row");
}

bool TouchCharacter_Points(aref game)
{
	if (sti(game.free.skill) > 0) return true;
	for (int t = 0; t < 2; t++)
	{
		string table = "personal";
		if (t == 1) table = "ship";
		for (int k = 0; k < TouchLib_Count(game, "tables." + table + ".rows"); k++)
		{
			aref row = TouchLib_Item(game, "tables." + table + ".rows", k);
			if (sti(row.added) != 0) return true;
		}
	}
	return false;
}

void TouchCharacter_Skills(aref game, aref view)
{
	bool points = TouchCharacter_Points(game);
	TouchCharacter_Table(game, view, "special", sti(game.specialPoints));
	TouchCharacter_Table(game, view, "personal", points);
	TouchCharacter_Table(game, view, "ship", points);
	view.points = points;
}

// k of the perk with that id in game.perks.nodes, -1 none (ids compare without case, as the game's do).
int TouchCharacter_Perk(aref game, string id)
{
	object index;
	for (int k = 0; k < TouchLib_Count(game, "perks.nodes"); k++)
	{
		aref node = TouchLib_Item(game, "perks.nodes", k);
		string key = node.id;
		index.(key) = k;
	}
	if (id == "" || !CheckAttribute(&index, id)) return -1;
	return sti(index.(id));
}

string TouchCharacter_State(aref game, int k)
{
	aref node = TouchLib_Item(game, "perks.nodes", k);
	if (sti(node.learned)) return "learned";
	for (int e = 0; e < TouchLib_Count(game, "perks.edges"); e++)
	{
		aref edge = TouchLib_Item(game, "perks.edges", e);
		if (!sti(edge.met) && TouchCharacter_Perk(game, edge.to) == k) return "locked";
	}
	return "open";
}

void TouchCharacter_Image(aref node, string state, aref out)
{
	aref image;
	makearef(image, node.image);
	CopyAttributes(out, image);
	if (CheckAttribute(out, "kind") && out.kind == "token")
	{
		out.enamel = "red";
		if (state != "learned" && state != "open") out.enamel = "iron";
	}
}

string TouchCharacter_GroupIcon(bool ship)
{
	if (ship) return "ship";
	return "hero";
}

// A perk named in the description: what it needs or opens.
void TouchCharacter_Link(aref game, aref rows, int k, bool met, bool ship)
{
	aref node = TouchLib_Item(game, "perks.nodes", k);
	string state = TouchCharacter_State(game, k);
	aref at = TouchLib_Add(rows, "r");
	at.perk = node.id;
	at.name = node.label;
	aref image;
	makearef(image, at.image);
	TouchCharacter_Image(node, state, image);
	if (!sti(node.learned) && CheckAttribute(image, "kind") && image.kind == "token") image.enamel = "iron";
	at.met = met;
	at.nudge = false;
	at.state = state;
	at.other = sti(node.ship) != ship;
	at.groupIcon = "";
	if (sti(at.other)) at.groupIcon = TouchCharacter_GroupIcon(sti(node.ship));
}

// What is still missing first, then this tab's perks before the other group's.
void TouchCharacter_Links(aref game, aref out, aref links)
{
	for (int pass = 0; pass < 4; pass++)
	{
		for (int k = 0; k < GetAttributesNum(links); k++)
		{
			aref link = GetAttributeN(links, k);
			int rank = 0;
			if (sti(link.met)) rank += 2;
			if (sti(link.other)) rank += 1;
			if (rank != pass) continue;
			aref at = TouchLib_Add(out, "r");
			CopyAttributes(at, link);
		}
	}
}

void TouchCharacter_Perks(aref ui, aref game, aref view)
{
	bool full = sti(game.full);
	bool ship = sti(game.groupShip);
	string group = "self";
	if (ship) group = "ship";
	int free = sti(game.free.(group));
	view.perksEmpty = UiText("hero.no_perks");
	view.perkInfoNudge = false;
	view.perkInfo = "";
	if (full)
	{
		if (free < 0) free = 0;
		view.perkInfo = UiText("hero.free_points") + " " + free;
		if (sti(game.next.(group).rate) > 0)
			view.perkInfo = view.perkInfo + " · " + UiText("hero.next_point") + " " + TouchLib_Thousands(sti(game.next.(group).exp)) +
			                " / " + TouchLib_Thousands(sti(game.next.(group).rate));
	}
	int count = TouchLib_Count(game, "perks.nodes");
	bool table = false;
	for (int k = 0; k < count; k++)
	{
		aref node = TouchLib_Item(game, "perks.nodes", k);
		if (sti(node.table)) table = true;
	}
	if (!table) return;
	object layout;
	aref nodes, edges;
	makearef(nodes, game.perks.nodes);
	makearef(edges, game.perks.edges);
	UiTreeLayout(nodes, edges, &layout);
	if (!sti(layout.valid)) UiError("character: the game's perks link in a circle or to a perk it has not");
	// The perk described: the one asked for, else the group's first in reading order (the top row, from the left).
	int selected = TouchCharacter_Perk(game, TouchCharacter.perk);
	if (selected < 0)
	{
		for (k = 0; k < count; k++)
		{
			node = TouchLib_Item(game, "perks.nodes", k);
			if (!sti(node.table)) continue;
			aref place = TouchLib_Item(&layout, "places", k);
			if (selected < 0) { selected = k; continue; }
			aref best = TouchLib_Item(&layout, "places", selected);
			if (sti(place.top) < sti(best.top) || (sti(place.top) == sti(best.top) && sti(place.left) < sti(best.left))) selected = k;
		}
	}
	aref chosen = TouchLib_Item(game, "perks.nodes", selected);
	TouchCharacter.perk = chosen.id;
	// The links as <ui-links> draws them: 2 the selected perk's (on top), 1 from a learned perk, 0 the others.
	string links = "";
	for (int j = 0; j < TouchLib_Count(&layout, "links"); j++)
	{
		aref link = TouchLib_Item(&layout, "links", j);
		int from = TouchCharacter_Perk(game, link.from);
		int to = TouchCharacter_Perk(game, link.to);
		aref source = TouchLib_Item(game, "perks.nodes", from);
		int kind = 0;
		if (sti(source.learned)) kind = 1;
		if (from == selected || to == selected) kind = 2;
		if (links != "") links += ";";
		links += kind + " " + link.points;
	}
	aref tree, list;
	makearef(tree, view.tree);
	makearef(list, tree.nodes);
	tree.links = links;
	tree.width = layout.width;
	tree.height = layout.height;
	for (k = 0; k < count; k++)
	{
		node = TouchLib_Item(game, "perks.nodes", k);
		place = TouchLib_Item(&layout, "places", k);
		string state = TouchCharacter_State(game, k);
		aref at = TouchLib_Add(list, "n");
		at.id = node.id;
		at.name = node.label;
		aref image;
		makearef(image, at.image);
		TouchCharacter_Image(node, state, image);
		at.state = state;
		at.selected = k == selected;
		at.other = sti(node.ship) != ship;
		at.groupIcon = TouchCharacter_GroupIcon(sti(node.ship));
		at.left = place.left;
		at.top = place.top;
		at.reveal = "";
		if (k == selected) at.reveal = group + ":" + node.id;
		at.revealTop = sti(place.top) - 16;
	}
	UiOn("perks", "row");
	TouchCharacter_Detail(game, view, selected, free);
}

// The panel of the perk described: what it needs and what it opens, each a row that describes that perk.
void TouchCharacter_Detail(aref game, aref view, int selected, int free)
{
	bool full = sti(game.full);
	bool ship = sti(game.groupShip);
	string group = "self";
	if (ship) group = "ship";
	aref node = TouchLib_Item(game, "perks.nodes", selected);
	bool learned = sti(node.learned);
	bool external = !sti(node.table);
	bool shipPerk = sti(node.ship);
	bool can = full && !external && !learned && game.refused != node.id;
	object needs, opens;
	for (int e = 0; e < TouchLib_Count(game, "perks.edges"); e++)
	{
		aref edge = TouchLib_Item(game, "perks.edges", e);
		if (TouchCharacter_Perk(game, edge.to) == selected)
			TouchCharacter_Link(game, &needs, TouchCharacter_Perk(game, edge.from), sti(edge.met), ship);
		if (TouchCharacter_Perk(game, edge.from) == selected)
		{
			int to = TouchCharacter_Perk(game, edge.to);
			aref target = TouchLib_Item(game, "perks.nodes", to);
			TouchCharacter_Link(game, &opens, to, sti(target.learned), ship);
		}
	}
	aref detail, rows;
	makearef(detail, view.detail);
	detail.id = "detail:" + group + ":" + node.id;
	detail.title = node.label;
	detail.text = node.text;
	aref image;
	makearef(image, detail.image);
	TouchCharacter_Image(node, TouchCharacter_State(game, selected), image);
	detail.group = "";
	if (external || shipPerk != ship) detail.group = game.groups.(TouchCharacter_Group(shipPerk));
	detail.groupIcon = TouchCharacter_GroupIcon(shipPerk);
	makearef(rows, detail.requires);
	TouchCharacter_Links(game, rows, &needs);
	makearef(rows, detail.opens);
	TouchCharacter_Links(game, rows, &opens);
	detail.note = "";
	if (!external && sti(node.officer)) detail.note = UiText("hero.officer_has");
	detail.learn = "";
	detail.go = "";
	UiOn("perk-detail", "go");
	if (external)
	{
		// A perk of the other group is read here; it is learned in its own tab.
		detail.go.id = "perk-open-group";
		detail.go.label = UiText("hero.open_group");
		UiOn("perk-open-group", "");
	}
	else if (full)
	{
		// Learned by a hold (what cannot be undone); the game decides, its refusal turns the button off.
		detail.learn.id = "perk-learn";
		detail.learn.label = UiText("hero.learn");
		if (learned) detail.learn.label = UiText("hero.learned");
		detail.learn.disabled = !can;
		detail.learn.busy = sti(game.learning);
		UiOn("perk-learn", "click");
		UiOn("perk-learn", "refused");
	}
	// What a refused learn points at, in the build after its tap: the missing perks, or the free points.
	bool nudge = full && !external && !learned && !can && sti(TouchCharacter.nudge);
	if (!nudge) return;
	for (int k = 0; k < TouchLib_Count(detail, "requires"); k++)
	{
		aref need = TouchLib_Item(detail, "requires", k);
		need.nudge = !sti(need.met);
	}
	view.perkInfoNudge = free <= 0;
}

string TouchCharacter_Group(bool ship)
{
	if (ship) return "ship";
	return "self";
}

void TouchCharacter_Officers(aref game, aref view)
{
	aref posts, frees, passengers;
	makearef(posts, view.posts);
	makearef(frees, view.freePosts);
	makearef(passengers, view.passengers);
	int total = TouchLib_Count(game, "posts");
	int taken = 0;
	for (int k = 0; k < total; k++)
	{
		aref post = TouchLib_Item(game, "posts", k);
		aref at;
		bool selected = sti(TouchCharacter.posted) == sti(post.slot);
		if (sti(post.character) >= 0)
		{
			taken++;
			at = TouchLib_Add(posts, "p");
			at.id = post.slot;
			at.name = post.label;
			at.meta = post.role;
			aref image, from;
			makearef(image, at.image);
			makearef(from, post.image);
			CopyAttributes(image, from);
			at.selected = selected;
		}
		else
		{
			at = TouchLib_Add(frees, "f");
			at.id = post.slot;
			at.role = post.role;
			at.icon = post.icon;
			at.selected = selected;
		}
		UiOn("post-" + post.slot, "");
	}
	view.postsCount = taken + " " + UiText("common.of") + " " + total;
	int count = TouchLib_Count(game, "passengers");
	for (k = 0; k < count; k++)
	{
		aref passenger = TouchLib_Item(game, "passengers", k);
		at = TouchLib_Add(passengers, "p");
		at.name = passenger.label;
		at.meta = UiText("hero.rank_short") + " " + passenger.rank;
		makearef(image, at.image);
		makearef(from, passenger.image);
		CopyAttributes(image, from);
	}
	view.passengersCount = "";
	if (count > 0) view.passengersCount = count;
}

void TouchCharacter_Help(aref ui, aref game, aref view)
{
	string table = TouchCharacter.help.table;
	int k = sti(TouchCharacter.help.row);
	object help;
	TouchAdpCharacter_Help(ui, table, k, &help);
	aref row = TouchLib_Item(game, "tables." + table + ".rows", k);
	aref dialog, steps;
	makearef(dialog, view.dialog);
	dialog.kind = "skill";
	dialog.title = row.name;
	dialog.text = help.text;
	makearef(steps, dialog.steps);
	if (help.base != "" && TouchLib_Count(&help, "steps") > 0)
	{
		aref line = TouchCharacter_Line(steps, UiText("hero.step_base"), help.base, "dim");
		for (int n = 0; n < TouchLib_Count(&help, "steps"); n++)
		{
			aref step = TouchLib_Item(&help, "steps", n);
			string label = "";
			if (step.kind == "item") label = step.label;
			if (step.kind == "officer") label = UiFill(UiText("hero.step_officer"), step.label);
			if (step.kind == "navy") label = TouchCharacter_FillThree(UiText("hero.step_navy"), step.a1, step.a2, step.a3);
			if (step.kind == "health") label = UiFill(UiText("hero.step_health"), UiText("hero.health_" + step.a1));
			if (label == "") label = UiText("hero.step_" + step.kind);
			string tone = "ok";
			if (sti(step.value) < 0) tone = "danger";
			TouchCharacter_Line(steps, label, TouchCharacter_Signed(sti(step.value)), tone);
		}
		int bonus = sti(row.bonus);
		string total = "";
		if (bonus < 0) total = "danger";
		if (bonus > 0) total = "ok";
		TouchCharacter_Line(steps, UiText("hero.step_result"), "" + (sti(row.base) + bonus), total);
	}
	aref actions;
	makearef(actions, dialog.actions);
	object close = {id = "skill-help-close", label = UiText("common.close"), variant = "primary"};
	aref at = TouchLib_Add(actions, "a");
	CopyAttributes(at, &close);
	UiOn("skill-help-close", "");
}

void TouchCharacter_Dialog(aref ui, aref game, aref view)
{
	if (CheckAttribute(&TouchCharacter, "help.table") && game.window == "" && TouchCharacter.tab == "skills")
	{
		TouchCharacter_Help(ui, game, view);
		UiOn("dialog-close", "");
		UiModal("dialog", "dialog-close");
		return;
	}
	aref dialog, list;
	makearef(dialog, view.dialog);
	aref at;
	if (game.window == "remove")
	{
		aref remove;
		makearef(remove, game.remove);
		dialog.kind = "officer";
		dialog.title = remove.label;
		dialog.subtitle = remove.role;
		makearef(list, dialog.facts);
		TouchCharacter_Line(list, remove.skill, remove.skillValue, "");
		TouchCharacter_Line(list, UiText("hero.loyalty"), remove.loyalty, "");
		if (sti(remove.salary) > 0)
			TouchCharacter_Line(list, UiText("hero.salary"), TouchLib_Thousands(sti(remove.salary)) + " " + UiText("hero.per_month"), "");
		makearef(list, dialog.actions);
		object dismiss = {id = "officer-remove", label = UiText("hero.dismiss"), variant = "secondary",
		                  disabled = !sti(remove.can), busy = UiPending("officer-remove")};
		at = TouchLib_Add(list, "a");
		CopyAttributes(at, &dismiss);
		object skills = {id = "officer-skills", label = UiText("hero.tab_skills"), variant = "primary"};
		at = TouchLib_Add(list, "a");
		CopyAttributes(at, &skills);
		UiOn("officer-remove", "");
		UiOn("officer-skills", "");
	}
	if (game.window == "assign")
	{
		dialog.kind = "assign";
		dialog.title = game.assign.role;
		dialog.empty = UiText("hero.no_candidates");
		makearef(list, dialog.rows);
		for (int k = 0; k < TouchLib_Count(game, "assign.rows"); k++)
		{
			aref candidate = TouchLib_Item(game, "assign.rows", k);
			at = TouchLib_Add(list, "r");
			at.id = k;
			at.name = candidate.label;
			at.meta = UiText("hero.rank_short") + " " + candidate.rank;
			if (sti(candidate.salary) > 0)
				at.meta = at.meta + " · " + TouchLib_Thousands(sti(candidate.salary)) + " " + UiText("hero.per_month");
			at.value = candidate.skill + " " + candidate.skillValue;
			aref image, from;
			makearef(image, at.image);
			makearef(from, candidate.image);
			CopyAttributes(image, from);
			at.busy = candidate.busy;
			UiOn("candidate-" + k, "");
		}
	}
	if (game.window == "message")
	{
		dialog.kind = "message";
		dialog.title = game.message.title;
		dialog.text = game.message.text;
		makearef(list, dialog.actions);
		object ok = {id = "msg-ok", label = UiText("common.close"), variant = "primary"};
		at = TouchLib_Add(list, "a");
		CopyAttributes(at, &ok);
		UiOn("msg-ok", "");
	}
	if (game.window == "" || game.window == "perk") return;
	UiOn("dialog-close", "");
	UiModal("dialog", "dialog-close");
}

// The person the HUD asked for (a party portrait, an event line), shown once the game can switch.
void TouchCharacter_Asked(aref ui, aref game)
{
	object subject;
	if (UiSubject("character", &subject))
	{
		if (subject.tab == "perks") TouchCharacter.tab = "perks";
		TouchCharacter.asked = subject.character;
	}
	int asked = sti(TouchCharacter.asked);
	if (asked < 0 || !sti(game.full) || game.window != "" || sti(game.waiting)) return;
	TouchCharacter.asked = -1;
	for (int k = 0; k < TouchLib_Count(game, "people"); k++)
	{
		aref person = TouchLib_Item(game, "people", k);
		if (sti(person.character) != asked) continue;
		if (k != sti(game.shown)) TouchAdpCharacter_Person(ui, k);
		return;
	}
	UiError("character: the person " + asked + " the HUD asked for is not in the character list");
}

void TouchCharacter_Build()
{
	aref ui = GetEventData();
	if (ui.screen != "character") return;
	if (UiFirst())
	{
		DeleteAttribute(&TouchCharacter, "");
		TouchCharacter.tab = "skills";
		TouchCharacter.perk = "";
		TouchCharacter.wanted = -1;
		TouchCharacter.posted = -1;
		TouchCharacter.open = false;
		TouchCharacter.nudge = false;
		TouchCharacter.asked = -1;
	}
	DeleteAttribute(&TouchCharacter, "game");
	aref game;
	makearef(game, TouchCharacter.game);
	TouchAdpCharacter_State(ui, game);
	TouchCharacter_Asked(ui, game);
	if (!sti(game.full) && TouchCharacter.tab == "officers") TouchCharacter.tab = "skills";
	if (game.window != "") TouchCharacter.posted = -1;
	if (sti(TouchCharacter.wanted) == sti(game.groupShip)) TouchCharacter.wanted = -1;
	DeleteAttribute(&TouchCharacter_View, "");
	aref view;
	makearef(view, TouchCharacter_View);
	view.title = game.title;
	view.tab = TouchCharacter.tab;
	view.headerSection = "";
	if (sti(game.full) && sti(game.sections))
	{
		view.headerSection = TouchLib_Section(&TouchCharacter_Header, "hero");
		UiOn("header", "section");
	}
	TouchCharacter_Tabs(game, view);
	UiOn("header-tabs", "select");
	UiOn("header", "tab");
	if (sti(game.full))
	{
		aref subject, people;
		makearef(subject, view.subject);
		makearef(people, game.people);
		TouchLib_Subject(subject, people, sti(game.shown), sti(TouchCharacter.open));
		TouchLib_SubjectOn(people, sti(TouchCharacter.open));
	}
	TouchCharacter_Card(game, view);
	UiOn("card", "portrait");
	for (int k = 0; k < TouchLib_Count(game, "actions"); k++)
	{
		aref action = TouchLib_Item(game, "actions", k);
		UiOn(action.id, "");
	}
	if (TouchCharacter.tab == "skills") TouchCharacter_Skills(game, view);
	if (TouchCharacter.tab == "perks") TouchCharacter_Perks(ui, game, view);
	if (TouchCharacter.tab == "officers") TouchCharacter_Officers(game, view);
	TouchCharacter_Dialog(ui, game, view);
	TouchCharacter.nudge = false;
	UiOn("header-back", "");
	UiShow(&TouchCharacter_View);
}

// A tab of the header: a perk group's is the game's group shown, pressed for it.
void TouchCharacter_Select(aref ui, aref game, string id)
{
	bool perks = id == "perks1" || id == "perks2";
	string tab = "skills";
	if (perks) tab = "perks";
	if (id == "officers") tab = "officers";
	bool ship = id == "perks2";
	bool shown = sti(game.groupShip);
	if (sti(TouchCharacter.wanted) >= 0) shown = sti(TouchCharacter.wanted);
	if (tab == TouchCharacter.tab && (!perks || ship == shown)) return;
	TouchCharacter.tab = tab;
	DeleteAttribute(&TouchCharacter, "help");
	if (game.window != "" && game.window != "message")
	{
		TouchAdpCharacter_Close(ui);
		UiHold(TouchCharacter_Wait);
		return;
	}
	if (perks && ship != sti(game.groupShip))
	{
		TouchCharacter.perk = "";
		TouchCharacter.wanted = ship;
		TouchAdpCharacter_Group(ui, ship);
		UiHold(TouchCharacter_Wait);
		return;
	}
	UiRebuild();
}

// A perk named in the panel: described here when it is in this tree, else the game shows its group.
void TouchCharacter_Jump(aref ui, aref game, string id)
{
	int k = TouchCharacter_Perk(game, id);
	if (k < 0) return;
	aref node = TouchLib_Item(game, "perks.nodes", k);
	TouchCharacter.perk = node.id;
	bool ship = sti(node.ship);
	if (!sti(node.table) && ship != sti(game.groupShip) && game.window == "" && !sti(game.learning))
	{
		TouchCharacter.wanted = ship;
		TouchAdpCharacter_Group(ui, ship);
		UiHold(TouchCharacter_Wait);
		return;
	}
	UiRebuild();
}

void TouchCharacter_Tap()
{
	aref ui = GetEventData();
	if (ui.screen != "character") return;
	string id = GetEventData(), verb = GetEventData(), value = GetEventData();
	aref game;
	makearef(game, TouchCharacter.game);
	if (id == "header" && verb == "section")
	{
		if (!TouchLib_SectionAsk(&TouchCharacter_Header, "hero", value)) return;
		// The section tapped is drawn chosen from this frame; the game opens it.
		TouchCharacter_View.headerSection = value;
		UiPredict(&TouchCharacter_View);
		TouchAdpCharacter_Section(ui, value);
		UiHold(TouchCharacter_Wait);
		return;
	}
	if (id == "header-tabs") { TouchCharacter_Select(ui, game, value); return; }
	if (id == "header" && verb == "tab")
	{
		int tab = TouchLib_TapIndex(value, TouchLib_Count(&TouchCharacter_View, "tabs"), "the tab");
		aref chosen = TouchLib_Item(&TouchCharacter_View, "tabs", tab);
		TouchCharacter_Select(ui, game, chosen.id);
		return;
	}
	if (id == "header-back")
	{
		if (CheckAttribute(&TouchCharacter, "help.table")) { DeleteAttribute(&TouchCharacter, "help"); UiRebuild(); return; }
		if (game.window != "") { TouchAdpCharacter_Close(ui); UiHold(TouchCharacter_Wait); return; }
		TouchAdpCharacter_Leave(ui);
		return;
	}
	if (sti(game.full))
	{
		int person = TouchLib_SubjectTap(&TouchCharacter, id, verb, sti(game.shown), TouchLib_Count(game, "people"));
		if (person >= 0)
		{
			// The person picked is named from this frame; the game shows them a frame later.
			aref people, subject;
			makearef(people, game.people);
			makearef(subject, TouchCharacter_View.subject);
			TouchLib_Subject(subject, people, person, false);
			UiPredict(&TouchCharacter_View);
			TouchAdpCharacter_Person(ui, person);
			UiHold(TouchCharacter_Wait);
			return;
		}
		if (id == "header" || TouchLib_StartsWith(id, "header-subject")) return;
	}
	if (id == "card") { UiRead(game.card.name, TouchCharacter_Facts(game)); return; }
	if (id == "special" || id == "personal" || id == "ship")
	{
		int row = TouchLib_TapIndex(value, TouchLib_Count(game, "tables." + id + ".rows"), "the skill");
		if (verb == "row")
		{
			TouchCharacter.help.table = id;
			TouchCharacter.help.row = row;
			UiRebuild();
			return;
		}
		TouchAdpCharacter_Skill(ui, id, row, verb == "inc");
		UiHold(TouchCharacter_Wait);
		return;
	}
	if (id == "skill-help-close" || (id == "dialog-close" && CheckAttribute(&TouchCharacter, "help.table")))
	{
		DeleteAttribute(&TouchCharacter, "help");
		UiRebuild();
		return;
	}
	if (id == "perks")
	{
		// The description only: the game is asked nothing.
		if (TouchCharacter_Perk(game, value) < 0) return;
		TouchCharacter.perk = value;
		UiRebuild();
		return;
	}
	if (id == "perk-detail") { TouchCharacter_Jump(ui, game, value); return; }
	if (id == "perk-open-group") { TouchCharacter_Jump(ui, game, TouchCharacter.perk); return; }
	if (id == "perk-learn" && verb == "refused") { TouchCharacter.nudge = true; UiRebuild(); return; }
	if (id == "perk-learn")
	{
		if (sti(game.learning) || game.refused == TouchCharacter.perk) return;
		TouchAdpCharacter_Learn(ui, TouchCharacter.perk);
		UiHold(TouchCharacter_Wait);
		return;
	}
	if (TouchLib_StartsWith(id, "post-"))
	{
		int slot = UiAmount(TouchLib_Cut(id, 5, strlen(id)), "the post");
		TouchCharacter.posted = slot;
		TouchAdpCharacter_Post(ui, slot);
		UiHold(TouchCharacter_Wait);
		return;
	}
	if (TouchLib_StartsWith(id, "candidate-"))
	{
		int candidate = TouchLib_TapIndex(TouchLib_Cut(id, 10, strlen(id)), TouchLib_Count(game, "assign.rows"), "the candidate");
		TouchAdpCharacter_Assign(ui, candidate);
		UiHold(TouchCharacter_Wait);
		return;
	}
	if (id == "officer-remove") { TouchAdpCharacter_Dismiss(ui); UiHold(TouchCharacter_Wait); return; }
	if (id == "officer-skills")
	{
		TouchCharacter.tab = "skills";
		TouchAdpCharacter_Close(ui);
		UiHold(TouchCharacter_Wait);
		return;
	}
	if (id == "dialog-close" || id == "msg-ok") { TouchAdpCharacter_Close(ui); UiHold(TouchCharacter_Wait); return; }
	if (id == "game-accept" || id == "game-cancel") { TouchAdpCharacter_Act(ui, id); UiHold(TouchCharacter_Wait); return; }
}
