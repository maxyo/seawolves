// SPDX-License-Identifier: GPL-3.0-only
// The maps of this lineage (mapview, MapBest, PaperMap): the scrolled picture MAP the engine exports with its base
// size, view and imagelist markers, a point given back as the right click the scripts read, and the colony window
// they fill while it is held. The binding's data: exit (the node that leaves), backdrop (the picture under a
// transparent MAP, "" none), and pictures (absent: the window names its goods by nodes), MapBest's goods drawn as
// pictures at the script's own places: node, colonyFrom (x of the colony's goods), bands.b<k>.{caption, from, to}
// (their y bands), returnFrom (x of the trade assistant's second column), prices.{p0, p1} (the nodes of the two
// columns' profits). Implements contracts/map.c.

// One section of the colony window: <prefix>_CAPTION heads the <prefix>… pictures, named by the <prefix>_INFO lines,
// and its other texts; a section with neither is left out.
void TouchLegMap_Section(aref model, string prefix, string page, aref sections)
{
	string caption = prefix + "_CAPTION";
	string named = prefix + "_INFO";
	object section = {title = TouchLegLib_Label(model, caption), page = page};
	aref goods, lines, at;
	makearef(goods, section.goods);
	makearef(lines, section.lines);
	object names;
	TouchLegLib_Lines(model, named, &names, "n");
	int k;
	aref node;
	string name;
	for (k = 0; k < GetAttributesNum(model); k++)
	{
		node = GetAttributeN(model, k);
		name = GetAttributeName(node);
		if (TouchLib_StartsWith(name, prefix) && name != caption && node.type == "image")
		{
			if (CheckAttribute(node, "images.i0"))
			{
				at = TouchLib_Add(goods, "g");
				aref image, source;
				makearef(image, at.image);
				makearef(source, node.images.i0);
				CopyAttributes(image, source);
				at.label = "";
				if (GetAttributesNum(goods) <= GetAttributesNum(&names))
				{
					aref label = GetAttributeN(&names, GetAttributesNum(goods) - 1);
					at.label = label.text;
				}
			}
		}
	}
	for (k = 0; k < GetAttributesNum(model); k++)
	{
		node = GetAttributeN(model, k);
		name = GetAttributeName(node);
		if (TouchLib_StartsWith(name, prefix) && name != caption && node.type != "image")
			if (GetAttributesNum(goods) == 0 || name != named) TouchLegLib_Lines(model, name, lines, "l");
	}
	if (GetAttributesNum(goods) == 0 && GetAttributesNum(lines) == 0) return;
	at = TouchLib_Add(sections, "s");
	CopyAttributes(at, &section);
}

// A picture of MapBest's goods as a good {image, label, value}: its name is the row's label, else its id.
void TouchLegMap_Good(aref picture, aref goods, string value)
{
	aref good = TouchLib_Add(goods, "g");
	aref image, source;
	makearef(image, good.image);
	makearef(source, picture.images.i0);
	CopyAttributes(image, source);
	good.label = picture.label;
	if (good.label == "") good.label = picture.id;
	if (value != "") good.value = value;
}

int TouchLegMap_Place(aref picture, string axis)
{
	return UiAmount(picture.context.(axis), "the place " + axis + " of " + picture.id);
}

// MapBest's goods by the places its script drew them at: the colony's own in y bands right of colonyFrom, the trade
// assistant's in two columns left of it, each good beside its profit on the price line of its row.
void TouchLegMap_Pictures(aref model, aref layout, aref sections)
{
	string drawn = layout.node;
	aref rows;
	makearef(rows, model.(drawn).rows);
	aref bands;
	makearef(bands, layout.bands);
	int k, b;
	aref picture, at, goods;
	object section, listed, prices, words;
	for (b = 0; b < GetAttributesNum(bands); b++)
	{
		aref band = GetAttributeN(bands, b);
		DeleteAttribute(&section, "");
		section.title = TouchLegLib_Label(model, band.caption);
		section.page = "trade";
		section.lines = "";
		makearef(goods, section.goods);
		for (k = 0; k < GetAttributesNum(rows); k++)
		{
			picture = GetAttributeN(rows, k);
			int x = TouchLegMap_Place(picture, "x");
			int y = TouchLegMap_Place(picture, "y");
			if (x >= sti(layout.colonyFrom) && y >= sti(band.from) && y < sti(band.to))
				TouchLegMap_Good(picture, goods, "");
		}
		if (GetAttributesNum(goods) > 0)
		{
			at = TouchLib_Add(sections, "s");
			CopyAttributes(at, &section);
		}
	}
	for (int column = 0; column < 2; column++)
	{
		DeleteAttribute(&listed, "");
		for (k = 0; k < GetAttributesNum(rows); k++)
		{
			picture = GetAttributeN(rows, k);
			int left = TouchLegMap_Place(picture, "x");
			bool back = left >= sti(layout.returnFrom);
			if (left < sti(layout.colonyFrom) && back == (column == 1))
			{
				at = TouchLib_Add(&listed, "r");
				CopyAttributes(at, picture);
				at.by = TouchLegMap_Place(picture, "y");
			}
		}
		UiOrder(&listed, "by", false);
		DeleteAttribute(&prices, "");
		DeleteAttribute(&words, "");
		string node = "p" + column;
		TouchLegLib_Lines(model, layout.prices.(node), &prices, "p");
		for (k = 0; k < GetAttributesNum(&prices); k++)
		{
			at = GetAttributeN(&prices, k);
			TouchLib_Words(at.text, &words);
		}
		string direction = "TRADEASSISTANT_TO";
		if (column == 1) direction = "TRADEASSISTANT_FROM";
		DeleteAttribute(&section, "");
		section.title = TouchLegLib_Label(model, direction);
		section.page = "assistant";
		section.lines = "";
		makearef(goods, section.goods);
		for (k = 0; k < GetAttributesNum(&listed); k++)
		{
			string profit = "";
			if (k < GetAttributesNum(&words))
			{
				at = GetAttributeN(&words, k);
				profit = "+" + at.text;
			}
			TouchLegMap_Good(GetAttributeN(&listed, k), goods, profit);
		}
		if (GetAttributesNum(goods) > 0)
		{
			at = TouchLib_Add(sections, "s");
			CopyAttributes(at, &section);
		}
	}
	string special = TouchLegLib_Label(model, "TRADEASSISTANT_SPECIAL");
	if (special == "") return;
	at = TouchLib_Add(sections, "s");
	at.title = "";
	at.page = "assistant";
	at.goods = "";
	at.lines.l0.text = special;
}

void TouchLegMap_Colony(aref model, aref data, aref out)
{
	string caption = TouchLegLib_Label(model, "INFO_CAPTION");
	if (caption == "") return;
	aref colony;
	makearef(colony, out.colony);
	colony.title = caption;
	colony.nation = "";
	object flag;
	if (TouchLegLib_NodeImage(model, "INFO_NATION_PICTURE", &flag)) colony.nation = TouchLegLib_FlagOf(&flag);
	object travel;
	TouchLegLib_Lines(model, "COLONY_TRAVEL_INFO", &travel, "l");
	TouchLegLib_Lines(model, "COLONY_TIME_INFO", &travel, "l");
	colony.sub = "";
	int k;
	aref at;
	for (k = 0; k < GetAttributesNum(&travel); k++)
	{
		at = GetAttributeN(&travel, k);
		if (k > 0) colony.sub = colony.sub + " · ";
		colony.sub = colony.sub + at.text;
	}
	// The window's facts are two columns of lines: the names and their values.
	object names, values;
	TouchLegLib_Lines(model, "COLONY_INFO_TEXT", &names, "l");
	TouchLegLib_Lines(model, "COLONY_INFO_TEXT2", &values, "l");
	if (GetAttributesNum(&values) == 0) TouchLegLib_Lines(model, "COLONY_INFO", &values, "l");
	aref facts;
	makearef(facts, colony.facts);
	for (k = 0; k < GetAttributesNum(&names); k++)
	{
		aref name = GetAttributeN(&names, k);
		aref fact = TouchLib_Add(facts, "f");
		fact.k = name.text;
		fact.v = "";
		if (k < GetAttributesNum(&values))
		{
			at = GetAttributeN(&values, k);
			fact.v = at.text;
		}
	}
	colony.factsTitle = TouchLegLib_Label(model, "COLONY_INFO_LABEL");
	aref notes, sections;
	makearef(notes, colony.notes);
	TouchLegLib_Lines(model, "COLONY_INFO_SIEGE", notes, "n");
	makearef(sections, colony.sections);
	if (CheckAttribute(data, "pictures"))
	{
		if (CheckAttribute(model, data.pictures.node + ".rows"))
		{
			aref layout;
			makearef(layout, data.pictures);
			TouchLegMap_Pictures(model, layout, sections);
			return;
		}
	}
	TouchLegMap_Section(model, "EXPORT", "trade", sections);
	TouchLegMap_Section(model, "IMPORT", "trade", sections);
	TouchLegMap_Section(model, "CONTRABAND", "trade", sections);
	TouchLegMap_Section(model, "AGGRESSIVE", "trade", sections);
	TouchLegMap_Section(model, "TRADEASSISTANT", "assistant", sections);
}

void TouchAdpMap_State(aref ui, aref out)
{
	aref model = TouchLegLib_Model(ui);
	aref data;
	makearef(data, ui.data);
	out.title = TouchLegLib_Label(model, "TITLE");
	aref map, image;
	makearef(map, out.map);
	makearef(image, map.image);
	if (!TouchLegLib_NodeImage(model, "MAP", image))
		if (!TouchLegLib_NodeImage(model, data.backdrop, image)) UiError("map: no picture for MAP (nor its backdrop)");
	if (!CheckAttribute(model, "MAP.context.base")) UiError("map: MAP has no base size");
	map.base = model.MAP.context.base;
	map.view = model.MAP.context.view;
	aref markers;
	makearef(markers, out.markers);
	int k;
	for (k = 0; k < TouchLib_Count(model, "MAP.rows"); k++)
	{
		aref row = TouchLib_Item(model, "MAP.rows", k);
		if (CheckAttribute(row, "images.i0"))
		{
			aref marker = TouchLib_Add(markers, "m");
			object place = {id = row.id, x = row.context.x, y = row.context.y, w = row.context.width,
			                h = row.context.height};
			CopyAttributes(marker, &place);
			makearef(image, marker.image);
			aref source;
			makearef(source, row.images.i0);
			CopyAttributes(image, source);
		}
	}
	out.mapsTitle = TouchLegLib_Label(model, "STR_1");
	aref maps;
	makearef(maps, out.maps);
	for (k = 0; k < TouchLib_Count(model, "TABLE_MAPS.rows"); k++)
	{
		aref listed = TouchLib_Item(model, "TABLE_MAPS.rows", k);
		object entry = {label = listed.cells.c0.text, selected = listed.value == "1", can = sti(listed.enabled)};
		aref at = TouchLib_Add(maps, "m");
		CopyAttributes(at, &entry);
	}
	TouchLegMap_Colony(model, data, out);
}

void TouchAdpMap_Point(aref ui, string at)
{
	if (UiOffers("MAP", "point")) UiSend("MAP", "point", at);
}

void TouchAdpMap_Release(aref ui)
{
	UiSend("MAP", "release", "");
}

void TouchAdpMap_Choose(aref ui, int map)
{
	TouchLegLib_Choose(TouchLegLib_Model(ui), "TABLE_MAPS", map);
}

// A window without its exit button (MapBest's model has none) is left the game's own way, by its back.
void TouchAdpMap_Leave(aref ui)
{
	string exit = ui.data.exit;
	if (!CheckAttribute(ui, "model." + exit)) exit = "";
	UiBack(exit);
}
