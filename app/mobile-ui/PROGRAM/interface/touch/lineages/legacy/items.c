// SPDX-License-Identifier: GPL-3.0-only
// The items from the interface items. Implements contracts/items.c. The rows are the game's TABLE_ITEMS (its tr<n>
// shows Items[] of tr<n>.index), what each slot holds the game's equipment functions and its pictures ITEM_<n> /
// SLOT_ITEM_<n>; the button EQUIP_BUTTON is the game's: its caption and whether it may be pressed are the game's
// answer to the row chosen (items.c SetItemInfo).
// data: slots.s<k>.{node, group, label, icon} (the window's pictures of what is worn, in our order), artefact.{group,
// label, icon, nodes.<the game's slot>} (the artefact slots: a person has those of chr.equip_item),
// figures.<group>.f<n>.{attr, key, digits, better} (the attributes of an item of that group set against the worn
// one), kinds.<group> (the attribute naming its kind), quest.<item type> (the quest items' types), texts (the game's
// file of item texts).

aref TouchLegItems_Data(aref ui)
{
	aref data;
	makearef(data, ui.data);
	return data;
}

// The item of a TABLE_ITEMS row.
int TouchLegItems_Index(aref row)
{
	string tr = "tr" + (sti(row.index) + 1);
	if (!CheckAttribute(&GameInterface, "TABLE_ITEMS." + tr + ".index")) UiError("items: the game's row " + tr + " has no item");
	return sti(GameInterface.TABLE_ITEMS.(tr).index);
}

string TouchLegItems_Group(int index)
{
	if (!CheckAttribute(&Items[index], "groupID")) return "";
	return Items[index].groupID;
}

bool TouchLegItems_Timed(int index)
{
	if (!CheckAttribute(&Items[index], "time")) return false;
	return sti(Items[index].time) > -1;
}

// A picture as group/image, "" none: the slots' pictures are told apart from the rows' by it.
string TouchLegItems_PictureName(aref at)
{
	if (!CheckAttribute(at, "images.i0")) return "";
	return at.images.i0.group + "/" + at.images.i0.image;
}

void TouchLegItems_Picture(aref from, aref out)
{
	aref image;
	makearef(image, out.image);
	if (!TouchLegLib_Image(from, image)) out.image = "";
}

// The person the game shows: the one its equipment caption names (SetVariable), else the one listed.
int TouchLegItems_Shown(aref model, aref out, int listed)
{
	if (!CheckAttribute(model, "SETUP_FRAME_CAPTION")) return listed;
	string caption = UiOneLine(model.SETUP_FRAME_CAPTION.label);
	for (int k = 0; k < TouchLib_Count(out, "people"); k++)
	{
		aref person = TouchLib_Item(out, "people", k);
		if (TouchLib_EndsWith(caption, " " + person.label)) return k;
	}
	UiError("items: the game shows a person not in its character list: " + caption);
	return listed;
}

void TouchLegItems_Rows(aref ui, ref chr, aref out)
{
	aref model = TouchLegLib_Model(ui);
	aref data = TouchLegItems_Data(ui);
	aref rows;
	makearef(rows, out.rows);
	for (int k = 0; k < TouchLib_Count(model, "TABLE_ITEMS.rows"); k++)
	{
		aref row = TouchLib_Item(model, "TABLE_ITEMS.rows", k);
		int index = TouchLegItems_Index(row);
		string id = Items[index].id;
		aref at = TouchLib_Add(rows, "r");
		at.label = UiOneLine(TouchLegLib_Cell(row, 0));
		at.image = "";
		aref image;
		makearef(image, at.image);
		TouchLegLib_Picture(row, 0, image);
		at.count = TouchLegLib_Amount(row, 2, "TABLE_ITEMS count");
		at.weight = UiDecimal(UiOneLine(TouchLegLib_Cell(row, 3)));
		at.selected = row.value == "1";
		at.group = TouchLegItems_Group(index);
		at.item = index;
		at.worn = false;
		if (IsEquipCharacterByItem(chr, id)) at.worn = true;
		if (IsEquipCharacterByArtefact(chr, id)) at.worn = true;
		at.quest = false;
		if (CheckAttribute(&Items[index], "itemType")) at.quest = CheckAttribute(data, "quest." + Items[index].itemType);
	}
}

// k of the row of item `id`, -1 none.
int TouchLegItems_RowOf(aref out, string id)
{
	if (id == "") return -1;
	for (int k = 0; k < TouchLib_Count(out, "rows"); k++)
	{
		aref at = TouchLib_Item(out, "rows", k);
		if (Items[sti(at.item)].id == id) return k;
	}
	return -1;
}

// A slot of the window's: what it holds is the game's equipment of its group (an artefact slot: of that slot), drawn
// by its picture; a slot two kinds share (the pistol's and the musket's) holds it only where the window draws it.
void TouchLegItems_Slot(aref ui, aref out, aref slot, string node, string held, string days)
{
	aref model = TouchLegLib_Model(ui);
	aref slots;
	makearef(slots, out.slots);
	aref at = TouchLib_Add(slots, "s");
	at.label = UiText(slot.label);
	at.icon = slot.icon;
	at.group = slot.group;
	at.row = -1;
	at.days = "";
	if (!CheckAttribute(model, node + ".images.i0")) return;
	at.days = days;
	aref picture;
	makearef(picture, model.(node));
	TouchLegItems_Picture(picture, at);
	at.row = TouchLegItems_RowOf(out, held);
	if (sti(at.row) < 0) return;
	aref row = TouchLib_Item(model, "TABLE_ITEMS.rows", sti(at.row));
	aref cell;
	makearef(cell, row.cells.c0);
	if (TouchLegItems_PictureName(cell) != TouchLegItems_PictureName(picture)) at.row = -1;
}

void TouchLegItems_Slots(aref ui, ref chr, aref out)
{
	aref data = TouchLegItems_Data(ui);
	aref worn;
	makearef(worn, data.slots);
	for (int k = 0; k < GetAttributesNum(worn); k++)
	{
		aref slot = GetAttributeN(worn, k);
		TouchLegItems_Slot(ui, out, slot, slot.node, GetCharacterEquipByGroup(chr, slot.group), "");
	}
	if (!CheckAttribute(chr, "equip_item")) return;
	aref artefact, equip;
	makearef(artefact, data.artefact);
	makearef(equip, chr.equip_item);
	for (int n = 0; n < GetAttributesNum(equip); n++)
	{
		string name = GetAttributeName(GetAttributeN(equip, n));
		if (!CheckAttribute(artefact, "nodes." + name)) UiError("items: the window draws no artefact slot " + name);
		string held = GetCharacterEquipBySlot(chr, name);
		string days = "";
		if (held != "")
		{
			int index = GetItemIndex(held);
			if (index >= 0 && TouchLegItems_Timed(index)) days = GetCharacterSlotRemainTime(chr, name);
		}
		TouchLegItems_Slot(ui, out, artefact, artefact.nodes.(name), held, days);
	}
}

// The figures of an item of a group that has them, each set against the worn item of that group.
void TouchLegItems_Figures(aref ui, ref chr, int index, aref detail)
{
	aref data = TouchLegItems_Data(ui);
	string group = TouchLegItems_Group(index);
	if (group == "" || !CheckAttribute(data, "figures." + group)) return;
	int against = -1;
	string worn = GetCharacterEquipByGroup(chr, group);
	if (worn != "" && worn != Items[index].id) against = GetItemIndex(worn);
	aref figures, list;
	makearef(figures, data.figures.(group));
	makearef(list, detail.figures);
	for (int n = 0; n < GetAttributesNum(figures); n++)
	{
		aref figure = GetAttributeN(figures, n);
		string attr = figure.attr;
		if (CheckAttribute(&Items[index], attr))
		{
			aref at = TouchLib_Add(list, "f");
			at.key = figure.key;
			at.value = Items[index].(attr);
			at.digits = figure.digits;
			at.better = figure.better;
			if (against >= 0) { if (CheckAttribute(&Items[against], attr)) at.against = Items[against].(attr); }
			if (attr == "Weight") detail.weight = "";
		}
	}
}

// The item's description as the game's file has it (SetItemInfo writes it with the figures and the weight into one
// text), and its potion's health.
string TouchLegItems_Text(aref data, int index)
{
	ref item = &Items[index];
	int file = LanguageOpenFile(data.texts);
	string text = GetAssembledString(LanguageConvertString(file, item.describe), item);
	if (CheckAttribute(item, "potion.health"))
	{
		string health = LanguageConvertString(file, "Potion parameters") + ": " + LanguageConvertString(file, "health value");
		if (stf(item.potion.health) >= 0) health += "+";
		text += TouchLib_Newline() + health + sti(item.potion.health);
	}
	LanguageCloseFile(file);
	return TouchLegLib_StockText(text);
}

void TouchLegItems_Detail(aref ui, ref chr, aref out, int k)
{
	aref model = TouchLegLib_Model(ui);
	aref data = TouchLegItems_Data(ui);
	aref at = TouchLib_Item(out, "rows", k);
	int index = sti(at.item);
	string group = at.group;
	aref detail;
	makearef(detail, out.detail);
	detail.row = k;
	detail.title = at.label;
	detail.image = "";
	aref image;
	makearef(image, detail.image);
	TouchLegLib_Picture(TouchLib_Item(model, "TABLE_ITEMS.rows", k), 0, image);
	if (CheckAttribute(model, "INFO_PIC.images.i0"))
	{
		aref picture;
		makearef(picture, model.INFO_PIC);
		TouchLegLib_Image(picture, image);
	}
	detail.slot = "";
	for (int s = 0; s < TouchLib_Count(out, "slots"); s++)
	{
		aref slot = TouchLib_Item(out, "slots", s);
		if (slot.group == group && detail.slot == "") detail.slot = slot.label;
	}
	detail.worn = at.worn;
	detail.kind = "";
	if (CheckAttribute(data, "kinds." + group))
	{
		string attr = data.kinds.(group);
		if (CheckAttribute(&Items[index], attr)) detail.kind = XI_ConvertString(Items[index].(attr));
	}
	detail.weight = "";
	if (CheckAttribute(&Items[index], "Weight")) detail.weight = Items[index].Weight;
	TouchLegItems_Figures(ui, chr, index, detail);
	detail.text = TouchLegItems_Text(data, index);
	detail.term = "";
	detail.left = "";
	if (group == data.artefact.group && TouchLegItems_Timed(index))
	{
		detail.term = Items[index].time;
		if (IsEquipCharacterByArtefact(chr, Items[index].id))
			detail.left = GetCharacterSlotRemainTime(chr, GetCharacterEquipSlot(chr, Items[index].id));
	}
	aref action;
	makearef(action, detail.action);
	action.label = UiOneLine(TouchLegLib_Label(model, "EQUIP_BUTTON"));
	action.can = UiOffers("EQUIP_BUTTON", "press");
	action.reason = "";
	if (CheckAttribute(model, "EQUIP_BUTTON.reason")) action.reason = model.EQUIP_BUTTON.reason;
	// Taking off a timed artefact uses it up (RemoveCharacterArtefactEquip); the game asks nothing, the screen warns.
	action.warn = sti(detail.worn) && detail.term != "";
}

void TouchAdpItems_State(aref ui, aref out)
{
	aref model = TouchLegLib_Model(ui);
	out.title = UiOneLine(TouchLegLib_Label(model, "TITLE"));
	out.load = UiDecimal(UiOneLine(TouchLib_AfterColon(TouchLegLib_Label(model, "Weight_TEXT"))));
	int listed = TouchLegLib_People(model, out);
	int shown = TouchLegItems_Shown(model, out, listed);
	out.shown = shown;
	out.picked = listed;
	out.waiting = listed >= 0 && listed != shown;
	if (sti(out.waiting))
	{
		// The game shows the person of the slot picked in its next frame (items.c ProcessFrame).
		string answer = UiAnswer();
		if (answer == "waiting" || answer == "applied") UiAwait();
	}
	aref person = TouchLib_Item(out, "people", shown);
	ref chr = GetCharacter(sti(person.character));
	object words;
	TouchLegLib_Tabs(model, &words, out);
	TouchLegItems_Rows(ui, chr, out);
	TouchLegItems_Slots(ui, chr, out);
	for (int k = 0; k < TouchLib_Count(out, "rows"); k++)
	{
		aref at = TouchLib_Item(out, "rows", k);
		if (sti(at.selected)) TouchLegItems_Detail(ui, chr, out, k);
	}
	if (CheckAttribute(model, "MAP_CAPTION") || CheckAttribute(model, "MAP_EXIT_BTN"))
	{
		out.map.title = UiOneLine(TouchLegLib_Label(model, "MAP_CAPTION"));
		out.map.text = UiOneLine(TouchLegLib_Label(model, "MAP_TEXT"));
		aref image;
		makearef(image, out.map.image);
		out.map.image = "";
		if (CheckAttribute(model, "MAP_PICTURE"))
		{
			aref picture;
			makearef(picture, model.MAP_PICTURE);
			TouchLegLib_Image(picture, image);
		}
	}
}

void TouchAdpItems_Select(aref ui, int row)
{
	TouchLegLib_Choose(TouchLegLib_Model(ui), "TABLE_ITEMS", row);
}

// The game's button acts on the row it was answered for: the engine lets the press through only while that row is
// still the chosen one.
void TouchAdpItems_Act(aref ui)
{
	aref model = TouchLegLib_Model(ui);
	for (int k = 0; k < TouchLib_Count(model, "TABLE_ITEMS.rows"); k++)
	{
		aref row = TouchLib_Item(model, "TABLE_ITEMS.rows", k);
		if (row.value == "1") { UiSend("EQUIP_BUTTON", "press", row.id); return; }
	}
	UiError("items: the game's action without a row chosen");
}

void TouchAdpItems_Tab(aref ui, int tab)
{
	aref model = TouchLegLib_Model(ui);
	object words, state;
	TouchLegLib_Tabs(model, &words, &state);
	aref at = TouchLib_Item(&state, "tabs", tab);
	UiSend(at.node, "press", "");
}

void TouchAdpItems_Person(aref ui, int person)
{
	aref model = TouchLegLib_Model(ui);
	object state;
	TouchLegLib_People(model, &state);
	TouchLegLib_PickPerson(model, &state, person);
}

void TouchAdpItems_CloseMap(aref ui)
{
	if (UiOffers("MAP_EXIT_BTN", "press")) UiSend("MAP_EXIT_BTN", "press", "");
}

void TouchAdpItems_Section(aref ui, string section)
{
	TouchLegLib_Section(TouchLegLib_Model(ui), section);
}

void TouchAdpItems_Leave(aref ui)
{
	if (CheckAttribute(ui, "model.EXIT_BTN")) { if (UiOffers("EXIT_BTN", "press")) { UiSend("EXIT_BTN", "press", ""); return; } }
	UiBack("");
}
