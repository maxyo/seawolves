// SPDX-License-Identifier: GPL-3.0-only
// What every adapter of this lineage reads the game's interfaces by: captions, the cells of a TABLE_LIST, the rows of
// a choice, the game's amount window filled and confirmed, the purse caption, the hero sections and the game's
// descriptions.

aref TouchLegLib_Model(aref ui)
{
	aref model;
	makearef(model, ui.model);
	return model;
}

// The first of the window's exit buttons `nodes` (names between spaces) it has, "" for none: what UiBack leaves by.
string TouchLegLib_Exit(aref model, string nodes)
{
	object names;
	TouchLib_Words(nodes, &names);
	for (int k = 0; k < GetAttributesNum(&names); k++)
	{
		aref name = GetAttributeN(&names, k);
		if (CheckAttribute(model, name.text)) return name.text;
	}
	return "";
}

// A button of the window as the game offers it: out.{label (its caption), can (it offers a press), reason}; no
// caption, false and no reason without the node (or without a node named).
void TouchLegLib_Button(aref model, string node, aref out)
{
	out.label = "";
	out.can = false;
	out.reason = "";
	if (node == "" || !CheckAttribute(model, node)) return;
	out.label = model.(node).label;
	out.can = UiOffers(node, "press");
	out.reason = model.(node).reason;
}

// The game's open question of a menu window, as its nodes {yes, no, text, names (its "no" has the answer's caption)}:
// CONFIRM_WINDOW_MB_YES/NO with CONFIRM_WINDOW_STRINGS, CONFIRM_EXIT_YES/NO with CONFIRM_EXIT_TEXT (whose "no"
// caption is not the answer's), CONFIRM_YES/NO with CONFIRM_TEXT, a notice CONFIRM_YES1 with CONFIRM_TEXT (one answer,
// no "no"). False while none is open.
bool TouchLegLib_Question(aref model, aref out)
{
	if (TouchLegLib_QuestionOf(model, out, "CONFIRM_WINDOW_MB_YES", "CONFIRM_WINDOW_MB_NO", "CONFIRM_WINDOW_STRINGS", true)) return true;
	if (TouchLegLib_QuestionOf(model, out, "CONFIRM_EXIT_YES", "CONFIRM_EXIT_NO", "CONFIRM_EXIT_TEXT", false)) return true;
	if (TouchLegLib_QuestionOf(model, out, "CONFIRM_YES", "CONFIRM_NO", "CONFIRM_TEXT", true)) return true;
	return TouchLegLib_QuestionOf(model, out, "CONFIRM_YES1", "", "CONFIRM_TEXT", true);
}

bool TouchLegLib_QuestionOf(aref model, aref out, string yes, string no, string text, bool names)
{
	if (!CheckAttribute(model, yes)) return false;
	out.yes = yes;
	out.no = no;
	out.text = text;
	out.names = names;
	return true;
}

// The open question as out.{question (its lines), no (the game's caption of its "no", "" none), canNo}.
void TouchLegLib_Asked(aref model, aref out)
{
	object set;
	if (!TouchLegLib_Question(model, &set)) UiError("no question of the game is open");
	object lines;
	TouchLegLib_Lines(model, set.text, &lines, "l");
	string text = "";
	for (int k = 0; k < GetAttributesNum(&lines); k++)
	{
		aref line = GetAttributeN(&lines, k);
		if (k > 0) text += TouchLib_Newline();
		text += line.text;
	}
	out.question = text;
	out.canNo = set.no != "" && CheckAttribute(model, set.no);
	out.no = "";
	if (sti(set.names)) out.no = TouchLegLib_Label(model, set.no);
}

// The node of the open question's answer.
string TouchLegLib_Reply(aref model, bool yes)
{
	object set;
	if (!TouchLegLib_Question(model, &set)) UiError("no question of the game is open");
	if (yes) return set.yes;
	return set.no;
}

// The caption of an optional node, "" without it (or without a node named).
string TouchLegLib_Label(aref model, string node)
{
	if (node == "") return "";
	if (!CheckAttribute(model, node)) return "";
	return model.(node).label;
}

// The texts of an optional node in its order as {text} entries <prefix><n> of `out`: the rows of a multi-line text,
// else its label; an empty one is left out.
void TouchLegLib_Lines(aref model, string node, aref out, string prefix)
{
	if (node == "") return;
	if (!CheckAttribute(model, node)) return;
	aref line;
	if (!CheckAttribute(model, node + ".rows"))
	{
		if (model.(node).label == "") return;
		line = TouchLib_Add(out, prefix);
		line.text = model.(node).label;
		return;
	}
	aref rows;
	makearef(rows, model.(node).rows);
	for (int k = 0; k < GetAttributesNum(rows); k++)
	{
		aref row = GetAttributeN(rows, k);
		if (row.label != "")
		{
			line = TouchLib_Add(out, prefix);
			line.text = row.label;
		}
	}
}

// A node's first picture copied to `out` ({kind, group, image, uv}), false without one.
bool TouchLegLib_NodeImage(aref model, string node, aref out)
{
	if (node == "") return false;
	if (!CheckAttribute(model, node)) return false;
	aref at;
	makearef(at, model.(node));
	return TouchLegLib_Image(at, out);
}

// The text of a row's column; -1 is a column the window does not have.
string TouchLegLib_Cell(aref row, int column)
{
	if (column < 0) return "";
	string cell = "c" + column;
	if (!CheckAttribute(row, "cells." + cell)) UiError("row " + row.id + " has no column " + column);
	return row.cells.(cell).text;
}

int TouchLegLib_Amount(aref row, int column, string what)
{
	return UiAmount(TouchLegLib_Cell(row, column), what);
}

// A row's picture: its column's, else the row's own, else the first a cell of it has.
void TouchLegLib_Picture(aref row, int column, aref out)
{
	aref cells;
	makearef(cells, row.cells);
	string cell = "c" + column;
	aref named;
	makearef(named, cells.(cell));
	if (TouchLegLib_Image(named, out)) return;
	if (TouchLegLib_Image(row, out)) return;
	for (int i = 0; i < GetAttributesNum(cells); i++)
		if (TouchLegLib_Image(GetAttributeN(cells, i), out)) return;
}

// The first image of a node, row or cell whole, the part of a texture it shows included; false without one.
bool TouchLegLib_Image(aref at, aref out)
{
	if (!CheckAttribute(at, "images.i0")) return false;
	aref image;
	makearef(image, at.images.i0);
	CopyAttributes(out, image);
	return true;
}

// The rows of a choice node (ships, characters) as {key, label, selected, can}, "" without the node.
void TouchLegLib_Choices(aref model, string node, aref out)
{
	if (!CheckAttribute(model, node + ".rows")) return;
	aref rows;
	makearef(rows, model.(node).rows);
	for (int k = 0; k < GetAttributesNum(rows); k++)
	{
		aref row = GetAttributeN(rows, k);
		object choice = {key = row.id, label = row.label, selected = row.value == "1",
		                 can = CheckAttribute(row, "offers.select")};
		aref at = TouchLib_Add(out, "s");
		CopyAttributes(at, &choice);
	}
}

void TouchLegLib_Choose(aref model, string node, int k)
{
	TouchLegLib_Act(model, node, k, "select");
}

// `verb` sent to row k of `node`, guarded by the row's id; returns that id.
string TouchLegLib_Act(aref model, string node, int k, string verb)
{
	aref row = TouchLib_Item(model, node + ".rows", k);
	object action = {node = node, row = row.index, verb = verb, value = row.id};
	UiSendTo(&action);
	return row.id;
}

// k of the row of `node` the game marks selected, -1 none: what none means is the caller's.
int TouchLegLib_Selected(aref model, string node)
{
	for (int k = 0; k < TouchLib_Count(model, node + ".rows"); k++)
	{
		aref row = TouchLib_Item(model, node + ".rows", k);
		if (row.value == "1") return k;
	}
	return -1;
}

// k of the row of `node` whose id is `id`, -1 none.
int TouchLegLib_RowOf(aref model, string node, string id)
{
	for (int k = 0; k < TouchLib_Count(model, node + ".rows"); k++)
	{
		aref row = TouchLib_Item(model, node + ".rows", k);
		if (row.id == id) return k;
	}
	return -1;
}

// The game's amount window QTY_*: its field (QTY_EDIT, or QTY_EDITBOX in the windows that name it so), and k of
// the TABLE_LIST row it is open on, -1 none.
string TouchLegLib_Edit(aref model)
{
	if (CheckAttribute(model, "QTY_EDIT")) return "QTY_EDIT";
	return "QTY_EDITBOX";
}

int TouchLegLib_Window(aref model)
{
	if (!CheckAttribute(model, TouchLegLib_Edit(model))) return -1;
	if (!CheckAttribute(model, "QTY_OK_BUTTON") && !CheckAttribute(model, "QTY_CANCEL_BUTTON")) return -1;
	int k = TouchLegLib_Selected(model, "TABLE_LIST");
	// The game opens the window on the table's selected row and keeps it selected while it is open.
	if (k < 0) UiError("the amount window is open on no row of TABLE_LIST");
	return k;
}

// Row k as the player's tap opens it: activated where it offers it, else selected; true when a window comes.
bool TouchLegLib_Open(aref model, int k)
{
	aref row = TouchLib_Item(model, "TABLE_LIST.rows", k);
	string verb = "select";
	if (CheckAttribute(row, "offers.activate")) verb = "activate";
	TouchLegLib_Act(model, "TABLE_LIST", k, verb);
	return verb == "activate";
}

// The open window closed with its cancel, guarded by its row; k of that row, -1 none open.
int TouchLegLib_Cancel(aref model)
{
	int open = TouchLegLib_Window(model);
	if (open < 0) return -1;
	aref row = TouchLib_Item(model, "TABLE_LIST.rows", open);
	object action = {node = "QTY_CANCEL_BUTTON", verb = "press", value = "", guard = row.id};
	UiSendTo(&action);
	return open;
}

// The window takes `amount` (< 0 gives), guarded by the row it was opened on. Its edit box reads every number after a
// negative one as giving until a zero: the zero goes first, in the same flush.
void TouchLegLib_Offer(aref model, int amount, string row)
{
	object value = {node = TouchLegLib_Edit(model), verb = "edit", value = "" + amount, guard = row, lead = "0"};
	UiSendTo(&value);
}

// The window takes `amount` and its OK.
void TouchLegLib_Confirm(aref model, int amount, string row)
{
	TouchLegLib_Offer(model, amount, row);
	object press = {node = "QTY_OK_BUTTON", verb = "press", value = "", guard = row};
	UiSendTo(&press);
}

// The game's category tabs TABBTN_<n> with their captions TABSTR_<n> as {node (pressed: the caption where it takes
// it), id (the tab's button: it stays when other tabs come or go), label (`words` t<n>, else the game's), selected,
// can}; none without tabs.
void TouchLegLib_Tabs(aref model, aref words, aref out)
{
	aref tabs;
	makearef(tabs, out.tabs);
	for (int position = 1; position <= 8; position++)
	{
		string caption = "TABSTR_" + position;
		string button = "TABBTN_" + position;
		string word = "t" + position;
		if (CheckAttribute(model, button) && TouchLegLib_Label(model, caption) != "")
		{
			object tab = {id = button, node = button, label = model.(caption).label, selected = model.(button).value == "1"};
			if (CheckAttribute(model, caption + ".offers.press")) tab.node = caption;
			if (CheckAttribute(words, word)) tab.label = UiText(words.(word));
			tab.can = UiOffers(tab.node, "press");
			aref at = TouchLib_Add(tabs, "t");
			CopyAttributes(at, &tab);
		}
	}
	if (GetAttributesNum(tabs) == 0) DeleteAttribute(out, "tabs");
}

// Our money from the game's "Наши деньги 1 500 песо": out.value (its figure as written), out.currency (the string key
// of what the merchant takes, "" money); false without OUR_GOLD.
bool TouchLegLib_Purse(aref model, aref out)
{
	if (!CheckAttribute(model, "OUR_GOLD")) return false;
	string gold = model.OUR_GOLD.label;
	UiAmount(gold, "OUR_GOLD");
	out.currency = "";
	if (CheckAttribute(model, "OUR_GOLD.context.currency"))
		if (model.OUR_GOLD.context.currency == "doubloons") out.currency = "trade.doubloons";
	int digit = TouchLib_FindDigit(gold, 0, true);
	int end = digit;
	while (end < strlen(gold) && findSubStr("0123456789 ", strcut(gold, end, end), 0) >= 0) end++;
	while (end > digit && strcut(gold, end - 1, end - 1) == " ") end--;
	out.value = TouchLib_Cut(gold, digit, end);
	return true;
}

// The hero sections' windows (character, ship, journal, trade book, nations, items) open one another by their
// buttons I_<SECTION>; a section the window has no button for is the screen's defect.
void TouchLegLib_SectionNodes(aref out)
{
	object nodes = {hero = "I_CHARACTER", ship = "I_SHIP", journal = "I_QUESTBOOK", tradebook = "I_TRADEBOOK",
	                nations = "I_NATIONS", items = "I_ITEMS"};
	CopyAttributes(out, &nodes);
}

// The window has a hero section's button: the sections are drawn.
bool TouchLegLib_Sections(aref model)
{
	object nodes;
	TouchLegLib_SectionNodes(&nodes);
	for (int k = 0; k < GetAttributesNum(&nodes); k++)
	{
		aref node = GetAttributeN(&nodes, k);
		if (CheckAttribute(model, GetAttributeValue(node))) return true;
	}
	return false;
}

void TouchLegLib_Section(aref model, string section)
{
	object nodes;
	TouchLegLib_SectionNodes(&nodes);
	if (!CheckAttribute(&nodes, section)) UiError("no hero section " + section);
	string node = nodes.(section);
	bool can = false;
	if (CheckAttribute(model, node)) can = UiOffers(node, "press");
	if (!can) UiError("the section " + section + " cannot be opened here (" + node + ")");
	UiSend(node, "press", "");
}

// A description of the game's text files as a sheet shows it: a line of only "~" (their paragraph mark) empty, no
// empty lines before or after it.
string TouchLegLib_StockText(string text)
{
	string newline = TouchLib_Newline();
	string out = "";
	string held = "";
	string line;
	int from = 0;
	while (from <= strlen(text))
	{
		int end = findSubStr(text, newline, from);
		if (end < 0) end = strlen(text);
		line = TouchLib_Cut(text, from, end);
		from = end + 1;
		if (UiOneLine(line) == "~" || UiOneLine(line) == "")
		{
			if (out != "") held += newline;
			continue;
		}
		out += held + line;
		held = newline;
	}
	return out;
}

// A crew as the game's windows draw it (SetCrewExpTable): the rows Sailors, Cannoners, Soldiers of `table` with their
// caption and word, the crew's figure of each where the game words it (a crew without men has none: an empty bar),
// the morale with its word after the colon of `moraleText`. out.{groups.g<k>.{label, value, word}, morale,
// moraleWord}.
void TouchLegLib_Crew(aref model, string table, string moraleText, ref crew, aref out)
{
	aref groups;
	makearef(groups, out.groups);
	string group;
	for (int k = 0; k < TouchLib_Count(model, table + ".rows"); k++)
	{
		aref row = TouchLib_Item(model, table + ".rows", k);
		group = row.id;
		object figure = {label = TouchLib_Trim(TouchLegLib_Cell(row, 1)), value = 0,
		                 word = TouchLib_Trim(TouchLegLib_Cell(row, 2))};
		if (figure.word != "") figure.value = sti(crew.Ship.Crew.Exp.(group));
		aref at = TouchLib_Add(groups, "g");
		CopyAttributes(at, &figure);
	}
	out.morale = sti(crew.Ship.Crew.Morale);
	out.moraleWord = TouchLib_AfterColon(UiOneLine(TouchLegLib_Label(model, moraleText)));
}

// The character whose ship the game's ships' scroll `node` shows now (FillScrollImageWithCompanionShips keeps the
// companion of each picture beside it).
ref TouchLegLib_ScrollCaptain(string node)
{
	string pic = "pic" + (sti(GameInterface.(node).current) + 1);
	if (!CheckAttribute(&GameInterface, node + "." + pic + ".companionIndex"))
		UiError("the game's scroll " + node + " has no captain for its " + pic);
	return &Characters[sti(GameInterface.(node).(pic).companionIndex)];
}

// Our flag icon of a nation the game names (its NATIONS picture, GetNationNameByType), "" for a name it has none of.
string TouchLegLib_Flag(string nation)
{
	if (nation == "England") return "nation-england";
	if (nation == "France") return "nation-france";
	if (nation == "Spain") return "nation-spain";
	if (nation == "Holland") return "nation-holland";
	if (nation == "Pirate") return "nation-pirates";
	return "";
}

// The flag icon of a picture of the game's NATIONS group, "" for another picture.
string TouchLegLib_FlagOf(aref image)
{
	if (image.kind != "atlas" || image.group != "NATIONS") return "";
	return TouchLegLib_Flag(image.image);
}

// The character a row of the game's portrait list `node` (CHARACTERS_SCROLL, PASSENGERSLIST) shows: its pic<n>
// .character, the game's identity of the person; a row without one is the game's list out of step with the window.
int TouchLegLib_ListCharacter(string node, aref row)
{
	string pic = "pic" + (sti(row.index) + 1);
	if (!CheckAttribute(&GameInterface, node + "." + pic + ".character"))
		UiError("the list " + node + " has no person of its slot " + row.index);
	return sti(GameInterface.(node).(pic).character);
}

// The people of the hero screens' character list CHARACTERS_SCROLL (FillCharactersScroll): the captain, the officers
// on their posts, companions, passengers and prisoners, each once in the list's order with the captions of the slots
// they fill (an officer may hold several posts), as out.people.p<k>.{key (the character's index), character, label,
// meta, image, row (the index of their first slot)}. Returns k of the person whose slot the list has selected, -1 none.
int TouchLegLib_People(aref model, aref out)
{
	aref people;
	makearef(people, out.people);
	int listed = -1;
	for (int n = 0; n < TouchLib_Count(model, "CHARACTERS_SCROLL.rows"); n++)
	{
		aref row = TouchLib_Item(model, "CHARACTERS_SCROLL.rows", n);
		int slot = sti(row.index);
		string pic = "pic" + (slot + 1);
		int character = TouchLegLib_ListCharacter("CHARACTERS_SCROLL", row);
		// The list writes "0" for a free post; the captain's slot is the first.
		if (slot > 0 && character == 0) continue;
		string caption = "";
		if (CheckAttribute(&GameInterface, "CHARACTERS_SCROLL." + pic + ".str1"))
			caption = GameInterface.CHARACTERS_SCROLL.(pic).str1;
		if (TouchLib_StartsWith(caption, "#")) caption = TouchLib_Cut(caption, 1, strlen(caption));
		int k = 0;
		while (k < GetAttributesNum(people))
		{
			aref known = GetAttributeN(people, k);
			if (sti(known.character) == character) break;
			k++;
		}
		aref person;
		if (k == GetAttributesNum(people))
		{
			ref chr = GetCharacter(character);
			person = TouchLib_Add(people, "p");
			person.key = "" + character;
			person.character = character;
			person.label = GetFullName(chr);
			person.meta = caption;
			person.row = slot;
			person.image = "";
			if (CheckAttribute(chr, "FaceId"))
			{
				person.image.kind = "portrait";
				person.image.image = chr.FaceId;
			}
		}
		else
		{
			person = GetAttributeN(people, k);
			if (caption != "") person.meta = person.meta + " · " + caption;
		}
		if (row.value == "1") listed = k;
	}
	return listed;
}

// Person k of TouchLegLib_People chosen in the character list: their first slot selected.
void TouchLegLib_PickPerson(aref model, aref people, int k)
{
	aref person = TouchLib_Item(people, "people", k);
	string node = "CHARACTERS_SCROLL";
	for (int n = 0; n < TouchLib_Count(model, node + ".rows"); n++)
	{
		aref row = TouchLib_Item(model, node + ".rows", n);
		if (sti(row.index) == sti(person.row))
		{
			if (!CheckAttribute(row, "offers.select")) UiError("the character list does not offer the slot " + row.index);
			TouchLegLib_Act(model, node, n, "select");
			return;
		}
	}
	UiError("the character list has no slot " + person.row);
}

// A captain's ship skills as the game's mini table shows them (interface_utils.c SetSPECIALMiniTable: row tr5 the
// skills' pictures "<Skill> skill icon", row tr6 their values, coloured where a skill is raised or lowered) as
// out.s<k>.{key (the game's skill, "Sailing"), value, tone ("ok", "danger", "")}; none without the table.
void TouchLegLib_ShipSkills(aref model, string table, aref out)
{
	if (!CheckAttribute(model, table + ".rows")) return;
	if (!CheckAttribute(model, table + ".rows.r4") || !CheckAttribute(model, table + ".rows.r5"))
		UiError("the captain's skill table " + table + " has no ship skill rows (tr5 pictures, tr6 values)");
	aref pictures;
	makearef(pictures, model.(table).rows.r4.cells);
	aref values;
	makearef(values, model.(table).rows.r5.cells);
	string suffix = " skill icon";
	for (int k = 0; k < GetAttributesNum(pictures); k++)
	{
		aref cell = GetAttributeN(pictures, k);
		string column = GetAttributeName(cell);
		if (!CheckAttribute(cell, "images.i0.image") || !CheckAttribute(values, column))
			UiError("the captain's skill table " + table + " names no skill in its column " + column);
		string skill = cell.images.i0.image;
		int at = findSubStr(skill, suffix, 0);
		if (at > 0) skill = TouchLib_Cut(skill, 0, at);
		object figure = {key = skill, value = values.(column).text, tone = ""};
		if (CheckAttribute(values, column + ".color"))
		{
			if (values.(column).color == "c4ffc4") figure.tone = "ok";
			if (values.(column).color == "ffc4c4") figure.tone = "danger";
		}
		aref to = TouchLib_Add(out, "s");
		CopyAttributes(to, &figure);
	}
}

// The {label, value} rows f<k> of a two-column table of the game (its first column a picture); a row without a label
// is left out.
void TouchLegLib_Facts(aref model, string table, aref out)
{
	for (int k = 0; k < TouchLib_Count(model, table + ".rows"); k++)
	{
		aref row = TouchLib_Item(model, table + ".rows", k);
		string label = TouchLegLib_Cell(row, 1);
		if (label == "") continue;
		aref at = TouchLib_Add(out, "f");
		at.label = label;
		at.value = TouchLegLib_Cell(row, 2);
	}
}

// The game's captain choice (OFFICERS_WINDOW of the shipyard and the ship exchange) as TouchLib_Captains takes it:
// the passengers of PASSENGERSLIST, the chosen one's name, his facts and ship skills as SetOfficersSkills shows them
// (cleared while nobody is chosen), the game's «Назначить» (absent without ACCEPT_ADD_OFFICER).
void TouchLegLib_Officers(aref model, aref out)
{
	out.title = TouchLib_Trim(UiOneLine(TouchLegLib_Label(model, "OFFICERS_WINDOW_CAPTION")));
	out.name = TouchLib_Trim(UiOneLine(TouchLegLib_Label(model, "OFFICER_NAME")));
	aref candidates;
	makearef(candidates, out.candidates);
	for (int k = 0; k < TouchLib_Count(model, "PASSENGERSLIST.rows"); k++)
	{
		aref row = TouchLib_Item(model, "PASSENGERSLIST.rows", k);
		object person = {key = "" + TouchLegLib_ListCharacter("PASSENGERSLIST", row), label = row.label,
		                 selected = row.value == "1", can = CheckAttribute(row, "offers.select"), reason = row.reason};
		aref at = TouchLib_Add(candidates, "c");
		CopyAttributes(at, &person);
		aref image;
		makearef(image, at.image);
		if (!TouchLegLib_Image(row, image)) DeleteAttribute(at, "image");
	}
	if (out.name != "")
	{
		aref facts;
		makearef(facts, out.facts);
		TouchLegLib_Facts(model, "TABLE_SMALLOTHER", facts);
		aref skills;
		makearef(skills, out.skills);
		TouchLegLib_ShipSkills(model, "TABLE_SMALLSKILL", skills);
	}
	if (!CheckAttribute(model, "ACCEPT_ADD_OFFICER")) return;
	aref appoint;
	makearef(appoint, out.appoint);
	TouchLegLib_Button(model, "ACCEPT_ADD_OFFICER", appoint);
}

// A slider's position as 0..100 of its range.
int TouchLegLib_SliderPercent(aref slider)
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
string TouchLegLib_SliderPosition(aref slider, int percent)
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

// An option of the window as the settings contract has it (contracts/settings.c): the slider or switch `node` into
// `at` {kind (slider | toggle | segment: a switch of several rows), value, on, options, selected, can, reason}; the
// game's caption as at.label when it has none yet.
void TouchLegLib_Option(aref model, string node, aref at)
{
	if (model.(node).type == "slider")
	{
		aref widget;
		makearef(widget, model.(node));
		at.kind = "slider";
		at.value = TouchLegLib_SliderPercent(widget);
		at.can = UiOffers(node, "set");
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
		if (at.label == "") at.label = first.label;
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

// The switch `node` turned on or off (its one row), or its row k chosen; nothing when the game offers no toggle.
void TouchLegLib_OptionToggle(aref ui, string node, int k, bool on)
{
	if (!CheckAttribute(ui, "model." + node + ".rows.r" + k + ".offers.toggle")) return;
	object action = {node = node, verb = "toggle", value = "0", row = k};
	if (on) action.value = "1";
	UiSendTo(&action);
}

// The slider `node` let go at `percent` of its range.
void TouchLegLib_OptionSlide(aref model, string node, int percent)
{
	if (!CheckAttribute(model, node)) UiError("settings: no slider " + node);
	aref slider;
	makearef(slider, model.(node));
	UiSend(node, "set", TouchLegLib_SliderPosition(slider, percent));
}
