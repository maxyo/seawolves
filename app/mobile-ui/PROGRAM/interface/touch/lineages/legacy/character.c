// SPDX-License-Identifier: GPL-3.0-only
// The character windows (lscchar_all: the hero's company, character: one person's card): the skill tables, the perk
// table of the group shown, the character list CHARACTERS_SCROLL and the game's windows over them (the perk window,
// the post chooser, the dismissal, a message). Implements contracts/character.c.

// The move the game answers in several steps, until it is complete: kind "skill" (row id of table, then its arrow
// up), "learn" (perk: its row selected, its window opened, then accepted), "post" (slot: selected, then opened),
// "assign" (candidate of the chooser for character on post: selected, then accepted), "section" (the window open
// closed first); stage "open" once the row was opened. added.<row id>: the points put in this visit; refused: the
// state in which the game refused a perk (TouchLegCharacter_LearnKey).
object TouchLegCharacter;

string TouchLegCharacter_Node(string table)
{
	if (table == "special") return "TABLE_SPECIAL";
	if (table == "personal") return "TABLE_SKILL_1";
	if (table == "ship") return "TABLE_SKILL_2";
	UiError("character: no skill table " + table);
	return "";
}

void TouchLegCharacter_Step(string kind, string what, string stage)
{
	object step = {kind = kind, what = what, stage = stage, up = false, character = -1, post = -1};
	aref at;
	makearef(at, TouchLegCharacter.step);
	DeleteAttribute(at, "");
	CopyAttributes(at, &step);
}

// The row of `node` whose id (the game's UserData.ID) is `id`, "" none: the row's key r<index>.
string TouchLegCharacter_Row(aref model, string node, string id)
{
	for (int k = 0; k < TouchLib_Count(model, node + ".rows"); k++)
	{
		aref row = TouchLib_Item(model, node + ".rows", k);
		if (row.id == id) return GetAttributeName(row);
	}
	return "";
}

bool TouchLegCharacter_Selected(aref model, string node, string key)
{
	if (key == "") return false;
	return model.(node).rows.(key).value == "1";
}

void TouchLegCharacter_Send(aref model, string node, string key, string verb)
{
	aref row;
	makearef(row, model.(node).rows.(key));
	object action = {node = node, row = row.index, verb = verb, value = row.id};
	UiSendTo(&action);
}

// The game's window open over the screen: "perk", "remove", "assign", "message", "" none.
string TouchLegCharacter_Window(aref model)
{
	if (CheckAttribute(model, "PERK_ACCEPT") || CheckAttribute(model, "PERK_CANCEL")) return "perk";
	if (CheckAttribute(model, "REMOVE_ACCEPT_OFFICER") || CheckAttribute(model, "REMOVE_CANCEL_OFFICER")) return "remove";
	if (CheckAttribute(model, "ACCEPT_ADD_OFFICER") || CheckAttribute(model, "CANCEL_ADD_OFFICER")) return "assign";
	if (CheckAttribute(model, "MSG_OK")) return "message";
	return "";
}

// The open window closed by its own cancel (or its ✕); false when it offers neither.
bool TouchLegCharacter_CloseWindow(aref model)
{
	string window = TouchLegCharacter_Window(model);
	object cancels = {perk = "PERK_CANCEL", remove = "REMOVE_CANCEL_OFFICER", assign = "CANCEL_ADD_OFFICER",
	                  message = "MSG_OK"};
	object exits = {perk = "PERK_EXIT_BTN", remove = "REMOVE_EXIT_BTN", assign = "OFFICER_EXIT_BTN",
	                message = "MSG_EXIT_BTN"};
	if (window == "") return false;
	string node = cancels.(window);
	if (CheckAttribute(model, node) && UiOffers(node, "press")) { UiSend(node, "press", ""); return true; }
	node = exits.(window);
	if (CheckAttribute(model, node) && UiOffers(node, "press")) { UiSend(node, "press", ""); return true; }
	return false;
}

// The character the window shows: the hero's window names its person in HERO_NAME (one of its list), the card
// window keeps it in its global xi_refCharacter.
int TouchLegCharacter_Shown(aref model, aref out)
{
	if (!sti(out.full)) return -1;
	string name = TouchLib_Trim(TouchLegLib_Label(model, "HERO_NAME"));
	for (int k = 0; k < TouchLib_Count(out, "people"); k++)
	{
		aref person = TouchLib_Item(out, "people", k);
		if (person.label == name) return k;
	}
	UiError("character: the game shows " + name + ", who is not in its character list");
	return -1;
}

// "Personal 1" (the game's caption with the group's free points): the caption alone.
string TouchLegCharacter_Group(aref model, string node)
{
	string text = TouchLib_Trim(TouchLegLib_Label(model, node));
	int at = strlen(text) - 1;
	while (at >= 0 && TouchLib_Digit(text, at)) at--;
	if (at < 0 || at == strlen(text) - 1) return text;
	return TouchLib_Trim(TouchLib_Cut(text, 0, at));
}

// "90 / 120" of a TABLE_OTHER row as {now, max}.
void TouchLegCharacter_Bar(string text, string what, aref out)
{
	out.now = 0;
	out.max = 0;
	if (text == "") return;
	int slash = findSubStr(text, "/", 0);
	if (slash < 0)
	{
		out.now = UiAmount(text, what);
		return;
	}
	out.now = UiAmount(TouchLib_Cut(text, 0, slash), what);
	out.max = UiAmount(TouchLib_Cut(text, slash + 1, strlen(text)), what);
}

// The loyalty word as the game's officer windows write it (interface_utils.c SetOTHERMiniTable).
string TouchLegCharacter_Loyalty(ref chr)
{
	return XI_ConvertString(GetLoyalityName(makeint(GetCharacterLoyality(chr) * 100 / MAX_LOYALITY)));
}

void TouchLegCharacter_Face(ref chr, aref out)
{
	out.image = "";
	if (!CheckAttribute(chr, "FaceId")) return;
	out.image.kind = "portrait";
	out.image.image = chr.FaceId;
}

// The key skill shown for a post: of the skills the game lends the main character from that post (GetSkillOfficerPost,
// a script fix of TEHO 1.7.3), the one this person has best. A post that lends none (the fighters) shows the best
// blade; so does every post in a game that cannot say.
string TouchLegCharacter_PostSkill(aref ui, ref chr, string post)
{
	string best = "";
	if (sti(ui.data.optional.skill_post))
	{
		for (int k = 1; GetSkillNameByIdx(k) != ""; k++)
		{
			string skill = GetSkillNameByIdx(k);
			if (TouchAdpSkillPost_Of(skill) != post) continue;
			if (best == "" || GetSkillValue(chr, "Skill", skill) > GetSkillValue(chr, "Skill", best)) best = skill;
		}
	}
	if (best != "") return best;
	best = "FencingL";
	if (GetSkillValue(chr, "Skill", "FencingS") > GetSkillValue(chr, "Skill", best)) best = "FencingS";
	if (GetSkillValue(chr, "Skill", "FencingH") > GetSkillValue(chr, "Skill", best)) best = "FencingH";
	return best;
}

void TouchLegCharacter_Card(aref ui, ref chr, aref out)
{
	aref model = TouchLegLib_Model(ui);
	aref card;
	makearef(card, out.card);
	card.name = TouchLib_Trim(TouchLegLib_Label(model, "HERO_NAME"));
	card.rank = "";
	card.rankValue = "";
	card.healthWord = "";
	card.healthCaption = "";
	card.healthBad = false;
	aref bar;
	makearef(bar, card.health);
	TouchLegCharacter_Bar("", "", bar);
	makearef(bar, card.energy);
	TouchLegCharacter_Bar("", "", bar);
	aref facts;
	makearef(facts, card.facts);
	for (int k = 0; k < TouchLib_Count(model, "TABLE_OTHER.rows"); k++)
	{
		aref row = TouchLib_Item(model, "TABLE_OTHER.rows", k);
		string caption = TouchLib_Trim(TouchLegLib_Cell(row, 1));
		string value = TouchLib_Trim(TouchLegLib_Cell(row, 2));
		if (row.id == "Rank") { card.rank = caption; card.rankValue = value; }
		if (row.id == "Life") { makearef(bar, card.health); TouchLegCharacter_Bar(value, "Life", bar); continue; }
		if (row.id == "Energy") { makearef(bar, card.energy); TouchLegCharacter_Bar(value, "Energy", bar); continue; }
		if (row.id == "Health")
		{
			// The game colours a bad health word (character_all.c SetVariable): not white, little green.
			card.healthWord = value;
			card.healthCaption = caption;
			card.healthBad = false;
			if (CheckAttribute(row, "cells.c2.color"))
			{
				string color = row.cells.c2.color;
				card.healthBad = color != "ffffff" && findSubStr("01234567", TouchLib_Cut(color, 2, 3), 0) >= 0;
			}
			continue;
		}
		if (value == "") continue;
		aref fact = TouchLib_Add(facts, "f");
		fact.caption = caption;
		fact.value = value;
	}
	card.reputation = TouchLib_Trim(TouchLegLib_Label(model, "REPUTATION_STR"));
	card.portrait = "";
	aref portrait;
	makearef(portrait, card.portrait);
	TouchLegLib_NodeImage(model, "CHARACTER_BIG_PICTURE", portrait);
	// The game's LOYALITY bar: the hero's authority (character_all.c SetVariable), an officer's loyalty word.
	card.standing.caption = TouchLib_Trim(TouchLegLib_Label(model, "LOYALITY_STR"));
	card.standing.value = "";
	card.standing.authority = false;
	card.salary = 0;
	if (sti(chr.index) == GetMainCharacterIndex())
	{
		if (CheckAttribute(chr, "reputation.authority"))
		{
			card.standing.value = chr.reputation.authority;
			card.standing.authority = true;
		}
		return;
	}
	card.standing.value = TouchLegCharacter_Loyalty(chr);
	card.salary = GetMoneyForOfficerFull(chr);
}

// A skill table's bonus column (character_all.c FillSkillTables): "" when the value used is the base, else "(+N)"
// or "(-N)". Anything else the game never writes.
int TouchLegCharacter_Bonus(string text, string what)
{
	text = TouchLib_Trim(text);
	if (text == "") return 0;
	int n = strlen(text);
	string sign = TouchLib_Cut(text, 1, 2);
	if (n < 4 || TouchLib_Cut(text, 0, 1) != "(" || TouchLib_Cut(text, n - 1, n) != ")" || (sign != "+" && sign != "-"))
		UiError("character: the bonus of " + what + " is '" + text + "', not (+N) or (-N)");
	int value = UiAmount(TouchLib_Cut(text, 2, n - 1), what);
	if (sign == "-") return -value;
	return value;
}

void TouchLegCharacter_Table(aref ui, string table, aref out)
{
	aref model = TouchLegLib_Model(ui);
	string node = TouchLegCharacter_Node(table);
	bool special = table == "special";
	aref at;
	makearef(at, out.tables.(table));
	at.heading = "";
	if (table == "personal") at.heading = TouchLegCharacter_Group(model, "TABSTR_1");
	if (table == "ship") at.heading = TouchLegCharacter_Group(model, "TABSTR_2");
	aref rows;
	makearef(rows, at.rows);
	for (int k = 0; k < TouchLib_Count(model, node + ".rows"); k++)
	{
		aref row = TouchLib_Item(model, node + ".rows", k);
		string id = row.id;
		aref entry = TouchLib_Add(rows, "r");
		entry.id = id;
		entry.name = TouchLib_Trim(TouchLegLib_Cell(row, 1));
		entry.icon = "";
		if (!special && CheckAttribute(ui, "data.skillIcons." + id)) entry.icon = ui.data.skillIcons.(id);
		entry.added = 0;
		if (CheckAttribute(&TouchLegCharacter, "added." + id)) entry.added = TouchLegCharacter.added.(id);
		if (special)
		{
			entry.experience = "";
			entry.bonus = TouchLegCharacter_Bonus(TouchLegLib_Cell(row, 2), node + " " + id);
			entry.base = UiAmount(TouchLegLib_Cell(row, 3), node + " " + id);
			continue;
		}
		// A skill at its maximum has no experience left to gain: the game writes no number.
		entry.experience = TouchLib_Trim(TouchLegLib_Cell(row, 2));
		entry.bonus = TouchLegCharacter_Bonus(TouchLegLib_Cell(row, 3), node + " " + id);
		entry.base = UiAmount(TouchLegLib_Cell(row, 4), node + " " + id);
	}
}

// The picture of a perk: our token where the binding names one, else the table row's, else the game's atlas.
void TouchLegCharacter_PerkImage(aref ui, aref model, string perk, string key, bool learned, aref out)
{
	if (CheckAttribute(ui, "data.perkIcons." + perk))
	{
		out.kind = "token";
		out.icon = ui.data.perkIcons.(perk);
		out.enamel = "iron";
		if (learned) out.enamel = "red";
		return;
	}
	if (key != "")
	{
		aref row;
		makearef(row, model.TABLE_PERKS.rows.(key));
		if (TouchLegLib_Image(row.cells.c0, out)) return;
	}
	out.kind = "atlas";
	out.group = "PERK_DISABLE";
	if (learned) out.group = "PERK_ENABLE";
	out.image = perk;
}

// The perks of the shown group's table and, as the game links them (ChrPerksList.list.<perk>.condition: every one
// of them needed), each perk they need, also of the other group, breadth first in the table's order.
void TouchLegCharacter_Perks(aref ui, ref chr, aref out)
{
	aref model = TouchLegLib_Model(ui);
	aref nodes, edges;
	makearef(nodes, out.perks.nodes);
	makearef(edges, out.perks.edges);
	object seen; // perk id -> listed (attribute names compare without case, as the game's ids do)
	for (int k = 0; k < TouchLib_Count(model, "TABLE_PERKS.rows"); k++)
	{
		aref row = TouchLib_Item(model, "TABLE_PERKS.rows", k);
		string id = row.id;
		if (CheckAttribute(&seen, id)) continue;
		seen.(id) = true;
		aref node = TouchLib_Add(nodes, "n");
		node.id = id;
		node.key = GetAttributeName(row);
	}
	bool hero = sti(chr.index) == GetMainCharacterIndex();
	for (int n = 0; n < GetAttributesNum(nodes); n++)
	{
		node = GetAttributeN(nodes, n);
		string perk = node.id;
		if (!CheckAttribute(&ChrPerksList, "list." + perk)) UiError("character: the game has no perk " + perk);
		node.table = node.key != "";
		// The table's own caption names its perks; the game's text the others.
		node.label = GetConvertStr(perk, "AbilityDescribe.txt");
		if (node.key != "")
		{
			string key = node.key;
			aref listed;
			makearef(listed, model.TABLE_PERKS.rows.(key));
			node.label = TouchLib_Trim(TouchLegLib_Cell(listed, 1));
		}
		node.text = TouchLegLib_StockText(GetConvertStr("perk" + perk, "AbilityDescribe.txt"));
		node.ship = CheckAttribute(&ChrPerksList, "list." + perk + ".BaseType") && ChrPerksList.list.(perk).BaseType == "ship";
		node.learned = CheckCharacterPerk(chr, perk);
		node.officer = hero && !sti(node.learned) && GetOfficersPerkUsing(chr, perk);
		aref image;
		makearef(image, node.image);
		TouchLegCharacter_PerkImage(ui, model, perk, node.key, sti(node.learned), image);
		if (!CheckAttribute(&ChrPerksList, "list." + perk + ".condition")) continue;
		aref conditions;
		makearef(conditions, ChrPerksList.list.(perk).condition);
		for (int c = 0; c < GetAttributesNum(conditions); c++)
		{
			string need = GetAttributeName(GetAttributeN(conditions, c));
			aref edge = TouchLib_Add(edges, "e");
			edge.from = need;
			edge.to = perk;
			edge.met = CheckCharacterPerk(chr, need);
			if (CheckAttribute(&seen, need)) continue;
			seen.(need) = true;
			aref more = TouchLib_Add(nodes, "n");
			more.id = need;
			more.key = "";
		}
	}
	for (n = 0; n < GetAttributesNum(nodes); n++)
	{
		node = GetAttributeN(nodes, n);
		DeleteAttribute(node, "key");
	}
}

// What the game's answer to a perk depends on: whose, the group, the free points and the perks learned.
string TouchLegCharacter_LearnKey(aref out, string perk)
{
	string group = "self";
	if (sti(out.groupShip)) group = "ship";
	int learned = 0;
	for (int n = 0; n < TouchLib_Count(out, "perks.nodes"); n++)
	{
		aref node = TouchLib_Item(out, "perks.nodes", n);
		if (sti(node.learned)) learned++;
	}
	return out.character + "/" + group + "/" + perk + "/" + out.free.(group) + "/" + learned;
}

// The officers on their posts (CHARACTERS_SCROLL slots that are posts, character_all.h FillCharactersScroll: a free
// one has no character) and the passengers that hold none.
void TouchLegCharacter_Posts(aref ui, aref out)
{
	aref model = TouchLegLib_Model(ui);
	aref posts;
	makearef(posts, out.posts);
	for (int k = 0; k < TouchLib_Count(model, "CHARACTERS_SCROLL.rows"); k++)
	{
		aref row = TouchLib_Item(model, "CHARACTERS_SCROLL.rows", k);
		int slot = sti(row.index);
		string post = GetOfficerTypeByNum(slot);
		if (post == "") continue; // the captain, companions, passengers and prisoners
		string pic = "pic" + (slot + 1);
		aref at = TouchLib_Add(posts, "p");
		at.slot = slot;
		at.post = post;
		at.role = GameInterface.CHARACTERS_SCROLL.(pic).str1;
		if (TouchLib_StartsWith(at.role, "#")) at.role = TouchLib_Cut(at.role, 1, strlen(at.role));
		// Our icon of the post: the binding's, a fighter's by its place (three fighters, three blades).
		at.icon = ui.data.fighters.("f" + (slot - (slot / 3) * 3));
		if (CheckAttribute(ui, "data.postIcons." + post)) at.icon = ui.data.postIcons.(post);
		at.character = sti(GameInterface.CHARACTERS_SCROLL.(pic).character);
		at.label = "";
		at.image = "";
		if (sti(at.character) <= 0) { at.character = -1; continue; }
		ref chr = GetCharacter(sti(at.character));
		at.label = GetFullName(chr);
		TouchLegCharacter_Face(chr, at);
	}
	aref passengers;
	makearef(passengers, out.passengers);
	ref mc = GetMainCharacter();
	for (int i = 0; i < GetPassengersQuantity(mc); i++)
	{
		int idx = GetPassenger(mc, i);
		if (idx < 0) continue;
		chr = GetCharacter(idx);
		if (CheckAttribute(chr, "prisoned") && sti(chr.prisoned)) continue;
		if (CheckAttribute(chr, "isfree") && sti(chr.isfree) > 0) continue;
		at = TouchLib_Add(passengers, "p");
		at.label = GetFullName(chr);
		at.rank = chr.rank;
		TouchLegCharacter_Face(chr, at);
	}
}

// The selected post of the list (its slot), -1 none.
int TouchLegCharacter_PostShown(aref model)
{
	int k = TouchLegLib_Selected(model, "CHARACTERS_SCROLL");
	if (k < 0) return -1;
	aref row = TouchLib_Item(model, "CHARACTERS_SCROLL.rows", k);
	return sti(row.index);
}

void TouchLegCharacter_Windows(aref ui, aref out)
{
	aref model = TouchLegLib_Model(ui);
	out.window = TouchLegCharacter_Window(model);
	int slot = TouchLegCharacter_PostShown(model);
	string role = "";
	string post = "";
	if (slot >= 0)
	{
		string pic = "pic" + (slot + 1);
		role = GameInterface.CHARACTERS_SCROLL.(pic).str1;
		if (TouchLib_StartsWith(role, "#")) role = TouchLib_Cut(role, 1, strlen(role));
		post = GetOfficerTypeByNum(slot);
	}
	// The game opens both on the scroll's current post, which its list always has.
	if ((out.window == "remove" || out.window == "assign") && slot < 0)
		UiError("character: the officer window " + out.window + " is open on no post");
	if (out.window == "remove")
	{
		int idx = sti(GameInterface.CHARACTERS_SCROLL.(pic).character);
		ref chr = GetCharacter(idx);
		string skill = TouchLegCharacter_PostSkill(ui, chr, post);
		object remove = {slot = slot, label = GetFullName(chr), role = role, skill = XI_ConvertString(skill),
		                 skillValue = GetSkillValue(chr, "Skill", skill), loyalty = TouchLegCharacter_Loyalty(chr),
		                 salary = GetMoneyForOfficerFull(chr), can = UiOffers("REMOVE_ACCEPT_OFFICER", "press")};
		aref at;
		makearef(at, out.remove);
		CopyAttributes(at, &remove);
	}
	if (out.window == "assign")
	{
		out.assign.role = role;
		aref rows;
		makearef(rows, out.assign.rows);
		for (int k = 0; k < TouchLib_Count(model, "PASSENGERSLIST.rows"); k++)
		{
			aref row = TouchLib_Item(model, "PASSENGERSLIST.rows", k);
			pic = "pic" + (sti(row.index) + 1);
			if (!CheckAttribute(&GameInterface, "PASSENGERSLIST." + pic + ".character"))
				UiError("character: the post chooser has no person of its row " + row.index);
			chr = GetCharacter(sti(GameInterface.PASSENGERSLIST.(pic).character));
			skill = TouchLegCharacter_PostSkill(ui, chr, post);
			aref entry = TouchLib_Add(rows, "r");
			entry.label = GetFullName(chr);
			entry.rank = chr.rank;
			entry.salary = GetMoneyForOfficerFull(chr);
			entry.skill = XI_ConvertString(skill);
			entry.skillValue = GetSkillValue(chr, "Skill", skill);
			entry.busy = TouchLegCharacter.step.kind == "assign" && sti(TouchLegCharacter.step.what) == k;
			TouchLegCharacter_Face(chr, entry);
		}
	}
	if (out.window == "message")
	{
		out.message.title = TouchLib_Trim(TouchLegLib_Label(model, "MSG_WINDOW_CAPTION"));
		out.message.text = TouchLib_Trim(TouchLegLib_Label(model, "MSG_WINDOW_TEXT"));
	}
}

// The game's own answer buttons of its start window (the PIRATES points of a new hero).
void TouchLegCharacter_Actions(aref ui, aref out)
{
	aref model = TouchLegLib_Model(ui);
	aref actions;
	makearef(actions, out.actions);
	object nodes = {a0 = "GAME_CANCEL", a1 = "GAME_ACCEPT"};
	object ids = {a0 = "game-cancel", a1 = "game-accept"};
	for (int k = 0; k < 2; k++)
	{
		string key = "a" + k;
		string node = nodes.(key);
		if (!CheckAttribute(model, node)) continue;
		aref at = TouchLib_Add(actions, "a");
		at.id = ids.(key);
		at.label = UiOneLine(TouchLegLib_Label(model, node));
		at.primary = k == 1;
		at.can = UiOffers(node, "press");
	}
}

void TouchAdpCharacter_State(aref ui, aref out)
{
	aref model = TouchLegLib_Model(ui);
	if (UiFirst()) DeleteAttribute(&TouchLegCharacter, "");
	if (!CheckAttribute(&TouchLegCharacter, "step")) TouchLegCharacter_Step("", "", "");
	out.title = UiOneLine(TouchLegLib_Label(model, "TITLE"));
	out.full = sti(ui.data.full);
	out.sections = TouchLegLib_Sections(model);
	// Salaries divide by the hero's skills, which the game keeps for a while with the posts of that moment: dropped
	// as the game's own windows drop it before they fill their tables (character_all.c FillSkillTables).
	DelBakSkillAttr(GetMainCharacter());
	int character;
	out.shown = -1;
	out.waiting = false;
	if (sti(out.full))
	{
		int listed = TouchLegLib_People(model, out);
		out.shown = TouchLegCharacter_Shown(model, out);
		aref person = TouchLib_Item(out, "people", sti(out.shown));
		character = sti(person.character);
		out.waiting = listed >= 0 && listed != sti(out.shown);
		if (sti(out.waiting))
		{
			// The game shows the person of the slot picked in its next frame.
			string answer = UiAnswer();
			if (answer == "waiting" || answer == "applied") UiAwait();
		}
	}
	else character = UiGameInt("xi_refCharacter.index");
	out.character = character;
	TouchLegCharacter.character = character;
	ref chr = GetCharacter(character);
	TouchLegCharacter_Card(ui, chr, out);
	TouchLegCharacter_Actions(ui, out);
	out.free.skill = 0;
	if (TouchLegLib_Label(model, "FREE_SKILL") != "") out.free.skill = UiAmount(TouchLegLib_Label(model, "FREE_SKILL"), "FREE_SKILL");
	out.free.special = 0;
	if (TouchLegLib_Label(model, "FREE_SPECIAL") != "")
		out.free.special = UiAmount(TouchLegLib_Label(model, "FREE_SPECIAL"), "FREE_SPECIAL");
	out.free.self = 0;
	out.free.ship = 0;
	if (CheckAttribute(chr, "perks.FreePoints_self")) out.free.self = sti(chr.perks.FreePoints_self);
	if (CheckAttribute(chr, "perks.FreePoints_ship")) out.free.ship = sti(chr.perks.FreePoints_ship);
	out.next.self.exp = 0;
	out.next.ship.exp = 0;
	if (CheckAttribute(chr, "perks.FreePoints_self_exp")) out.next.self.exp = sti(chr.perks.FreePoints_self_exp);
	if (CheckAttribute(chr, "perks.FreePoints_ship_exp")) out.next.ship.exp = sti(chr.perks.FreePoints_ship_exp);
	out.next.self.rate = GetFreePoints_SelfRate(chr);
	out.next.ship.rate = GetFreePoints_ShipRate(chr);
	out.groups.self = TouchLegCharacter_Group(model, "TABSTR_1");
	out.groups.ship = "";
	if (CheckAttribute(model, "TABBTN_2")) out.groups.ship = TouchLegCharacter_Group(model, "TABSTR_2");
	out.groupShip = CheckAttribute(model, "TABBTN_2") && model.TABBTN_2.value == "1";
	TouchLegCharacter_Table(ui, "special", out);
	TouchLegCharacter_Table(ui, "personal", out);
	TouchLegCharacter_Table(ui, "ship", out);
	// Characteristics take points in the start window only, while the game offers its rows.
	out.specialPoints = false;
	if ((sti(out.free.special) > 0 || CheckAttribute(model, "GAME_ACCEPT")) && TouchLib_Count(model, "TABLE_SPECIAL.rows") > 0)
	{
		aref first = TouchLib_Item(model, "TABLE_SPECIAL.rows", 0);
		out.specialPoints = CheckAttribute(first, "offers.select");
	}
	TouchLegCharacter_Perks(ui, chr, out);
	TouchLegCharacter_Posts(ui, out);
	TouchLegCharacter_Windows(ui, out);
	if (TouchLegCharacter_Follow(ui, out)) UiAwait();
	out.learning = TouchLegCharacter.step.kind == "learn";
	out.refused = "";
	if (CheckAttribute(&TouchLegCharacter, "refused.key"))
		if (TouchLegCharacter.refused.key == TouchLegCharacter_LearnKey(out, TouchLegCharacter.refused.perk))
			out.refused = TouchLegCharacter.refused.perk;
}

// The next step of the move asked for, in this model; true when one was sent (its answer comes in a later frame).
bool TouchLegCharacter_Follow(aref ui, aref out)
{
	aref model = TouchLegLib_Model(ui);
	string answer = UiAnswer();
	if (answer == "" || answer == "refused") TouchLegCharacter_Step("", "", "");
	string kind = TouchLegCharacter.step.kind;
	string what = TouchLegCharacter.step.what;
	string window = out.window;
	// The game's perk window is a step of learning and never drawn by itself: one left open is closed.
	if (kind == "") return window == "perk" && TouchLegCharacter_CloseWindow(model);
	if (answer == "waiting") return false;
	string key;
	if (kind == "skill")
	{
		string node = TouchLegCharacter_Node(TouchLegCharacter.step.table);
		key = TouchLegCharacter_Row(model, node, what);
		if (key == "") UiError("character: the skill row " + what + " is gone");
		if (!TouchLegCharacter_Selected(model, node, key)) return false;
		bool up = sti(TouchLegCharacter.step.up);
		TouchLegCharacter_Step("", "", "");
		return TouchLegCharacter_Arrow(what, up);
	}
	if (kind == "learn")
	{
		key = TouchLegCharacter_Row(model, "TABLE_PERKS", what);
		bool chosen = TouchLegCharacter_Selected(model, "TABLE_PERKS", key);
		if (window == "perk")
		{
			TouchLegCharacter_Step("", "", "");
			if (chosen && UiOffers("PERK_ACCEPT", "press")) { UiSend("PERK_ACCEPT", "press", ""); return true; }
			// The game's answer: not now. The button stays off in this state.
			if (chosen)
			{
				TouchLegCharacter.refused.perk = what;
				TouchLegCharacter.refused.key = TouchLegCharacter_LearnKey(out, what);
			}
			return TouchLegCharacter_CloseWindow(model);
		}
		if (window != "") return TouchLegCharacter_CloseWindow(model);
		if (key == "") UiError("character: the perk " + what + " is not in the game's list");
		if (!chosen || TouchLegCharacter.step.stage == "open") return false;
		TouchLegCharacter.step.stage = "open";
		TouchLegCharacter_Send(model, "TABLE_PERKS", key, "activate");
		return true;
	}
	if (kind == "post")
	{
		if (window == "remove" || window == "assign") { TouchLegCharacter_Step("", "", ""); return false; }
		if (window != "") return TouchLegCharacter_CloseWindow(model);
		if (TouchLegCharacter_PostShown(model) != sti(what) || TouchLegCharacter.step.stage == "open") return false;
		TouchLegCharacter.step.stage = "open";
		TouchLegCharacter_Send(model, "CHARACTERS_SCROLL", "r" + what, "activate");
		return true;
	}
	if (kind == "assign")
	{
		// The chooser closed (assigned, or the game cancelled it), or its rows are now of another post or person: a
		// tap of then is not carried to someone else.
		if (window != "assign" || TouchLegCharacter_PostShown(model) != sti(TouchLegCharacter.step.post))
		{
			TouchLegCharacter_Step("", "", "");
			return false;
		}
		aref row = TouchLib_Item(model, "PASSENGERSLIST.rows", sti(what));
		string pic = "pic" + (sti(row.index) + 1);
		if (sti(GameInterface.PASSENGERSLIST.(pic).character) != sti(TouchLegCharacter.step.character))
		{
			TouchLegCharacter_Step("", "", "");
			return false;
		}
		if (row.value != "1") return false;
		TouchLegCharacter_Step("", "", "");
		if (!UiOffers("ACCEPT_ADD_OFFICER", "press")) UiError("character: the post chooser refuses its row " + what);
		UiSend("ACCEPT_ADD_OFFICER", "press", "");
		return true;
	}
	if (kind == "section")
	{
		if (window != "") return TouchLegCharacter_CloseWindow(model);
		TouchLegCharacter_Step("", "", "");
		TouchLegLib_Section(model, what);
		return true;
	}
	return false;
}

// The game's arrow of the selected skill row pressed; false where it offers none (the limit is the game's).
bool TouchLegCharacter_Arrow(string id, bool up)
{
	string node = "B_SKILLDOWN";
	if (up) node = "B_SKILLUP";
	if (!UiOffers(node, "press")) return false;
	UiSend(node, "press", "");
	int added = 0;
	if (CheckAttribute(&TouchLegCharacter, "added." + id)) added = sti(TouchLegCharacter.added.(id));
	if (up) added++;
	else added--;
	TouchLegCharacter.added.(id) = added;
	return true;
}

// What the value of a skill row is made of: the game's own steps, recorded while it computes the value (while
// chr.SkillTrace exists GetCharacterSPECIALSimple / GetCharacterSkillSimple record their running value after each
// step), then the two steps outside those functions: an officer's higher skill taking the hero's place and a ship of
// a class the navigation does not reach.
void TouchAdpCharacter_Help(aref ui, string table, int k, aref out)
{
	aref model = TouchLegLib_Model(ui);
	string node = TouchLegCharacter_Node(table);
	aref row = TouchLib_Item(model, node + ".rows", k);
	string id = row.id;
	out.text = TouchLegLib_StockText(GetRPGText(id));
	out.base = "";
	aref steps;
	makearef(steps, out.steps);
	ref chr = GetCharacter(sti(TouchLegCharacter.character));
	bool special = table == "special";
	DelBakSkillAttr(chr);
	DeleteAttribute(chr, "SkillTrace");
	chr.SkillTrace = id;
	int own;
	if (special) own = GetCharacterSPECIALSimple(chr, id);
	else own = GetCharacterSkillSimple(chr, id);
	// The recorded steps are read before any other skill function runs (each would record into them).
	object recorded;
	aref traced;
	makearef(traced, chr.SkillTrace);
	CopyAttributes(&recorded, traced);
	DeleteAttribute(chr, "SkillTrace");
	int n = GetAttributesNum(&recorded);
	if (n == 0) return; // no such characteristic or skill: the game's functions return a constant
	aref step = GetAttributeN(&recorded, 0);
	if (GetAttributeValue(step) != "base") UiError("character: the first step of " + id + " is " + GetAttributeValue(step));
	out.base = step.value;
	int prev = sti(step.value);
	string last = "";
	aref line;
	int idLngFile = LanguageOpenFile("ItemsDescribe.txt");
	for (int i = 1; i < n; i++)
	{
		step = GetAttributeN(&recorded, i);
		int value = sti(step.value);
		if (value == prev) continue;
		string source = GetAttributeValue(step);
		// one source spread over several of the game's lines (an item raising four skills) is one line
		if (source == last)
		{
			line.value = sti(line.value) + value - prev;
			prev = value;
			continue;
		}
		line = TouchLegCharacter_StepLine(steps, value - prev);
		TouchLegCharacter_Source(line, chr, source, idLngFile);
		last = source;
		prev = value;
	}
	LanguageCloseFile(idLngFile);
	int result = own;
	if (!special)
	{
		result = GetSummonSkillFromNameSimple(chr, id);
		if (result != own)
		{
			line = TouchLegCharacter_StepLine(steps, result - own);
			line.kind = "officer";
			int officer = TouchLegCharacter_Officer(ui, id);
			if (officer >= 0) line.label = GetFullName(GetCharacter(officer));
		}
		own = result;
		result = ApplayNavyPenaltyToSkill(chr, id, own);
	}
	else result = ApplayNavyPenalty(chr, id, own);
	if (result != own)
	{
		line = TouchLegCharacter_StepLine(steps, result - own);
		line.kind = "navy";
		line.a1 = GetCharacterShipClass(chr);
		line.a2 = GetSummonSkillFromNameSimple(chr, "Sailing");
		line.a3 = GetShipClassNavySkill(GetCharacterShipClass(chr));
	}
}

aref TouchLegCharacter_StepLine(aref steps, int value)
{
	object fresh = {kind = "", label = "", value = value, a1 = "", a2 = "", a3 = ""};
	aref line = TouchLib_Add(steps, "s");
	CopyAttributes(line, &fresh);
	return line;
}

// A recorded step's kind (the screen words it) and, for an item (worn, carried, the suit, the blade), the game's name
// of the item in the player's language.
void TouchLegCharacter_Source(aref line, ref chr, string source, int idLngFile)
{
	string item = "";
	if (TouchLib_StartsWith(source, "item:")) item = TouchLib_Cut(source, 5, strlen(source));
	if (source == "suit") item = GetCharacterEquipSuitID(chr);
	if (source == "blade") item = GetCharacterEquipByGroup(chr, "blade");
	if (item != "")
	{
		line.kind = "item";
		int idx = FindItem(item);
		if (idx < 0) UiError("character: no item " + item + " (step " + source + ")");
		line.label = LanguageConvertString(idLngFile, Items[idx].name);
		return;
	}
	line.kind = source; // health, mangarosa, curse, drunk, herotype, overload, limit
	if (source == "health") line.a1 = GetHealthNum(chr);
}

// The officer whose skill GetSummonSkillFromNameSimple took: the holder of the post the game lends that skill from
// (GetSkillOfficerPost); -1 in a game that cannot say, whose step then names no one.
int TouchLegCharacter_Officer(aref ui, string id)
{
	if (!sti(ui.data.optional.skill_post)) return -1;
	string post = TouchAdpSkillPost_Of(id);
	ref mc = GetMainCharacter();
	if (post == "" || !CheckAttribute(mc, "Fellows.Passengers." + post))
		UiError("character: the game lends " + id + " from no post it has");
	return sti(mc.Fellows.Passengers.(post));
}

void TouchAdpCharacter_Person(aref ui, int person)
{
	aref model = TouchLegLib_Model(ui);
	object state;
	TouchLegLib_People(model, &state);
	TouchLegCharacter_Step("", "", "");
	TouchLegLib_PickPerson(model, &state, person);
}

void TouchAdpCharacter_Group(aref ui, bool ship)
{
	string node = "TABBTN_1";
	if (ship) node = "TABBTN_2";
	if (TouchLegCharacter.step.kind == "learn") TouchLegCharacter_Step("", "", "");
	UiSend(node, "press", "");
}

void TouchAdpCharacter_Skill(aref ui, string table, int k, bool up)
{
	aref model = TouchLegLib_Model(ui);
	string node = TouchLegCharacter_Node(table);
	aref row = TouchLib_Item(model, node + ".rows", k);
	if (row.value == "1")
	{
		TouchLegCharacter_Arrow(row.id, up);
		return;
	}
	TouchLegCharacter_Step("skill", row.id, "");
	TouchLegCharacter.step.table = table;
	TouchLegCharacter.step.up = up;
	TouchLegCharacter_Send(model, node, GetAttributeName(row), "select");
}

void TouchAdpCharacter_Learn(aref ui, string perk)
{
	aref model = TouchLegLib_Model(ui);
	string key = TouchLegCharacter_Row(model, "TABLE_PERKS", perk);
	if (key == "") UiError("character: the perk " + perk + " is not in the game's list");
	if (TouchLegCharacter_Selected(model, "TABLE_PERKS", key))
	{
		TouchLegCharacter_Step("learn", perk, "open");
		TouchLegCharacter_Send(model, "TABLE_PERKS", key, "activate");
		return;
	}
	TouchLegCharacter_Step("learn", perk, "");
	TouchLegCharacter_Send(model, "TABLE_PERKS", key, "select");
}

void TouchAdpCharacter_Post(aref ui, int slot)
{
	aref model = TouchLegLib_Model(ui);
	string key = "r" + slot;
	if (!CheckAttribute(model, "CHARACTERS_SCROLL.rows." + key)) UiError("character: the character list has no slot " + slot);
	if (TouchLegCharacter_PostShown(model) == slot)
	{
		TouchLegCharacter_Step("post", slot, "open");
		TouchLegCharacter_Send(model, "CHARACTERS_SCROLL", key, "activate");
		return;
	}
	TouchLegCharacter_Step("post", slot, "");
	TouchLegCharacter_Send(model, "CHARACTERS_SCROLL", key, "select");
}

void TouchAdpCharacter_Assign(aref ui, int candidate)
{
	aref model = TouchLegLib_Model(ui);
	aref row = TouchLib_Item(model, "PASSENGERSLIST.rows", candidate);
	string pic = "pic" + (sti(row.index) + 1);
	TouchLegCharacter_Step("assign", candidate, "");
	TouchLegCharacter.step.character = sti(GameInterface.PASSENGERSLIST.(pic).character);
	TouchLegCharacter.step.post = TouchLegCharacter_PostShown(model);
	if (row.value == "1")
	{
		TouchLegCharacter_Step("", "", "");
		UiSend("ACCEPT_ADD_OFFICER", "press", "");
		return;
	}
	TouchLegCharacter_Send(model, "PASSENGERSLIST", GetAttributeName(row), "select");
}

void TouchAdpCharacter_Dismiss(aref ui)
{
	UiSend("REMOVE_ACCEPT_OFFICER", "press", "");
}

void TouchAdpCharacter_Close(aref ui)
{
	TouchLegCharacter_Step("", "", "");
	TouchLegCharacter_CloseWindow(TouchLegLib_Model(ui));
}

void TouchAdpCharacter_Act(aref ui, string action)
{
	if (action == "game-accept") UiSend("GAME_ACCEPT", "press", "");
	else UiSend("GAME_CANCEL", "press", "");
}

void TouchAdpCharacter_Section(aref ui, string section)
{
	aref model = TouchLegLib_Model(ui);
	if (TouchLegCharacter_CloseWindow(model))
	{
		TouchLegCharacter_Step("section", section, "");
		return;
	}
	TouchLegCharacter_Step("", "", "");
	TouchLegLib_Section(model, section);
}

void TouchAdpCharacter_Leave(aref ui)
{
	if (CheckAttribute(ui, "model.EXIT_BTN")) { if (UiOffers("EXIT_BTN", "press")) { UiSend("EXIT_BTN", "press", ""); return; } }
	UiBack("");
}
