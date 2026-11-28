// SPDX-License-Identifier: GPL-3.0-only
// Alchemy: the known recipes on the left; on the right the shown recipe, what a batch takes of the hero's items, how
// many batches to make (up to what the game counts) and the making, with the time the game gives it (pack screen
// s-alchemy).

#event_handler("UiBuild", "TouchAlchemy_Build");
#event_handler("UiTap", "TouchAlchemy_Tap");

// The adapter's state of the last build (.game), the recipe the batches were set for (.shown) and the batches the
// player set (.batches, -1 not yet: one when the game can make one).
object TouchAlchemy;
object TouchAlchemy_View;

// s: how long a choice of recipe waits for the game's answer.
float TouchAlchemy_Wait = 0.5;

int TouchAlchemy_Batches(aref game)
{
	int most = sti(game.batches);
	int batches = sti(TouchAlchemy.batches);
	if (batches < 0) batches = 1;
	if (batches > most) batches = most;
	return batches;
}

void TouchAlchemy_Recipes(aref game, aref view)
{
	view.recipes.label = UiText("alchemy.recipe");
	view.recipes.rows = "";
	aref rows;
	makearef(rows, view.recipes.rows);
	for (int k = 0; k < TouchLib_Count(game, "recipes"); k++)
	{
		aref recipe = TouchLib_Item(game, "recipes", k);
		aref at = TouchLib_Add(rows, "r");
		at.id = recipe.key;
		at.name = recipe.label;
		TouchLib_Copy(at, "image", recipe, "image");
		at.meta = "";
		if (recipe.kind != "") at.meta = UiText(recipe.kind);
		at.selected = sti(recipe.selected);
		at.disabled = !sti(recipe.selected) && !sti(recipe.can);
		at.reason = "";
		at.cells = "";
	}
}

void TouchAlchemy_Parts(aref game, aref view)
{
	view.parts.label = UiText("alchemy.component");
	view.parts.cols.c0.label = UiText("alchemy.have");
	view.parts.cols.c0.icon = "chest";
	view.parts.cols.c1.label = UiText("alchemy.per_batch");
	view.parts.cols.c1.icon = "potion";
	view.parts.rows = "";
	aref rows;
	makearef(rows, view.parts.rows);
	for (int k = 0; k < TouchLib_Count(game, "parts"); k++)
	{
		aref part = TouchLib_Item(game, "parts", k);
		aref at = TouchLib_Add(rows, "p");
		at.id = part.key;
		at.name = part.label;
		TouchLib_Copy(at, "image", part, "image");
		at.meta = "";
		if (sti(part.catalyst)) at.meta = UiText("alchemy.catalyst");
		if (sti(part.have) > 0) at.cells.c0 = part.have;
		else
		{
			at.cells.c0.text = UiText("alchemy.none");
			at.cells.c0.tone = "danger";
		}
		at.cells.c1 = part.per;
	}
}

void TouchAlchemy_Build()
{
	aref ui = GetEventData();
	if (ui.screen != "alchemy") return;
	if (UiFirst()) DeleteAttribute(&TouchAlchemy, "");
	aref game;
	makearef(game, TouchAlchemy.game);
	TouchAdpAlchemy_State(ui, game);
	string shown = "";
	if (CheckAttribute(game, "recipe")) shown = game.recipe.key;
	if (!CheckAttribute(&TouchAlchemy, "shown") || TouchAlchemy.shown != shown)
	{
		TouchAlchemy.shown = shown;
		TouchAlchemy.batches = -1;
	}
	int batches = TouchAlchemy_Batches(game);
	int most = sti(game.batches);
	aref view;
	makearef(view, TouchAlchemy_View);
	DeleteAttribute(view, "");
	view.title = game.title;
	if (view.title == "") view.title = UiText("alchemy.title");
	TouchAlchemy_Recipes(game, view);
	view.recipe = "";
	if (shown != "")
	{
		// The recipe's time for the batches set, for one while none are.
		int minutes = 1;
		if (batches > 1) minutes = batches;
		view.recipe.title = game.recipe.label;
		view.recipe.sub = "× " + game.recipe.yield + " " + UiText("alchemy.yield") + " · " +
		                  TouchAdpAlchemy_Minutes(ui, minutes) + " " + UiText("alchemy.minutes");
		TouchLib_Copy(view, "recipe.image", game, "recipe.image");
		view.recipe.info = game.recipe.text != "";
	}
	TouchAlchemy_Parts(game, view);
	int top = most;
	if (top < 1) top = 1;
	object q = {value = batches, min = 0, max = top, label = UiText("alchemy.batches"), summary = "", all = true,
	            allLabel = UiText("alchemy.max"), disabled = most == 0};
	if (most == 0) q.summary = "-";
	q.steps.s0 = 1;
	q.steps.s1 = 10;
	aref to;
	makearef(to, view.q);
	CopyAttributes(to, &q);
	object primary = {label = UiText("alchemy.craft"), sub = "", disabled = shown == "" || batches < 1, reason = "",
	                  busy = UiPending("primary")};
	if (batches > 0) primary.sub = TouchAdpAlchemy_Minutes(ui, batches) + " " + UiText("alchemy.minutes");
	makearef(to, view.primary);
	CopyAttributes(to, &primary);
	UiOn("recipes", "row");
	UiOn("info", "");
	UiOn("batches", "change");
	UiOn("batches", "commit");
	UiOn("primary", "");
	UiOn("header-back", "");
	UiShow(view);
}

void TouchAlchemy_Tap()
{
	aref ui = GetEventData();
	if (ui.screen != "alchemy") return;
	string id = GetEventData(), verb = GetEventData(), value = GetEventData();
	aref game;
	makearef(game, TouchAlchemy.game);
	if (id == "header-back") { TouchAdpAlchemy_Leave(ui); return; }
	if (id == "info") { UiRead(game.recipe.label, game.recipe.text); return; }
	if (id == "batches")
	{
		int batches = UiAmount(value, "the batches");
		if (batches < 0) batches = 0;
		TouchAlchemy.batches = batches;
		UiRebuild();
		return;
	}
	if (id == "primary")
	{
		// The game makes them in this tap; the next build shows what it left.
		if (!TouchAdpAlchemy_Make(ui, TouchAlchemy_Batches(game))) UiError("alchemy: the game made no batches");
		TouchAlchemy.batches = -1;
		UiRebuild();
		return;
	}
	if (id != "recipes") UiError("alchemy: no tap of " + id);
	for (int k = 0; k < TouchLib_Count(game, "recipes"); k++)
	{
		aref recipe = TouchLib_Item(game, "recipes", k);
		if (recipe.key != value) continue;
		if (sti(recipe.selected) || !sti(recipe.can)) return;
		TouchAdpAlchemy_Choose(ui, k);
		UiHold(TouchAlchemy_Wait);
		return;
	}
	UiError("alchemy: no recipe " + value);
}
