// SPDX-License-Identifier: GPL-3.0-only
// The generic layout: a game window no other screen draws, laid out from the engine's model alone (widget types,
// captions, the verbs each offers), and the Other sheet of every other screen (what it does not draw of its window).
// An element's id is its node's name, a row's with its own identity after a colon (the Other sheet's with "extra-"
// before it); a tap sends the node's own verb to the game. Nothing here knows a game: the rules read the model and the
// conventions of the engine's windows (exit and section buttons, captions of pictures).

#event_handler("UiBuild", "TouchGeneric_Build");
#event_handler("UiTap", "TouchGeneric_Tap");

object TouchGeneric;       // the view of the last build of the layout
object TouchGeneric_Sheet; // the view of the last build of the Other sheet
// The widgets shown, in the model's order: w<n>.{node, row (-1: the node's own), type, label, value, exclusive,
// cells (a row's columns), part (TouchGeneric_Parts)}.
object TouchGeneric_Items;
// What each element bound by the last builds does, the layout's and the sheet's apart: a<n>.{id, kind, node, row, …}.
object TouchGeneric_Acts;
object TouchGeneric_SheetActs;

// Words of the buttons' captions in the six languages, compared whole (the language's == ignores the case of ASCII
// letters only: a word of another alphabet is listed as a button writes it too).
string TouchGeneric_ExitWords = "назад отмена выход уйти закрыть Назад Отмена Выход Уйти Закрыть cancel back exit close × zurück abbrechen beenden verlassen schließen atrás cancelar salir cerrar volver retour annuler quitter fermer sortir indietro annulla esci uscire chiudi";
string TouchGeneric_ConfirmWords = "ок ok oк да готово принять применить подтвердить начать взять купить забрать ОК Ок Да Готово Принять Применить Подтвердить Начать Взять Купить Забрать ja fertig annehmen anwenden bestätigen starten nehmen kaufen sí listo aceptar aplicar confirmar empezar comenzar tomar comprar recoger oui terminé accepter appliquer confirmer commencer prendre acheter sì fatto pronto accetta applica conferma inizia prendi compra";
// A word of a caption or of a node's name that marks a destructive button.
string TouchGeneric_DangerWords = "delete удалить сжечь казнить Удалить Сжечь Казнить kill löschen verbrennen töten hinrichten eliminar borrar quemar matar supprimer effacer brûler tuer elimina cancella brucia uccidi";

// A caption is short: one line of at most `letters` letters, counted as two bytes each (the languages with an
// alphabet of their own write a letter in two).
bool TouchGeneric_Short(string text, int letters)
{
	if (text == "") return false;
	if (findSubStr(text, TouchLib_Newline(), 0) >= 0) return false;
	return strlen(text) <= letters * 2;
}

bool TouchGeneric_Word(string words, string text)
{
	object list;
	TouchLib_Words(words, &list);
	for (int k = 0; k < GetAttributesNum(&list); k++)
	{
		aref word = GetAttributeN(&list, k);
		if (word.text == text) return true;
	}
	return false;
}

// A word of `text`, split also at '_' (a node's name), is one of `words`.
bool TouchGeneric_AnyWord(string words, string text)
{
	object list;
	int at = findSubStr(text, "_", 0);
	while (at >= 0)
	{
		text = TouchLib_Cut(text, 0, at) + " " + TouchLib_Cut(text, at + 1, strlen(text));
		at = findSubStr(text, "_", 0);
	}
	TouchLib_Words(text, &list);
	for (int k = 0; k < GetAttributesNum(&list); k++)
	{
		aref word = GetAttributeN(&list, k);
		if (TouchGeneric_Word(words, word.text)) return true;
	}
	return false;
}

// The node without the "_2" of the engine's copies of a navigation button.
string TouchGeneric_Base(string node)
{
	if (TouchLib_EndsWith(node, "_2")) return TouchLib_Cut(node, 0, strlen(node) - 2);
	return node;
}

// A navigation button of the hero's sections: every window the header shows them for has a screen of its own, so here
// they are rows of the Other sheet.
bool TouchGeneric_Section(string node)
{
	string base = TouchGeneric_Base(node);
	return base == "I_CHARACTER" || base == "I_SHIP" || base == "I_QUESTBOOK" || base == "I_TRADEBOOK" ||
	       base == "I_NATIONS" || base == "I_ITEMS";
}

// What a button is to the window: exit, confirm, danger, section or other.
string TouchGeneric_Role(string node, string label)
{
	if (TouchGeneric_Section(node)) return "section";
	if (TouchLib_StartsWith(node, "EXIT") || TouchLib_StartsWith(node, "CANCEL") || TouchLib_StartsWith(node, "CLOSE") ||
	    TouchLib_StartsWith(node, "BACK") || TouchLib_EndsWith(node, "_EXIT") || TouchLib_EndsWith(node, "_CANCEL") ||
	    TouchLib_EndsWith(node, "_CLOSE") || TouchGeneric_Word(TouchGeneric_ExitWords, label)) return "exit";
	if (TouchGeneric_AnyWord(TouchGeneric_DangerWords, label) || TouchGeneric_AnyWord(TouchGeneric_DangerWords, node))
		return "danger";
	if (TouchLib_StartsWith(node, "OK") || TouchLib_StartsWith(node, "ACCEPT") || TouchLib_StartsWith(node, "YES") ||
	    TouchLib_StartsWith(node, "CONFIRM") || TouchLib_StartsWith(node, "DONE") || TouchLib_StartsWith(node, "APPLY") ||
	    TouchLib_StartsWith(node, "START") || TouchLib_EndsWith(node, "_OK") || TouchLib_EndsWith(node, "_ACCEPT") ||
	    TouchLib_EndsWith(node, "_YES") || TouchGeneric_Word(TouchGeneric_ConfirmWords, label)) return "confirm";
	return "other";
}

// The model's entry of item k: its node, or its row.
aref TouchGeneric_At(aref model, aref item)
{
	aref at;
	string node = item.node;
	if (sti(item.row) < 0) { makearef(at, model.(node)); return at; }
	string slot = "r" + item.row;
	makearef(at, model.(node).rows.(slot));
	return at;
}

aref TouchGeneric_Item(int k)
{
	return GetAttributeN(&TouchGeneric_Items, k);
}

// The element id of an item: its node, a row's with its identity (its index when the game gives none).
string TouchGeneric_Id(aref model, aref item, string prefix)
{
	if (sti(item.row) < 0) return prefix + item.node;
	aref at = TouchGeneric_At(model, item);
	if (at.id != "") return prefix + item.node + ":" + at.id;
	return prefix + item.node + ":" + item.row;
}

// The widgets of the model in its order, one per node or per row.
void TouchGeneric_Flatten(aref model)
{
	DeleteAttribute(&TouchGeneric_Items, "");
	for (int k = 0; k < GetAttributesNum(model); k++)
	{
		aref node = GetAttributeN(model, k);
		string name = node.node; // its name as the game writes it (an attribute's name keeps the case first seen)
		if (!CheckAttribute(node, "rows"))
		{
			object single = {node = name, row = -1, type = node.type, label = node.label, value = node.value,
			                 exclusive = false, cells = 0, part = ""};
			CopyAttributes(TouchLib_Add(&TouchGeneric_Items, "w"), &single);
			continue;
		}
		aref rows;
		makearef(rows, node.rows);
		for (int j = 0; j < GetAttributesNum(rows); j++)
		{
			aref row = GetAttributeN(rows, j);
			object entry = {node = name, row = row.index, type = node.type, label = row.label, value = row.value,
			                exclusive = CheckAttribute(row, "exclusive"), cells = TouchLib_Count(row, "cells"), part = ""};
			CopyAttributes(TouchLib_Add(&TouchGeneric_Items, "w"), &entry);
		}
	}
}

bool TouchGeneric_TitleNode(aref item)
{
	if (item.type == "heading") return true;
	if (item.type != "text") return false;
	string node = item.node;
	return node == "TITLE" || findSubStr(node, "CAPTION", 0) >= 0 || TouchLib_EndsWith(node, "_TITLE");
}

// A picture the player presses without a caption of its own (a tab of the window) is named by the text node of its
// name with _STR after it, or with STR for its BTN; "#<n>" when it has none and its name ends with "_<n>"; "".
string TouchGeneric_PictureLabel(aref model, string node)
{
	string named = node + "_STR";
	int at = findSubStr(node, "BTN", 0);
	if (!CheckAttribute(model, named) && at >= 0)
		named = TouchLib_Cut(node, 0, at) + "STR" + TouchLib_Cut(node, at + 3, strlen(node));
	if (CheckAttribute(model, named))
	{
		if (model.(named).type == "text" && model.(named).label != "") return named;
	}
	int digit = TouchLib_FindDigit(node, 0, true);
	if (digit < 1 || TouchLib_FindDigit(node, digit, false) >= 0) return "";
	if (TouchLib_Cut(node, digit - 1, digit) != "_") return "";
	return "#" + TouchLib_Cut(node, digit, strlen(node));
}

// The part each item plays: "title", "exit" (the header's ←), "section", "confirm", "danger", "other" (buttons by
// role), "arrow" (‹ ›: a switch of what the window shows), "tab" (a picture tab), "caption" (a picture tab's words),
// "flow" (texts, lists, controls); "" for what is not shown.
void TouchGeneric_Parts(aref model)
{
	aref item;
	int k;
	int count = GetAttributesNum(&TouchGeneric_Items);
	string title = "";
	bool navigation = false;
	for (k = 0; k < count; k++)
	{
		item = TouchGeneric_Item(k);
		if (title == "" && TouchGeneric_TitleNode(item)) { item.part = "title"; title = item.label; }
		if (item.type == "button" && TouchGeneric_Section(item.node)) navigation = true;
	}
	for (k = 0; k < count; k++)
	{
		item = TouchGeneric_Item(k);
		if (item.type != "image" || item.label != "") continue;
		string caption = TouchGeneric_PictureLabel(model, item.node);
		if (caption == "") continue;
		item.part = "tab";
		if (TouchLib_StartsWith(caption, "#")) { item.label = TouchLib_Cut(caption, 1, strlen(caption)); continue; }
		item.label = model.(caption).label;
		for (int j = 0; j < count; j++)
		{
			aref text = TouchGeneric_Item(j);
			if (text.node == caption) text.part = "caption";
		}
	}
	bool leave = false;
	for (k = 0; k < count; k++)
	{
		item = TouchGeneric_Item(k);
		if (item.part != "") continue;
		string type = item.type;
		string node = item.node;
		if (navigation && TouchGeneric_Base(node) == "STR_BUTTONS") continue;
		if (title != "" && (type == "text" || type == "heading") && item.label == title) continue;
		if (TouchLib_EndsWith(node, "_2") && sti(item.row) < 0)
		{
			string original = TouchGeneric_Base(node);
			if (CheckAttribute(model, original))
			{
				if (model.(original).label == item.label && model.(original).value == item.value &&
				    CheckAttribute(model, original + ".offers.press") == CheckAttribute(model, node + ".offers.press"))
					continue;
			}
		}
		if (type == "image" && item.label == "") continue;
		item.part = "flow";
		if (type != "button" && type != "image" && type != "save" && !TouchLib_StartsWith(type, "nation_help")) continue;
		if (item.label == "‹" || item.label == "›") { item.part = "arrow"; continue; }
		item.part = TouchGeneric_Role(node, item.label);
		if (item.part == "exit" && leave) item.part = "other";
		if (item.part == "exit") leave = true;
	}
}

bool TouchGeneric_Control(aref item)
{
	if (item.part == "arrow") return true;
	string type = item.type;
	return item.part == "flow" && (type == "check" || type == "edit" || type == "slider" || type == "stepper");
}

int TouchGeneric_Serial = 0; // names the blocks: one deleted leaves a gap TouchLib_Add's count would fill twice

aref TouchGeneric_NewBlock(aref blocks, string kind)
{
	string slot = "b" + TouchGeneric_Serial;
	TouchGeneric_Serial++;
	aref block;
	makearef(block, blocks.(slot));
	block.kind = kind;
	block.length = 0;
	block.items = "";
	block.labels = "";
	return block;
}

void TouchGeneric_Take(aref block, int k)
{
	aref item = TouchGeneric_Item(k);
	string slot = "i" + TouchLib_Count(block, "items");
	block.items.(slot) = k;
	block.length = sti(block.length) + strlen(item.label);
}

int TouchGeneric_ItemOf(aref block, int k)
{
	return sti(GetAttributeValue(TouchLib_Item(block, "items", k)));
}

// A run of rows of one choice node: a table (cells in columns), tabs (two to four short options, one selected) or a
// list.
string TouchGeneric_ChoiceKind(aref block)
{
	bool selection = false;
	bool brief = true;
	int count = TouchLib_Count(block, "items");
	for (int k = 0; k < count; k++)
	{
		aref item = TouchGeneric_Item(TouchGeneric_ItemOf(block, k));
		if (sti(item.cells) >= 2) return "table";
		if (item.value == "1") selection = true;
		if (!TouchGeneric_Short(item.label, 12)) brief = false;
	}
	if (count >= 2 && count <= 4 && selection && brief) return "tabs";
	return "list";
}

// The flow's items in blocks of a kind: b<n>.{kind (table, list, tabs, controls, text, label), items.i<k> (indices of
// TouchGeneric_Items), labels.l<k> (the short text that names control k), length (the text's bytes)}.
void TouchGeneric_Blocks(aref blocks)
{
	object flow;
	int k;
	aref item, block;
	for (k = 0; k < GetAttributesNum(&TouchGeneric_Items); k++)
	{
		item = TouchGeneric_Item(k);
		if (item.part == "flow" || item.part == "arrow" || item.part == "tab") { string slot = "f" + GetAttributesNum(&flow); flow.(slot) = k; }
	}
	int count = GetAttributesNum(&flow);
	k = 0;
	while (k < count)
	{
		int at = sti(GetAttributeValue(GetAttributeN(&flow, k)));
		aref first = TouchGeneric_Item(at);
		int end = k + 1;
		aref next;
		if (first.part == "tab")
		{
			block = TouchGeneric_NewBlock(blocks, "tabs");
			while (end < count) { next = TouchGeneric_Item(sti(GetAttributeValue(GetAttributeN(&flow, end)))); if (next.part != "tab") break; end++; }
		}
		else if (first.type == "choice")
		{
			block = TouchGeneric_NewBlock(blocks, "list");
			while (end < count) { next = TouchGeneric_Item(sti(GetAttributeValue(GetAttributeN(&flow, end)))); if (next.node != first.node) break; end++; }
		}
		else if (TouchGeneric_Control(first))
		{
			block = TouchGeneric_NewBlock(blocks, "controls");
			while (end < count) { next = TouchGeneric_Item(sti(GetAttributeValue(GetAttributeN(&flow, end)))); if (!TouchGeneric_Control(next)) break; end++; }
		}
		else if (first.type == "heading") block = TouchGeneric_NewBlock(blocks, "label");
		else
		{
			block = TouchGeneric_NewBlock(blocks, "text");
			while (end < count)
			{
				next = TouchGeneric_Item(sti(GetAttributeValue(GetAttributeN(&flow, end))));
				if (next.part != "flow" || next.type != first.type || next.type == "choice" || TouchGeneric_Control(next)) break;
				end++;
			}
		}
		for (int j = k; j < end; j++) TouchGeneric_Take(block, sti(GetAttributeValue(GetAttributeN(&flow, j))));
		k = end;
		if (block.kind == "list") block.kind = TouchGeneric_ChoiceKind(block);
		if (block.kind != "controls") continue;
		// A short text right before controls names the first of them (not a plain switch: it has its own words).
		int n = GetAttributesNum(blocks);
		aref before;
		if (n >= 2)
		{
			before = GetAttributeN(blocks, n - 2);
			if (before.kind == "text" && TouchLib_Count(before, "items") == 1)
			{
				aref only = TouchGeneric_Item(TouchGeneric_ItemOf(before, 0));
				if (TouchGeneric_Short(only.label, 30) && !(first.type == "check" && !sti(first.exclusive)))
				{
					string named = only.label;
					DeleteAttribute(blocks, GetAttributeName(before));
					n--;
					block = GetAttributeN(blocks, n - 1); // the list moved: an aref into it is stale
					block.labels.l0 = named;
				}
			}
		}
		// Controls parted only by their captions stay one block.
		if (n < 2) continue;
		before = GetAttributeN(blocks, n - 2);
		if (before.kind != "controls") continue;
		int offset = TouchLib_Count(before, "items");
		for (int c = 0; c < TouchLib_Count(block, "items"); c++) TouchGeneric_Take(before, TouchGeneric_ItemOf(block, c));
		if (CheckAttribute(block, "labels.l0")) { string label = "l" + offset; before.labels.(label) = block.labels.l0; }
		DeleteAttribute(blocks, GetAttributeName(block));
	}
	// A heading names a group only when there are two groups.
	int groups = 0;
	for (k = 0; k < GetAttributesNum(blocks); k++) { block = GetAttributeN(blocks, k); if (block.kind != "label") groups++; }
	if (groups >= 2) return;
	for (k = GetAttributesNum(blocks) - 1; k >= 0; k--)
	{
		block = GetAttributeN(blocks, k);
		if (block.kind == "label") DeleteAttribute(blocks, GetAttributeName(block));
	}
}

aref TouchGeneric_Act(aref acts, string id, string kind, aref item)
{
	aref act = TouchLib_Add(acts, "a");
	act.id = id;
	act.kind = kind;
	act.node = item.node;
	act.row = item.row;
	return act;
}

// A button of the bar: {id, label, variant, disabled, reason, busy, surface}.
aref TouchGeneric_Button(aref list, aref acts, aref model, aref item, string variant, string surface)
{
	aref at = TouchGeneric_At(model, item);
	string id = item.node;
	object button = {id = id, label = item.label, variant = variant, disabled = !CheckAttribute(at, "offers.press"),
	                 reason = at.reason, busy = UiPending(id), surface = surface};
	aref view = TouchLib_Add(list, "a");
	CopyAttributes(view, &button);
	TouchGeneric_Act(acts, id, "press", item);
	UiOn(id, "");
	return view;
}

// The verb a tap on a row of a choice means: activate on the selected row when offered, else select, else open, else
// activate; "" for none.
string TouchGeneric_ChoiceVerb(aref row)
{
	if (row.value == "1" && CheckAttribute(row, "offers.activate")) return "activate";
	if (CheckAttribute(row, "offers.select")) return "select";
	if (CheckAttribute(row, "offers.open")) return "open";
	if (CheckAttribute(row, "offers.activate")) return "activate";
	return "";
}

// A button or a choice's row as a row of a list or of the Other sheet: {kind "action", id, title, disabled, reason,
// selected, muted, busy}.
void TouchGeneric_ActionRow(aref rows, aref acts, aref model, aref item, string prefix)
{
	aref at = TouchGeneric_At(model, item);
	string id = TouchGeneric_Id(model, item, prefix);
	bool choice = item.type == "choice";
	string title = item.label;
	int tab = findSubStr(title, "	", 0);
	while (tab >= 0)
	{
		title = TouchLib_Cut(title, 0, tab) + " " + TouchLib_Cut(title, tab + 1, strlen(title));
		tab = findSubStr(title, "	", 0);
	}
	object row = {kind = "action", id = id, title = title, reason = at.reason, busy = UiPending(id)};
	if (choice)
	{
		row.disabled = TouchGeneric_ChoiceVerb(at) == "";
		row.selected = item.value == "1";
		// A quest's title: value 1 is a completed one.
		if (CheckAttribute(at, "offers.open")) { row.muted = item.value == "1"; row.selected = false; }
		TouchGeneric_Act(acts, id, "choice", item);
	}
	else
	{
		row.disabled = !CheckAttribute(at, "offers.press");
		row.selected = false;
		TouchGeneric_Act(acts, id, "press", item);
	}
	CopyAttributes(TouchLib_Add(rows, "r"), &row);
	UiOn(id, "");
}

// The rows of a run of controls (check, edit, slider, stepper, the ‹ › arrows) of `block` into `rows`; `place` is
// where the view keeps them (for the prediction of a switch: "<side>.<block>"), "" for none.
void TouchGeneric_ControlRows(aref rows, aref acts, aref model, aref block, string prefix, string place)
{
	int count = TouchLib_Count(block, "items");
	for (int k = 0; k < count; k++)
	{
		aref item = TouchGeneric_Item(TouchGeneric_ItemOf(block, k));
		aref at = TouchGeneric_At(model, item);
		string slot = "l" + k;
		string label = "";
		if (CheckAttribute(block, "labels." + slot)) label = block.labels.(slot);
		string type = item.type;
		string id = TouchGeneric_Id(model, item, prefix);
		aref act;
		aref row = TouchLib_Add(rows, "r");
		string rowSlot = GetAttributeName(row);
		if (item.part == "arrow")
		{
			// ‹ and the › that follows it: one switch.
			row.kind = "stepper";
			row.id = prefix + item.node;
			row.label = label;
			row.text = "";
			row.left = false;
			row.right = false;
			for (int side = 0; side < 2 && k < count; side++)
			{
				item = TouchGeneric_Item(TouchGeneric_ItemOf(block, k));
				if (item.part != "arrow") { k--; break; }
				string verb = "left";
				if (item.label == "›") verb = "right";
				if (side == 1 && verb == "left") { k--; break; }
				at = TouchGeneric_At(model, item);
				row.(verb) = CheckAttribute(at, "offers.press");
				act = TouchGeneric_Act(acts, row.id + "-" + verb, "press", item);
				UiOn(row.id + "-" + verb, "");
				if (verb == "right") break;
				k++;
			}
			continue;
		}
		if (type == "check" && sti(item.exclusive))
		{
			// One radio group: the following options of the node; a segment when they are few and short.
			int end = k;
			bool segment = true;
			bool usable = false;
			int selected = -1;
			aref option;
			while (end < count)
			{
				option = TouchGeneric_Item(TouchGeneric_ItemOf(block, end));
				if (option.type != "check" || !sti(option.exclusive) || option.node != item.node) break;
				if (!TouchGeneric_Short(option.label, 12)) segment = false;
				end++;
			}
			if (end - k > 4) segment = false;
			if (!segment)
			{
				for (int r = k; r < end; r++)
				{
					if (r > k) row = TouchLib_Add(rows, "r");
					option = TouchGeneric_Item(TouchGeneric_ItemOf(block, r));
					at = TouchGeneric_At(model, option);
					row.kind = "radio";
					row.id = TouchGeneric_Id(model, option, prefix);
					row.label = option.label;
					row.on = option.value == "1";
					row.disabled = !CheckAttribute(at, "offers.toggle");
					row.reason = at.reason;
					act = TouchGeneric_Act(acts, row.id, "radio", option);
					UiOn(row.id, "change");
				}
				k = end - 1;
				continue;
			}
			row.kind = "segment";
			row.id = prefix + item.node;
			row.label = label;
			row.reason = at.reason;
			act = TouchGeneric_Act(acts, row.id, "segment", item);
			act.place = place;
			act.slot = rowSlot;
			for (int o = k; o < end; o++)
			{
				option = TouchGeneric_Item(TouchGeneric_ItemOf(block, o));
				string to = "o" + (o - k);
				row.options.(to) = option.label;
				act.options.(to) = option.row;
				if (option.value == "1") selected = o - k;
				if (CheckAttribute(TouchGeneric_At(model, option), "offers.toggle")) usable = true;
			}
			row.selected = selected;
			row.disabled = !usable;
			UiOn(row.id, "change");
			k = end - 1;
			continue;
		}
		row.id = id;
		if (type == "check")
		{
			row.kind = "toggle";
			row.label = item.label;
			row.on = item.value == "1";
			row.disabled = !CheckAttribute(at, "offers.toggle");
			row.reason = at.reason;
			act = TouchGeneric_Act(acts, id, "toggle", item);
			act.place = place;
			act.slot = rowSlot;
			UiOn(id, "change");
			continue;
		}
		if (type == "slider")
		{
			row.kind = "slider";
			row.label = label;
			if (label == "") row.label = item.label;
			int low = sti(at.min);
			int span = sti(at.max) - low;
			if (span < 1) span = 1;
			int percent = makeint((100.0 * (UiAmount(item.value, item.node) - low)) / span + 0.5);
			if (percent < 0) percent = 0;
			if (percent > 100) percent = 100;
			row.value = percent;
			row.disabled = !CheckAttribute(at, "offers.set");
			row.reason = at.reason;
			TouchGeneric_Act(acts, id, "slider", item);
			// The drag's progress moves nothing: the position goes to the game when it is let go.
			UiOn(id, "change");
			UiOn(id, "commit");
			continue;
		}
		if (type == "stepper")
		{
			row.kind = "stepper";
			row.label = label;
			row.text = item.value;
			if (row.text == "") row.text = item.label;
			row.left = CheckAttribute(at, "offers.left");
			row.right = CheckAttribute(at, "offers.right");
			TouchGeneric_Act(acts, id + "-left", "left", item);
			TouchGeneric_Act(acts, id + "-right", "right", item);
			UiOn(id + "-left", "");
			UiOn(id + "-right", "");
			continue;
		}
		row.kind = "field";
		row.label = label;
		row.value = item.value;
		act = TouchGeneric_Act(acts, id, "edit", item);
		act.title = label;
		UiOn(id, "focus");
	}
}

bool TouchGeneric_Numeric(string text)
{
	bool digit = false;
	for (int k = 0; k < strlen(text); k++)
	{
		string c = strcut(text, k, k);
		if (findSubStr("0123456789", c, 0) >= 0) digit = true;
		else if (findSubStr(" -+.,%", c, 0) < 0) return false;
	}
	return digit;
}

// "4000" -> "4 000": a whole number of four digits or more, grouped; anything else as the game wrote it.
string TouchGeneric_Grouped(string text)
{
	if (strlen(text) < 4 || TouchLib_FindDigit(text, 0, false) >= 0) return text;
	return TouchLib_Thousands(sti(text));
}

// One block as the pack's s-block draws it.
void TouchGeneric_BlockView(aref view, aref acts, aref model, aref block, string surface, string place)
{
	string kind = block.kind;
	aref first = TouchGeneric_Item(TouchGeneric_ItemOf(block, 0));
	int count = TouchLib_Count(block, "items");
	int k;
	aref item, at;
	view.id = first.node;
	view.kind = kind;
	view.surface = surface;
	view.scroll = false;
	if (kind == "table")
	{
		aref node;
		string name = first.node;
		makearef(node, model.(name));
		int columns = 0;
		for (k = 0; k < count; k++) { item = TouchGeneric_Item(TouchGeneric_ItemOf(block, k)); if (sti(item.cells) > columns) columns = sti(item.cells); }
		object used, numeric;
		int c;
		for (c = 0; c < columns; c++)
		{
			string cell = "c" + c;
			used.(cell) = false;
			numeric.(cell) = true;
			for (k = 0; k < count; k++)
			{
				at = TouchGeneric_At(model, TouchGeneric_Item(TouchGeneric_ItemOf(block, k)));
				if (!CheckAttribute(at, "cells." + cell)) continue;
				string text = at.cells.(cell).text;
				if (text == "") continue;
				used.(cell) = true;
				if (!TouchGeneric_Numeric(text)) numeric.(cell) = false;
			}
		}
		bool head = false;
		bool grown = false;
		view.columns = "";
		aref columnList;
		makearef(columnList, view.columns);
		for (c = 0; c < columns; c++)
		{
			cell = "c" + c;
			if (!sti(used.(cell))) continue;
			string label = "";
			if (CheckAttribute(node, "headers." + cell)) label = UiOneLine(node.headers.(cell));
			if (label != "") head = true;
			aref column = TouchLib_Add(columnList, "c");
			column.label = label;
			column.align = "left";
			column.w = "2";
			if (sti(numeric.(cell))) { column.align = "right"; column.w = "88px"; }
			else if (!grown) { column.w = "3"; grown = true; }
		}
		view.head = head;
		view.interactive = false;
		view.rows = "";
		aref tableRows;
		makearef(tableRows, view.rows);
		for (k = 0; k < count; k++)
		{
			item = TouchGeneric_Item(TouchGeneric_ItemOf(block, k));
			at = TouchGeneric_At(model, item);
			aref row = TouchLib_Add(tableRows, "r");
			row.id = at.id;
			if (at.id == "") row.id = item.row;
			row.selected = item.value == "1";
			row.disabled = !sti(at.enabled) || (TouchGeneric_ChoiceVerb(at) == "" && sti(at.listed));
			row.reason = at.reason;
			row.cells = "";
			aref cells;
			makearef(cells, row.cells);
			if (sti(at.enabled)) view.interactive = true;
			for (c = 0; c < columns; c++)
			{
				cell = "c" + c;
				if (!sti(used.(cell))) continue;
				aref shown = TouchLib_Add(cells, "c");
				shown.text = "";
				if (CheckAttribute(at, "cells." + cell)) shown.text = at.cells.(cell).text;
				if (sti(numeric.(cell))) { shown.text = TouchGeneric_Grouped(shown.text); shown.bold = true; }
			}
		}
		aref act = TouchGeneric_Act(acts, first.node, "table", first);
		UiOn(first.node, "row");
		return;
	}
	if (kind == "list")
	{
		view.rows = "";
		aref listRows;
		makearef(listRows, view.rows);
		for (k = 0; k < count; k++) TouchGeneric_ActionRow(listRows, acts, model, TouchGeneric_Item(TouchGeneric_ItemOf(block, k)), "");
		return;
	}
	if (kind == "tabs")
	{
		view.items = "";
		aref items;
		makearef(items, view.items);
		aref tabs = TouchGeneric_Act(acts, first.node, "tabs", first);
		for (k = 0; k < count; k++)
		{
			item = TouchGeneric_Item(TouchGeneric_ItemOf(block, k));
			at = TouchGeneric_At(model, item);
			aref tab = TouchLib_Add(items, "t");
			tab.label = item.label;
			tab.selected = item.value == "1";
			string verb = "press";
			if (item.type == "choice") verb = TouchGeneric_ChoiceVerb(at);
			tab.disabled = verb == "" || !CheckAttribute(at, "offers." + verb);
			string t = "t" + k;
			tabs.tabs.(t).node = item.node;
			tabs.tabs.(t).row = item.row;
		}
		UiOn(first.node, "select");
		return;
	}
	if (kind == "controls")
	{
		view.rows = "";
		aref controlRows;
		makearef(controlRows, view.rows);
		TouchGeneric_ControlRows(controlRows, acts, model, block, "", place);
		return;
	}
	if (kind == "label") { view.text = first.label; return; }
	view.paragraphs = "";
	for (k = 0; k < count; k++)
	{
		item = TouchGeneric_Item(TouchGeneric_ItemOf(block, k));
		string rest = item.label;
		while (rest != "")
		{
			int end = findSubStr(rest, TouchLib_Newline(), 0);
			if (end < 0) end = strlen(rest);
			string line = TouchLib_Cut(rest, 0, end);
			if (line != "") { string p = "p" + TouchLib_Count(view, "paragraphs"); view.paragraphs.(p) = line; }
			rest = TouchLib_Cut(rest, end + 1, strlen(rest));
		}
	}
}

string TouchGeneric_Exit = ""; // the node the header's ← presses, "" for the engine's back

// The layout of a window no other screen draws: blocks in the template they fit, buttons placed by role.
void TouchGeneric_Layout(aref model)
{
	DeleteAttribute(&TouchGeneric, "");
	DeleteAttribute(&TouchGeneric_Acts, "");
	TouchGeneric_Flatten(model);
	TouchGeneric_Parts(model);
	object blocks;
	TouchGeneric_Blocks(&blocks);
	aref block, item;
	int k;
	int count = GetAttributesNum(&blocks);
	// The main block (the longest table or list) and the detail (the longest text).
	int longest = -1;
	int detail = -1;
	int size = 0;
	int length = 0;
	int others = 0;
	int controls = 0;
	bool reading = count > 0;
	bool settings = true;
	bool texts = false;
	for (k = 0; k < count; k++)
	{
		block = GetAttributeN(&blocks, k);
		string kind = block.kind;
		if ((kind == "table" || kind == "list") && TouchLib_Count(block, "items") > size) { longest = k; size = TouchLib_Count(block, "items"); }
		if (kind == "text" && sti(block.length) > length) { detail = k; length = sti(block.length); }
		if (kind != "text" && kind != "label") reading = false;
		if (kind == "controls") controls += TouchLib_Count(block, "items");
		if (kind == "text") texts = true;
		if (kind != "controls" && kind != "label" && !(kind == "text" && sti(block.length) <= 240)) settings = false;
	}
	for (k = 0; k < count; k++) { block = GetAttributeN(&blocks, k); if (k != longest && block.kind != "tabs") others++; }
	// A list beside anything else keeps its own column, where it scrolls: below other blocks it has no height left.
	string layout = "fallback";
	if (reading || count == 0) layout = "reading";
	else if (longest >= 0 && others == 0) layout = "list";
	else if (longest >= 0) layout = "split";
	else if (controls > 0 && settings)
	{
		layout = "settings";
		if (controls >= 6 && texts) layout = "settings-wide";
	}
	bool two = layout == "split" || layout == "settings-wide";
	TouchGeneric.layout = layout;
	TouchGeneric.title = "";
	TouchGeneric.left = "";
	TouchGeneric.right = "";
	for (k = 0; k < count; k++)
	{
		block = GetAttributeN(&blocks, k);
		bool right = false;
		if (layout == "split") right = k != longest && block.kind != "tabs";
		if (layout == "settings-wide") right = block.kind == "text";
		string side = "left";
		if (right) side = "right";
		string surface = "wood";
		if (right || layout == "reading") surface = "parchment";
		aref list;
		makearef(list, TouchGeneric.(side));
		aref view = TouchLib_Add(list, "b");
		TouchGeneric_BlockView(view, &TouchGeneric_Acts, model, block, surface, side + " " + GetAttributeName(view));
		view.scroll = k == longest || (layout == "settings-wide" && k == detail);
	}
	// A window with nothing a finger can reach says so: the header's ← leaves it.
	bool empty = count == 0;
	TouchGeneric_Exit = "";
	object buttons;
	for (k = 0; k < GetAttributesNum(&TouchGeneric_Items); k++)
	{
		item = TouchGeneric_Item(k);
		if (item.part == "title") TouchGeneric.title = item.label;
		if (item.part == "exit") TouchGeneric_Exit = item.node;
		if (item.part == "confirm" || item.part == "danger" || item.part == "other") { string slot = "b" + GetAttributesNum(&buttons); buttons.(slot) = k; empty = false; }
		if (item.part == "section") empty = false;
	}
	if (empty)
	{
		aref leftList;
		makearef(leftList, TouchGeneric.left);
		aref note = TouchLib_Add(leftList, "b");
		note.id = "empty";
		note.kind = "text";
		note.surface = "parchment";
		note.scroll = false;
		note.paragraphs.p0 = UiText("generic.empty");
	}
	// The last confirm is the primary, the first danger stands apart; more than three leave two in the bar.
	int primary = -1;
	int danger = -1;
	object bar;
	for (k = 0; k < GetAttributesNum(&buttons); k++)
	{
		int at = sti(GetAttributeValue(GetAttributeN(&buttons, k)));
		item = TouchGeneric_Item(at);
		string put = "";
		if (item.part == "confirm") { if (primary >= 0) put = "" + primary; primary = at; }
		else if (item.part == "danger" && danger < 0) danger = at;
		else put = "" + at;
		if (put != "") { string to = "b" + GetAttributesNum(&bar); bar.(to) = put; }
	}
	int shown = GetAttributesNum(&bar);
	if (primary < 0 && shown == 1 && danger < 0)
	{
		item = TouchGeneric_Item(sti(bar.b0));
		if (CheckAttribute(TouchGeneric_At(model, item), "offers.press")) { primary = sti(bar.b0); shown = 0; }
	}
	int keep = shown;
	int placed = 0;
	if (primary >= 0) placed++;
	if (danger >= 0) placed++;
	if (shown + placed > 3) { keep = 2 - placed; if (keep < 0) keep = 0; }
	string surface = "wood";
	if (two || layout == "reading") surface = "parchment";
	TouchGeneric.actions = "";
	TouchGeneric.extras.rows = "";
	aref actions, more;
	makearef(actions, TouchGeneric.actions);
	makearef(more, TouchGeneric.extras.rows);
	for (k = 0; k < shown; k++)
	{
		string b = "b" + k;
		item = TouchGeneric_Item(sti(bar.(b)));
		if (k < keep) TouchGeneric_Button(actions, &TouchGeneric_Acts, model, item, "secondary", surface);
		else TouchGeneric_ActionRow(more, &TouchGeneric_Acts, model, item, "extra-");
	}
	if (primary >= 0) TouchGeneric_Button(actions, &TouchGeneric_Acts, model, TouchGeneric_Item(primary), "primary", surface);
	if (danger >= 0)
	{
		object dangers;
		aref button = TouchGeneric_Button(&dangers, &TouchGeneric_Acts, model, TouchGeneric_Item(danger), "danger", surface);
		TouchGeneric.danger = "";
		aref dangerView;
		makearef(dangerView, TouchGeneric.danger);
		CopyAttributes(dangerView, button);
	}
	for (k = 0; k < GetAttributesNum(&TouchGeneric_Items); k++)
	{
		item = TouchGeneric_Item(k);
		if (item.part == "section") TouchGeneric_ActionRow(more, &TouchGeneric_Acts, model, item, "extra-");
	}
	UiOn("header-back", "");
	UiShow(&TouchGeneric);
}

// The Other sheet of another screen: its texts as one note, its controls and buttons as rows.
void TouchGeneric_Other(aref model)
{
	DeleteAttribute(&TouchGeneric_Sheet, "");
	DeleteAttribute(&TouchGeneric_SheetActs, "");
	TouchGeneric_Flatten(model);
	TouchGeneric_Sheet.extras.rows = "";
	TouchGeneric_Sheet.extras.note = "";
	aref rows;
	makearef(rows, TouchGeneric_Sheet.extras.rows);
	object run;
	run.items = "";
	run.labels = "";
	for (int k = 0; k <= GetAttributesNum(&TouchGeneric_Items); k++)
	{
		string type = "";
		aref item;
		if (k < GetAttributesNum(&TouchGeneric_Items)) { item = TouchGeneric_Item(k); type = item.type; }
		if (type == "check" || type == "edit" || type == "slider" || type == "stepper") { TouchGeneric_Take(&run, k); continue; }
		if (TouchLib_Count(&run, "items") > 0)
		{
			TouchGeneric_ControlRows(rows, &TouchGeneric_SheetActs, model, &run, "extra-", "");
			DeleteAttribute(&run, "items");
			run.items = "";
		}
		if (type == "text" || type == "heading")
		{
			if (TouchGeneric_Sheet.extras.note != "") TouchGeneric_Sheet.extras.note = TouchGeneric_Sheet.extras.note + TouchLib_Newline();
			TouchGeneric_Sheet.extras.note = TouchGeneric_Sheet.extras.note + item.label;
		}
		if (type == "button" || type == "choice" || (type == "image" && item.label != ""))
			TouchGeneric_ActionRow(rows, &TouchGeneric_SheetActs, model, item, "extra-");
	}
	UiShow(&TouchGeneric_Sheet);
}

void TouchGeneric_Build()
{
	aref ui = GetEventData();
	if (ui.screen != "generic") return;
	aref model;
	makearef(model, ui.model);
	if (ui.capability == TOUCH_CAP_OTHER) TouchGeneric_Other(model);
	else TouchGeneric_Layout(model);
}

// A row of a choice tapped: the verb it means, on the row's identity when the engine lists its verbs.
void TouchGeneric_Choose(aref model, string node, int k)
{
	string slot = "r" + k;
	aref row;
	makearef(row, model.(node).rows.(slot));
	string verb = TouchGeneric_ChoiceVerb(row);
	if (verb == "") return;
	object action = {node = node, row = k, verb = verb, value = ""};
	if (sti(row.listed)) action.value = row.id;
	UiSendTo(&action);
}

void TouchGeneric_SendRow(string node, int k, string verb, string value)
{
	if (k < 0) { UiSend(node, verb, value); return; }
	object action = {node = node, row = k, verb = verb, value = value};
	UiSendTo(&action);
}

// The view's row a switch shows, for the prediction of its tap.
aref TouchGeneric_Shown(aref act)
{
	string place = act.place;
	int space = findSubStr(place, " ", 0);
	string side = TouchLib_Cut(place, 0, space);
	string block = TouchLib_Cut(place, space + 1, strlen(place));
	string slot = act.slot;
	aref row;
	makearef(row, TouchGeneric.(side).(block).rows.(slot));
	return row;
}

void TouchGeneric_Tap()
{
	aref ui = GetEventData();
	if (ui.screen != "generic") return;
	string id = GetEventData(), verb = GetEventData(), value = GetEventData();
	aref model;
	makearef(model, ui.model);
	if (id == "header-back") { UiBack(TouchGeneric_Exit); return; }
	aref acts;
	makearef(acts, TouchGeneric_Acts);
	if (ui.capability == TOUCH_CAP_OTHER) makearef(acts, TouchGeneric_SheetActs);
	int found = -1;
	for (int a = 0; a < GetAttributesNum(acts) && found < 0; a++)
	{
		aref candidate = GetAttributeN(acts, a);
		if (candidate.id == id) found = a;
	}
	if (found < 0) UiError("generic: no element " + id);
	aref act = GetAttributeN(acts, found);
	string kind = act.kind;
	string node = act.node;
	int row = sti(act.row);
	if (kind == "press") { TouchGeneric_SendRow(node, row, "press", ""); return; }
	if (kind == "choice") { TouchGeneric_Choose(model, node, row); return; }
	if (kind == "left" || kind == "right") { UiSend(node, kind, ""); return; }
	if (kind == "edit") { UiEditText(node, act.title); return; }
	if (kind == "radio") { TouchGeneric_SendRow(node, row, "toggle", "1"); return; }
	if (kind == "table")
	{
		for (int k = 0; k < TouchLib_Count(model, node + ".rows"); k++)
		{
			aref at = TouchLib_Item(model, node + ".rows", k);
			if ((at.id != "" && at.id == value) || (at.id == "" && at.index == value)) { TouchGeneric_Choose(model, node, sti(at.index)); return; }
		}
		UiError("generic: " + node + " has no row " + value);
	}
	if (kind == "tabs")
	{
		int tab = TouchLib_TapIndex(value, TouchLib_Count(act, "tabs"), "the tab of " + node);
		aref target = TouchLib_Item(act, "tabs", tab);
		if (sti(target.row) < 0) UiSend(target.node, "press", "");
		else TouchGeneric_Choose(model, target.node, sti(target.row));
		return;
	}
	if (kind == "slider")
	{
		if (verb != "commit") return; // the drag's progress
		aref slider;
		makearef(slider, model.(node));
		int low = sti(slider.min);
		int high = sti(slider.max);
		int step = sti(slider.step);
		if (step < 1) step = 1;
		int span = high - low;
		if (span < 1) span = 1;
		float wanted = UiNumber(value, "the position of " + node) * span / 100.0;
		int position = low + makeint(wanted / step + 0.5) * step;
		if (position > high) position = high;
		if (position < low) position = low;
		UiSend(node, "set", "" + position);
		return;
	}
	aref shown;
	if (kind == "toggle")
	{
		string on = "0";
		if (value == "1") on = "1";
		TouchGeneric_SendRow(node, row, "toggle", on);
		// The switch is drawn where the finger put it in the tap's frame (the game's answer confirms it).
		if (act.place == "") return;
		shown = TouchGeneric_Shown(act);
		shown.on = on == "1";
		UiPredict(&TouchGeneric);
		return;
	}
	if (kind == "segment")
	{
		int chosen = TouchLib_TapIndex(value, TouchLib_Count(act, "options"), "the option of " + node);
		int option = sti(GetAttributeValue(TouchLib_Item(act, "options", chosen)));
		string slot = "r" + option;
		if (!CheckAttribute(model, node + ".rows." + slot + ".offers.toggle")) return;
		TouchGeneric_SendRow(node, option, "toggle", "1");
		if (act.place == "") return;
		shown = TouchGeneric_Shown(act);
		shown.selected = chosen;
		UiPredict(&TouchGeneric);
		return;
	}
	UiError("generic: " + id + " is a " + kind + ", which takes no " + verb);
}
