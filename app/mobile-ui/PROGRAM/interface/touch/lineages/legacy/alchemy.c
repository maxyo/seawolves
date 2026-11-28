// SPDX-License-Identifier: GPL-3.0-only
// Alchemy from the interface alchemy: the recipes of its scroll as the engine lists them (ANDROID_ALCHEMY_RECIPE, the
// shown one selected, its name and picture), the shown recipe's components as Items[] names and draws them, and the game's own count, time and
// making of batches (interface_utils.c AlchemyBatches, AlchemyMinutes, AlchemyMake, a script fix of TEHO 1.7.3; the
// pot is NullCharacter.alchemy). The binding's data: kinds.<the item's groupID> = the string key of its kind.
// Implements contracts/alchemy.c.

// An item's picture as the window draws it: its atlas group and picture.
void TouchLegAlchemy_Picture(int index, aref out)
{
	out.kind = "atlas";
	out.group = Items[index].picTexture;
	out.image = "itm" + Items[index].picIndex;
}

int TouchLegAlchemy_Item(string id)
{
	int index = GetItemIndex(id);
	if (index < 0) UiError("alchemy: the game has no item " + id);
	return index;
}

void TouchAdpAlchemy_State(aref ui, aref out)
{
	aref model = TouchLegLib_Model(ui);
	DeleteAttribute(out, "");
	out.title = TouchLib_Trim(TouchLegLib_Label(model, "STORECAPTION1"));
	out.batches = 0;
	if (!CheckAttribute(model, "ANDROID_ALCHEMY_RECIPE.rows")) UiError("alchemy: the engine lists no recipes");
	aref recipes;
	makearef(recipes, out.recipes);
	string shown = "";
	int shownAt = -1;
	for (int k = 0; k < TouchLib_Count(model, "ANDROID_ALCHEMY_RECIPE.rows"); k++)
	{
		aref row = TouchLib_Item(model, "ANDROID_ALCHEMY_RECIPE.rows", k);
		int index = TouchLegAlchemy_Item(row.id);
		string kind = "";
		string group = "";
		if (CheckAttribute(&Items[index], "groupID")) group = Items[index].groupID;
		if (group != "" && CheckAttribute(ui, "data.kinds." + group)) kind = ui.data.kinds.(group);
		object recipe = {key = row.id, label = row.label, kind = kind, selected = row.value == "1",
		                 can = CheckAttribute(row, "offers.choose")};
		aref at = TouchLib_Add(recipes, "r");
		CopyAttributes(at, &recipe);
		aref image;
		makearef(image, at.image);
		TouchLegLib_Image(row, image);
		if (sti(recipe.selected))
		{
			shown = row.id;
			shownAt = k;
		}
	}
	if (shown == "") return;
	int made = TouchLegAlchemy_Item(shown);
	ref item = &Items[made];
	if (!CheckAttribute(item, "multiobject.qty")) UiError("alchemy: " + shown + " has no multiobject.qty");
	aref shownRow = TouchLib_Item(out, "recipes", shownAt);
	object chosen = {key = shown, label = shownRow.label, yield = sti(item.multiobject.qty),
	                 text = TouchLegLib_StockText(GetConvertStr(item.describe, "ItemsDescribe.txt"))};
	aref to;
	makearef(to, out.recipe);
	CopyAttributes(to, &chosen);
	TouchLib_Copy(out, "recipe.image", shownRow, "image");
	// The components as AddToTable lists them: what the hero carries and what lies in the pot.
	ref hero = GetMainCharacter();
	aref pot, components, parts;
	makearef(pot, NullCharacter.alchemy);
	makearef(components, item.component);
	makearef(parts, out.parts);
	for (int n = 0; n < GetAttributesNum(components); n++)
	{
		aref component = GetAttributeN(components, n);
		int part = TouchLegAlchemy_Item(component.id);
		object entry = {key = component.id, label = GetConvertStr(Items[part].name, "ItemsDescribe.txt"),
		                have = GetCharacterFreeItem(hero, component.id) + GetCharacterFreeItem(pot, component.id),
		                per = sti(component.qty), catalyst = component.use != "component"};
		aref at2 = TouchLib_Add(parts, "p");
		CopyAttributes(at2, &entry);
		aref image2;
		makearef(image2, at2.image);
		TouchLegAlchemy_Picture(part, image2);
	}
	out.batches = AlchemyBatches(shown, true);
}

void TouchAdpAlchemy_Choose(aref ui, int recipe)
{
	aref model = TouchLegLib_Model(ui);
	aref row = TouchLib_Item(model, "ANDROID_ALCHEMY_RECIPE.rows", recipe);
	object action = {node = "ANDROID_ALCHEMY_RECIPE", row = row.index, verb = "choose", value = row.id};
	UiSendTo(&action);
}

int TouchAdpAlchemy_Minutes(aref ui, int batches)
{
	return AlchemyMinutes(batches);
}

bool TouchAdpAlchemy_Make(aref ui, int batches)
{
	aref model = TouchLegLib_Model(ui);
	for (int k = 0; k < TouchLib_Count(model, "ANDROID_ALCHEMY_RECIPE.rows"); k++)
	{
		aref row = TouchLib_Item(model, "ANDROID_ALCHEMY_RECIPE.rows", k);
		if (row.value == "1") return AlchemyMake(row.id, batches);
	}
	UiError("alchemy: no recipe is shown");
	return false;
}

void TouchAdpAlchemy_Leave(aref ui)
{
	UiBack(TouchLegLib_Exit(TouchLegLib_Model(ui), "EXIT_BTN"));
}
