// SPDX-License-Identifier: GPL-3.0-only
// The save list: every record in one table (the new record first, then the autosaves, then the player's own), the
// chosen record in the detail column with its primary action and the trash glyph, and the game's questions as a
// dialog titled by what the player asked for. A load, save, overwrite or delete the game refuses is told with the
// record's name; a load asked for from the title (no game in progress) needs no question.

#event_handler("UiBuild", "TouchSaves_Build");
#event_handler("UiTap", "TouchSaves_Tap");

object TouchSaves;
object TouchSaves_Game;  // the adapter's state of the last build
// What the screen keeps between builds: fromTitle; chosen (the record's key); asked, askedKey, askedName (what the player asked for last); watching (its answer is awaited); here (the
// open question answers it); answered, named (the question, the name editor answered for the player).
object TouchSaves_State;
// Where and whose a record is, under its name: «<hero> · <location>», the location, else its file.
string TouchSaves_Identity(aref record)
{
	if (record.location == "")
	{
		if (sti(record.fresh)) return "";
		return record.file;
	}
	if (record.hero == "") return record.location;
	return record.hero + " · " + record.location;
}

// A record's row id: its file, the new record's "new" (its key is "", which a row cannot be tapped by).
string TouchSaves_RowId(string key)
{
	if (key == "") return "new";
	return "file:" + key;
}

// What the player sees as the record's name: the new record, an autosave, else the name the game gave it (its file:
// the location, «<profile> QuickSave <n>»).
string TouchSaves_Name(aref record)
{
	if (sti(record.fresh)) return UiText("save.new");
	if (sti(record.autosave)) return UiText("save.autosave");
	return record.file;
}

// "2 окт. 1654" of "1654-10-02"; "" for a date the record does not carry.
string TouchSaves_Date(string date)
{
	if (strlen(date) < 10) return "";
	int month = UiAmount(TouchLib_Cut(date, 5, 7), "a record's month");
	if (month < 1 || month > 12) return "";
	return UiAmount(TouchLib_Cut(date, 8, 10), "a record's day") + " " + UiText("common.month_" + month) + " " +
	       UiAmount(TouchLib_Cut(date, 0, 4), "a record's year");
}

// "сегодня, 22:14", "вчера, 23:05", "7 нояб., 20:12", "18 окт. 2025"
string TouchSaves_Created(aref record)
{
	string created = record.created;
	if (strlen(created) < 16) return "";
	string clock = TouchLib_Cut(created, 11, 16);
	if (record.createdWhen == "today") return UiText("save.today") + ", " + clock;
	if (record.createdWhen == "yesterday") return UiText("save.yesterday") + ", " + clock;
	string date = TouchSaves_Date(created);
	if (date == "") return "";
	// A record of another year needs its year more than its time (and the column stays narrow).
	string day = UiAmount(TouchLib_Cut(created, 8, 10), "a record's day") + " " +
	             UiText("common.month_" + UiAmount(TouchLib_Cut(created, 5, 7), "a record's month"));
	if (record.createdWhen == "year") return day + ", " + clock;
	return date;
}

// 402 → "6 ч 42 мин"
string TouchSaves_PlayTime(int minutes)
{
	string text = "";
	if (minutes >= 60) text = (minutes / 60) + " " + UiText("save.hours") + " ";
	return text + (minutes % 60) + " " + UiText("save.minutes");
}

// The records as the table lists them: the new one, the autosaves (none when writing: never written over), the rest.
void TouchSaves_Order(aref game, aref out)
{
	for (int group = 0; group < 3; group++)
		for (int k = 0; k < TouchLib_Count(game, "records"); k++)
		{
			aref record = TouchLib_Item(game, "records", k);
			int in = 2;
			if (sti(record.fresh)) in = 0;
			else { if (sti(record.autosave)) in = 1; }
			if (in != group) continue;
			if (in == 1 && sti(game.saving)) continue;
			aref at = TouchLib_Add(out, "r");
			CopyAttributes(at, record);
		}
}

void TouchSaves_Cell(aref cells, string text, string sub)
{
	aref at = TouchLib_Add(cells, "c");
	at.text = text;
	at.sub = sub;
}

void TouchSaves_Detail(aref game, aref record)
{
	aref detail;
	makearef(detail, TouchSaves.detail);
	bool located = record.location != "";
	detail.title = TouchSaves_Name(record);
	detail.sub = "";
	if (located)
	{
		detail.title = record.location;
		detail.sub = record.hero;
		if (sti(record.autosave))
		{
			detail.sub = TouchSaves_Name(record);
			if (record.hero != "") detail.sub = detail.sub + " · " + record.hero;
		}
	}
	detail.preview = "";
	if (!sti(record.fresh)) detail.preview = record.preview;
	detail.facts = "";
	aref facts;
	makearef(facts, detail.facts);
	aref fact;
	string date = TouchSaves_Date(record.gameDate);
	if (date != "")
	{
		fact = TouchLib_Add(facts, "f");
		fact.k = UiText("save.game_date");
		fact.v = date;
		if (record.gameTime != "") fact.v = date + ", " + record.gameTime;
	}
	if (sti(record.minutes) >= 0)
	{
		fact = TouchLib_Add(facts, "f");
		fact.k = UiText("save.play_time");
		fact.v = TouchSaves_PlayTime(sti(record.minutes));
	}
	string created = TouchSaves_Created(record);
	if (created != "")
	{
		fact = TouchLib_Add(facts, "f");
		fact.k = UiText("save.created");
		fact.v = created;
	}
	detail.primary = UiText("save.load");
	if (sti(game.saving)) detail.primary = UiText("save.overwrite");
	if (sti(record.fresh)) detail.primary = UiText("save.save");
	// A record without a readable description cannot be loaded: the press says why.
	bool unloadable = !sti(game.saving) && sti(record.unreadable);
	detail.disabled = unloadable || !sti(record.can);
	detail.reason = record.reason;
	if (unloadable) detail.reason = UiText("save.unreadable");
	detail.delete = sti(record.canDelete) && !sti(record.autosave); // no quick delete of an autosave
	detail.busy = UiPending("primary") || UiPending("delete");
	UiOn("primary", "");
	UiOn("delete", "");
}

// The game's own question: titled by what the player asked for last when it answers that, else by its own text.
void TouchSaves_Question(aref ui, aref game, aref state)
{
	state.here = false;
	if (!CheckAttribute(game, "question"))
	{
		state.answered = false;
		return;
	}
	bool here = state.asked == "save";
	for (int k = 0; k < TouchLib_Count(game, "records"); k++)
	{
		aref record = TouchLib_Item(game, "records", k);
		if (!sti(record.fresh) && record.key == state.askedKey) here = true;
	}
	state.here = here;
	if (here && state.asked == "load" && sti(state.fromTitle) && sti(game.question.canYes))
	{
		// Once while the question stays.
		if (!sti(state.answered)) TouchAdpSaves_Answer(ui, true);
		state.answered = true;
		// A step of the load the player asked for: the list stays as it was until the load's cover.
		UiAwait();
		return;
	}
	aref confirm;
	makearef(confirm, TouchSaves.confirm);
	confirm.preset = "confirm";
	confirm.title = game.question.text;
	confirm.subject = "";
	confirm.text = "";
	confirm.yes = game.question.yes;
	confirm.no = game.question.no;
	if (confirm.no == "") confirm.no = UiText("common.cancel");
	if (here && state.asked != "")
	{
		string kind = state.asked;
		if (kind == "delete" || kind == "overwrite") confirm.preset = "danger";
		confirm.title = UiText("save." + kind);
		confirm.yes = confirm.title;
		if (kind != "save") confirm.subject = state.askedName;
	}
	UiOn("confirm-confirm", "");
	UiOn("confirm-cancel", "");
	if (game.question.no != "") UiModal("confirm", "confirm-cancel");
	else UiModal("confirm", ""); // the game's question without a «no»
}

void TouchSaves_View(aref ui, aref game, aref state)
{
	DeleteAttribute(&TouchSaves, "");
	TouchSaves.title = UiText("save.title_load");
	if (sti(game.saving)) TouchSaves.title = UiText("save.title_save");
	// Name, the game date with the save's own date under it, the autosave mark: one date column of 120 dp leaves
	// the name its room beside the detail on a 640 dp screen.
	aref column;
	makearef(column, TouchSaves.columns.c0);
	column.label = UiText("save.record");
	column.w = "1";
	makearef(column, TouchSaves.columns.c1);
	column.label = UiText("save.game_date");
	column.w = "120px";
	column.align = "right";
	makearef(column, TouchSaves.columns.c2);
	column.label = "";
	column.w = "24px";
	column.align = "center";
	object order;
	TouchSaves_Order(game, &order);
	int count = GetAttributesNum(&order);
	// The player's choice holds while its record exists, else the new record when writing, else the game's choice.
	int chosen = -1;
	int k;
	aref record;
	for (k = 0; k < count; k++)
	{
		record = GetAttributeN(&order, k);
		if (record.key == state.chosen) chosen = k;
	}
	for (k = 0; k < count && chosen < 0; k++)
	{
		record = GetAttributeN(&order, k);
		if (sti(game.saving) && sti(record.fresh)) chosen = k;
		if (!sti(game.saving) && sti(record.selected)) chosen = k;
	}
	if (chosen < 0 && count > 0) chosen = 0;
	state.chosen = "";
	aref rows;
	makearef(rows, TouchSaves.rows);
	for (k = 0; k < count; k++)
	{
		record = GetAttributeN(&order, k);
		aref row = TouchLib_Add(rows, "r");
		row.id = TouchSaves_RowId(record.key);
		row.selected = k == chosen;
		row.disabled = !sti(record.enabled);
		aref cells;
		makearef(cells, row.cells);
		if (sti(record.fresh))
		{
			TouchSaves_Cell(cells, TouchSaves_Name(record), TouchSaves_Identity(record));
			cells.c0.icon = "plus";
			cells.c0.iconTone = "brass";
			cells.c1 = "";
			cells.c2 = "";
			continue;
		}
		string identity = TouchSaves_Identity(record);
		if (identity == record.file) identity = "";
		TouchSaves_Cell(cells, TouchSaves_Name(record), identity);
		// the game date over when it was saved: one column, so the name keeps its room
		TouchSaves_Cell(cells, TouchSaves_Date(record.gameDate), TouchSaves_Created(record));
		cells.c1.tone = "dim";
		if (!sti(record.autosave)) { cells.c2 = ""; continue; }
		cells.c2.icon = "energy";
		cells.c2.iconTone = "gold";
		cells.c2.iconOnly = true;
	}
	UiOn("saves", "row");
	if (chosen >= 0)
	{
		record = GetAttributeN(&order, chosen);
		state.chosen = record.key;
		TouchSaves_Detail(game, record);
	}
	TouchSaves_Question(ui, game, state);
	UiOn("header-back", "");
}

void TouchSaves_Build()
{
	aref ui = GetEventData();
	if (ui.screen != "saves") return;
	aref state;
	makearef(state, TouchSaves_State);
	if (UiFirst())
	{
		DeleteAttribute(state, "");
		state.chosen = "";
		state.asked = "";
		state.askedKey = "";
		state.askedName = "";
		state.watching = false;
		state.answered = false;
		state.named = false;
		// The title opens the list with a note; without one a game is in progress.
		object note;
		state.fromTitle = UiTake("saves", &note) && note.from == "title";
	}
	// What the player asked for, refused by the game: he is told which record it was.
	string answer = UiAnswer();
	if (sti(state.watching) && answer == "refused")
	{
		string failed = "save.save_failed";
		if (state.asked == "load") failed = "save.load_failed";
		if (state.asked == "delete") failed = "save.delete_failed";
		string text = UiText(failed);
		if (state.askedName != "" && state.asked != "save") text += ": " + state.askedName;
		UiToast(text);
	}
	if (answer != "waiting") state.watching = false;
	aref game;
	makearef(game, TouchSaves_Game);
	DeleteAttribute(game, "");
	TouchAdpSaves_State(ui, game);
	// The game's name editor: the record keeps the name the game proposes, once while the editor stays.
	if (sti(game.naming) && !sti(state.named)) TouchAdpSaves_Name(ui);
	state.named = game.naming;
	TouchSaves_View(ui, game, state);
	UiShow(&TouchSaves);
}

// The chosen record, as the detail column shows it.
aref TouchSaves_Chosen(aref game, aref state)
{
	for (int k = 0; k < TouchLib_Count(game, "records"); k++)
	{
		aref record = TouchLib_Item(game, "records", k);
		if (record.key == state.chosen) return record;
	}
	UiError("saves: no record is chosen");
	return record;
}

void TouchSaves_Ask(aref ui, aref game, aref state, string kind)
{
	aref record = TouchSaves_Chosen(game, state);
	state.asked = kind;
	state.askedKey = record.key;
	state.askedName = TouchSaves_Name(record);
	state.watching = true;
	if (kind == "delete") TouchAdpSaves_Delete(ui, record.key);
	else TouchAdpSaves_Activate(ui, record.key);
	UiHold();
}

void TouchSaves_Tap()
{
	aref ui = GetEventData();
	if (ui.screen != "saves") return;
	string id = GetEventData(), verb = GetEventData(), value = GetEventData();
	aref game;
	makearef(game, TouchSaves_Game);
	aref state;
	makearef(state, TouchSaves_State);
	if (id == "saves")
	{
		string key = "";
		if (value != "new") key = TouchLib_Cut(value, 5, strlen(value));
		if (TouchLib_Find(game, "records", "key", key) < 0) return; // the record tapped is gone
		state.chosen = key;
		UiRebuild();
		return;
	}
	if (id == "primary")
	{
		aref record = TouchSaves_Chosen(game, state);
		string kind = "load";
		if (sti(game.saving)) kind = "overwrite";
		if (sti(record.fresh)) kind = "save";
		TouchSaves_Ask(ui, game, state, kind);
		return;
	}
	if (id == "delete") { TouchSaves_Ask(ui, game, state, "delete"); return; }
	if (id == "confirm-confirm")
	{
		// The answer to the question about what the player asked for is watched as that request is.
		if (sti(state.here)) state.watching = true;
		TouchAdpSaves_Answer(ui, true);
		UiHold();
		return;
	}
	if (id == "confirm-cancel") { if (game.question.no != "") TouchAdpSaves_Answer(ui, false); return; }
	if (id == "header-back")
	{
		if (CheckAttribute(game, "question") && sti(game.question.canNo)) TouchAdpSaves_Answer(ui, false);
		else TouchAdpSaves_Leave(ui);
	}
}
