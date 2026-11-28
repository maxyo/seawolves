// SPDX-License-Identifier: GPL-3.0-only
// The exchange with another ship (transfer_main.c, also the boarding's loot): the goods table TABLE_LIST with the
// columns of the binding's data and the arrows B_LEFT (take) / B_RIGHT (give) of its selected row, the ships' tables
// TABLE_OTHER / TABLE_OTHER2, the crew window CREW_*, the defectors HIRE_*, the prisoners' and the captain's fate
// CAPTURE_*, the one question window REMOVE_* and the captain choice OFFICERS_WINDOW. Which window is open shows by
// its nodes (a hidden window exports none); the question window's meaning, the other captain and the defectors taken
// are the window's own globals (sMessageMode, bTransferMode, xi_refCharacter, iCrewQty), the least crew of a ship the
// game's GetMinCrewQuantity.
// Implements contracts/transfer.c.

// The step a move is at while the screen waits: "quick" (row `row` was selected for all of their stack to be taken)
// and the row selected again to bring back the arrows the game hides while its focus is off the table (`again`).
object TouchLegTransfer;

void TouchAdpTransfer_State(aref ui, aref out)
{
	aref model = TouchLegLib_Model(ui);
	DeleteAttribute(out, "");
	if (UiFirst() || !CheckAttribute(&TouchLegTransfer, "quick"))
	{
		TouchLegTransfer.quick = -1;
		TouchLegTransfer.again = -1;
	}
	ref captain = &Characters[UiGameInt("xi_refCharacter.index")];
	out.title = TouchLib_Trim(UiOneLine(TouchLegLib_Label(model, "TITLE")));
	aref ours;
	makearef(ours, out.ours);
	TouchLegTransfer_Ship(model, "", ours);
	aref theirs;
	makearef(theirs, out.theirs);
	TouchLegTransfer_Ship(model, "2", theirs);
	aref image;
	makearef(image, theirs.image);
	if (!TouchLegLib_NodeImage(model, "SHIP_BIG_PICTURE2", image)) DeleteAttribute(theirs, "image");
	bool alive = !LAi_IsDead(captain);
	theirs.captain = "";
	if (alive) theirs.captain = GetFullName(captain);
	theirs.vacant = !alive && !UiGameInt("bTransferMode");
	TouchLegTransfer_Figures(model, out);
	aref cargo;
	makearef(cargo, out.cargo);
	aref data;
	makearef(data, ui.data);
	TouchLegTrade_Rows(data, model, cargo);
	cargo.move.take = TouchLegTransfer_Can(model, "B_LEFT");
	cargo.move.give = TouchLegTransfer_Can(model, "B_RIGHT");
	if (CheckAttribute(model, "TAKE_GOODS"))
	{
		out.takeAll.can = UiOffers("TAKE_GOODS", "press");
		out.takeAll.reason = model.TAKE_GOODS.reason;
	}
	TouchLegTransfer_Buttons(model, out);
	bool crew = CheckAttribute(model, "CREW_QTY");
	if (CheckAttribute(model, "CREW_BUTTON"))
	{
		out.crewTab.can = crew || UiOffers("CREW_BUTTON", "press");
		out.crewTab.reason = model.CREW_BUTTON.reason;
	}
	if (crew) TouchLegTransfer_Crew(model, captain, out);
	TouchLegTransfer_Question(model, out);
	if (CheckAttribute(model, "ACCEPT_ADD_OFFICER") || CheckAttribute(model, "PASSENGERSLIST.rows"))
		TouchLegTransfer_Captains(model, out);
	if (TouchLegTransfer_Follow(ui, out)) UiAwait();
}

bool TouchLegTransfer_Can(aref model, string node)
{
	if (!CheckAttribute(model, node)) return false;
	return UiOffers(node, "press");
}

void TouchLegTransfer_Ship(aref model, string suffix, aref at)
{
	at.name = UiOneLine(TouchLegLib_Label(model, "SHIP_NAME" + suffix));
	at.type = UiOneLine(TouchLegLib_Label(model, "SHIP_TYPE" + suffix));
	at.rank = UiOneLine(TouchLegLib_Label(model, "SHIP_RANK" + suffix));
	at.room = -1;
	string table = "TABLE_OTHER" + suffix;
	int k = TouchLegTransfer_Figure(model, table, "Capacity");
	if (k < 0) return;
	aref row = TouchLib_Item(model, table + ".rows", k);
	// "317 / 700": the load of the deadweight
	string text = TouchLegLib_Cell(row, 2);
	int slash = findSubStr(text, "/", 0);
	if (slash < 0) UiError("transfer: no deadweight in '" + text + "'");
	at.room = UiAmount(TouchLib_Cut(text, slash + 1, strlen(text)), "the deadweight") -
	          UiAmount(TouchLib_Cut(text, 0, slash), "the load");
}

// k of the row `key` of a ship's table, -1 none.
int TouchLegTransfer_Figure(aref model, string table, string key)
{
	for (int k = 0; k < TouchLib_Count(model, table + ".rows"); k++)
	{
		aref at = TouchLib_Item(model, table + ".rows", k);
		if (at.id == key) return k;
	}
	return -1;
}

// The two ships' figures row by row of ours (the game writes theirs in the same rows).
void TouchLegTransfer_Figures(aref model, aref out)
{
	aref figures;
	makearef(figures, out.figures);
	for (int k = 0; k < TouchLib_Count(model, "TABLE_OTHER.rows"); k++)
	{
		aref row = TouchLib_Item(model, "TABLE_OTHER.rows", k);
		string label = TouchLegLib_Cell(row, 1);
		if (label == "") continue;
		aref at = TouchLib_Add(figures, "f");
		at.key = row.id;
		at.label = label;
		at.ours = TouchLegLib_Cell(row, 2);
		at.theirs = "";
		int other = TouchLegTransfer_Figure(model, "TABLE_OTHER2", row.id);
		if (other >= 0) at.theirs = TouchLegLib_Cell(TouchLib_Item(model, "TABLE_OTHER2.rows", other), 2);
	}
}

string TouchLegTransfer_Nodes[2] = {"CAPTAN_BUTTON", "SWAP_BUTTON"};

void TouchLegTransfer_Buttons(aref model, aref out)
{
	aref buttons;
	makearef(buttons, out.buttons);
	for (int i, string node: TouchLegTransfer_Nodes)
		if (CheckAttribute(model, node))
		{
			object button = {node = node, label = model.(node).label, can = UiOffers(node, "press"),
			                 reason = model.(node).reason};
			aref at = TouchLib_Add(buttons, "b");
			CopyAttributes(at, &button);
		}
}

void TouchLegTransfer_Crew(aref model, ref captain, aref out)
{
	object crew = {ours = UiAmount(model.CREW_QTY.label, "CREW_QTY"),
	               theirs = UiAmount(TouchLegLib_Label(model, "CREW_QTY2"), "CREW_QTY2"),
	               minOurs = GetMinCrewQuantity(GetMainCharacter()), minTheirs = GetMinCrewQuantity(captain),
	               take = TouchLegTransfer_Can(model, "CREW_LEFT"), give = TouchLegTransfer_Can(model, "CREW_RIGHT")};
	crew.morale.ours = UiOneLine(TouchLegLib_Label(model, "CREW_MORALE_TEXT"));
	crew.morale.theirs = UiOneLine(TouchLegLib_Label(model, "CREW_MORALE_TEXT2"));
	aref at;
	makearef(at, out.crew);
	CopyAttributes(at, &crew);
	aref side;
	makearef(side, at.ourFacts);
	TouchLegTransfer_Facts(model, "TABLE_CREW", side);
	makearef(side, at.theirFacts);
	TouchLegTransfer_Facts(model, "TABLE_CREW2", side);
}

// The {label, value} rows of a two-column table of the game (its first column a picture).
void TouchLegTransfer_Facts(aref model, string table, aref out)
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

// The game's window asking now, by its nodes: the defectors, the prisoners, the surrendered captain, the question
// window (its mode says whether it asks about the ship we leave), a message.
void TouchLegTransfer_Question(aref model, aref out)
{
	object question = {kind = "", leave = "", title = "", text = "", men = 0, most = 0, crew = 0, adjust = false};
	if (CheckAttribute(model, "HIRE_QTY_OK_BUTTON"))
	{
		question.kind = "hire";
		question.title = UiOneLine(TouchLegLib_Label(model, "HIRE_QTY_TypeOperation"));
		// iCrewQty: the men taken so far (our crew already counts them); Crew.Hire: the willing still left.
		question.men = UiGameInt("iCrewQty");
		question.most = question.men + UiGameInt("xi_refCharacter.Ship.Crew.Hire");
		question.crew = UiAmount(TouchLegLib_Label(model, "HIRE_CREW_QTY"), "HIRE_CREW_QTY");
		question.adjust = CheckAttribute(model, "HIRE_QTY_ADD_BUTTON") && CheckAttribute(model, "HIRE_QTY_REMOVE_BUTTON");
	}
	else
	{
		if (CheckAttribute(model, "CAPTURE_CREW_KILL"))
		{
			question.kind = "crew";
			question.title = UiOneLine(TouchLegLib_Label(model, "CAPTURE_TEXT_CREW"));
		}
		else
		{
			if (CheckAttribute(model, "CAPTURE_CAPTAIN_KILL"))
			{
				question.kind = "captain";
				question.title = UiOneLine(TouchLegLib_Label(model, "CAPTURE_CAPTION"));
				question.text = UiOneLine(TouchLegLib_Label(model, "CAPTURE_TEXT"));
			}
			else
			{
				question.title = UiOneLine(TouchLegLib_Label(model, "REMOVE_WINDOW_CAPTION"));
				question.text = UiOneLine(TouchLegLib_Label(model, "REMOVE_WINDOW_TEXT"));
				if (CheckAttribute(model, "REMOVE_ACCEPT_OFFICER"))
				{
					string mode = UiGameText("sMessageMode");
					question.kind = "ask";
					if (mode == "ShipDeadAsk") question.leave = "sink";
					if (mode == "ShipGoFreeAsk") question.leave = "release";
					if (question.leave != "") question.kind = "ship";
				}
				else
				{
					if (CheckAttribute(model, "REMOVE_OK")) question.kind = "message";
				}
			}
		}
	}
	if (question.kind == "") return;
	aref at;
	makearef(at, out.question);
	CopyAttributes(at, &question);
	if (question.kind == "captain")
	{
		aref image;
		makearef(image, at.image);
		if (!TouchLegLib_NodeImage(model, "CAPTUR_PICTURE", image)) DeleteAttribute(at, "image");
	}
	aref answers;
	makearef(answers, at.answers);
	string kind = question.kind;
	if (kind == "hire")
	{
		TouchLegTransfer_Answer(model, answers, "none", "HIRE_QTY_CANCEL_BUTTON");
		TouchLegTransfer_Answer(model, answers, "ok", "HIRE_QTY_OK_BUTTON");
	}
	if (kind == "crew")
	{
		TouchLegTransfer_Answer(model, answers, "kill", "CAPTURE_CREW_KILL");
		TouchLegTransfer_Answer(model, answers, "prison", "CAPTURE_CREW_PRISON");
		TouchLegTransfer_Answer(model, answers, "free", "CAPTURE_CREW_FREE");
	}
	if (kind == "captain")
	{
		TouchLegTransfer_Answer(model, answers, "kill", "CAPTURE_CAPTAIN_KILL");
		TouchLegTransfer_Answer(model, answers, "prison", "CAPTURE_CAPTAIN_PRISON");
		TouchLegTransfer_Answer(model, answers, "cancel", "CAPTURE_CANCEL");
	}
	if (kind == "ship")
	{
		TouchLegTransfer_Answer(model, answers, "yes", "REMOVE_ACCEPT_OFFICER");
		TouchLegTransfer_Answer(model, answers, "no", "REMOVE_CANCEL_OFFICER");
	}
	if (kind == "ask")
	{
		TouchLegTransfer_Answer(model, answers, "no", "REMOVE_CANCEL_OFFICER");
		TouchLegTransfer_Answer(model, answers, "yes", "REMOVE_ACCEPT_OFFICER");
	}
	if (kind == "message") TouchLegTransfer_Answer(model, answers, "ok", "REMOVE_OK");
}

void TouchLegTransfer_Answer(aref model, aref answers, string key, string node)
{
	if (!CheckAttribute(model, node)) return;
	object answer = {key = key, node = node, label = model.(node).label, can = UiOffers(node, "press"),
	                 reason = model.(node).reason};
	aref at = TouchLib_Add(answers, "a");
	CopyAttributes(at, &answer);
}

// The captain choice (OFFICERS_WINDOW): the passengers who can take the ship, the chosen one's name, his facts and
// ship skills as SetOfficersSkills shows them (cleared while the chosen slot holds nobody), the game's «Назначить».
void TouchLegTransfer_Captains(aref model, aref out)
{
	aref sheet;
	makearef(sheet, out.captains);
	sheet.title = UiOneLine(TouchLegLib_Label(model, "OFFICERS_WINDOW_CAPTION"));
	sheet.name = UiOneLine(TouchLegLib_Label(model, "OFFICER_NAME"));
	if (!CheckAttribute(model, "CANCEL_ADD_OFFICER") && !CheckAttribute(model, "OFFICER_EXIT_BTN"))
		UiError("transfer: the captain window has neither CANCEL_ADD_OFFICER nor OFFICER_EXIT_BTN");
	aref people;
	makearef(people, sheet.people);
	for (int k = 0; k < TouchLib_Count(model, "PASSENGERSLIST.rows"); k++)
	{
		aref row = TouchLib_Item(model, "PASSENGERSLIST.rows", k);
		object person = {label = row.label, selected = row.value == "1", can = CheckAttribute(row, "offers.select"),
		                 reason = row.reason};
		aref at = TouchLib_Add(people, "p");
		CopyAttributes(at, &person);
		aref image;
		makearef(image, at.image);
		if (!TouchLegLib_Image(row, image)) DeleteAttribute(at, "image");
	}
	if (sheet.name != "")
	{
		aref facts;
		makearef(facts, sheet.facts);
		TouchLegTransfer_Facts(model, "TABLE_SMALLOTHER", facts);
		aref skills;
		makearef(skills, sheet.skills);
		TouchLegLib_ShipSkills(model, "TABLE_SMALLSKILL", skills);
	}
	if (!CheckAttribute(model, "ACCEPT_ADD_OFFICER")) return;
	sheet.appoint.label = model.ACCEPT_ADD_OFFICER.label;
	sheet.appoint.can = UiOffers("ACCEPT_ADD_OFFICER", "press");
	sheet.appoint.reason = model.ACCEPT_ADD_OFFICER.reason;
}

// k of the selected cargo row, -1 none.
int TouchLegTransfer_Selected(aref cargo)
{
	for (int k = 0; k < TouchLib_Count(cargo, "rows"); k++)
	{
		aref row = TouchLib_Item(cargo, "rows", k);
		if (sti(row.selected)) return k;
	}
	return -1;
}

// The quick take once its row is selected; the selected row selected again, once, while the game hides both of its
// arrows (its focus is off the table, as after the crew window: SetGoodsArrows runs on a table's selection). True
// while the answer needs another frame.
bool TouchLegTransfer_Follow(aref ui, aref out)
{
	string answer = UiAnswer();
	if (answer == "" || answer == "refused") TouchLegTransfer.quick = -1;
	aref cargo;
	makearef(cargo, out.cargo);
	int open = TouchLegTransfer_Selected(cargo);
	if (open < 0 || CheckAttribute(out, "crew") || CheckAttribute(out, "question") || CheckAttribute(out, "captains"))
	{
		TouchLegTransfer.again = -1;
		return false;
	}
	aref row = TouchLib_Item(cargo, "rows", open);
	if (sti(TouchLegTransfer.quick) == open && answer != "waiting")
	{
		TouchLegTransfer.quick = -1;
		if (!sti(cargo.move.take)) return false;
		TouchLegTransfer_Press(ui, open, -1);
		return true;
	}
	if (sti(cargo.move.take) || sti(cargo.move.give))
	{
		TouchLegTransfer.again = -1;
		return false;
	}
	if (sti(TouchLegTransfer.again) == open || !sti(row.can)) return false;
	TouchLegTransfer.again = open;
	TouchAdpTransfer_Select(ui, open);
	return false;
}

// The arrows move row k's goods by a pack a press (TakeGoods / GiveGoods: a pack, or what is left of it), the game
// stopping them where the receiving hold is full; the build shows where it stopped. `amount` -1: all of theirs.
void TouchLegTransfer_Press(aref ui, int k, int amount)
{
	object cargo;
	aref data;
	makearef(data, ui.data);
	TouchLegTrade_Rows(data, TouchLegLib_Model(ui), &cargo);
	aref row = TouchLib_Item(&cargo, "rows", k);
	if (!sti(row.selected)) UiError("transfer: the goods of a row that is not selected are moved");
	if (amount < 0) amount = sti(row.own) + sti(row.other);
	int change = amount - sti(row.own);
	string arrow = "B_LEFT";
	if (change < 0) arrow = "B_RIGHT";
	if (!CheckAttribute(TouchLegLib_Model(ui), arrow)) return;
	// The presses nearest the amount; an end of the control (none, both stacks) is reached whole.
	int pack = sti(row.pack);
	int presses = (abs(change) + pack / 2) / pack;
	if (amount == 0 || amount == sti(row.own) + sti(row.other)) presses = (abs(change) + pack - 1) / pack;
	if (presses == 0 && change != 0) presses = 1;
	for (int i = 0; i < presses; i++) UiSend(arrow, "press", "");
}

void TouchAdpTransfer_Select(aref ui, int row)
{
	aref at = TouchLib_Item(TouchLegLib_Model(ui), "TABLE_LIST.rows", row);
	object action = {node = "TABLE_LIST", row = at.index, verb = "select", value = at.id};
	UiSendTo(&action);
}

void TouchAdpTransfer_Move(aref ui, int row, int amount)
{
	TouchLegTransfer_Press(ui, row, amount);
}

// A row not selected yet is selected first: the arrows act on the selected row once the game has bound them.
void TouchAdpTransfer_Take(aref ui, int row)
{
	aref at = TouchLib_Item(TouchLegLib_Model(ui), "TABLE_LIST.rows", row);
	if (at.value == "1")
	{
		TouchLegTransfer_Press(ui, row, -1);
		return;
	}
	TouchAdpTransfer_Select(ui, row);
	TouchLegTransfer.quick = row;
}

void TouchAdpTransfer_TakeAll(aref ui)
{
	UiSend("TAKE_GOODS", "press", "");
}

void TouchAdpTransfer_Press(aref ui, int button)
{
	object state;
	TouchLegTransfer_Buttons(TouchLegLib_Model(ui), &state);
	aref at = TouchLib_Item(&state, "buttons", button);
	UiSend(at.node, "press", "");
}

void TouchAdpTransfer_Crew(aref ui, bool open)
{
	aref model = TouchLegLib_Model(ui);
	string node = "CREW_BUTTON";
	if (!open && CheckAttribute(model, "CREW_EXIT_BTN")) node = "CREW_EXIT_BTN";
	UiSend(node, "press", "");
}

// The crew's arrows move one man a press; TakeCrew / GiveCrew stop at the receiving ship's room and its hold.
void TouchAdpTransfer_Men(aref ui, int men)
{
	aref model = TouchLegLib_Model(ui);
	int change = men - UiAmount(model.CREW_QTY.label, "CREW_QTY");
	string arrow = "CREW_LEFT";
	if (change < 0) arrow = "CREW_RIGHT";
	for (int i = 0; i < abs(change); i++) UiSend(arrow, "press", "");
}

void TouchAdpTransfer_Answer(aref ui, int answer)
{
	object state;
	aref model = TouchLegLib_Model(ui);
	TouchLegTransfer_Question(model, &state);
	aref at = TouchLib_Item(&state, "question.answers", answer);
	UiSend(at.node, "press", "");
}

// The window's arrows move the men taken by one a press (its handlers keep them within the willing).
void TouchAdpTransfer_Hire(aref ui, int men)
{
	int change = men - UiGameInt("iCrewQty");
	string arrow = "HIRE_QTY_ADD_BUTTON";
	if (change < 0) arrow = "HIRE_QTY_REMOVE_BUTTON";
	for (int i = 0; i < abs(change); i++) UiSend(arrow, "press", "");
}

void TouchAdpTransfer_Pick(aref ui, int person)
{
	TouchLegLib_Choose(TouchLegLib_Model(ui), "PASSENGERSLIST", person);
}

void TouchAdpTransfer_Appoint(aref ui)
{
	UiSend("ACCEPT_ADD_OFFICER", "press", "");
}

void TouchAdpTransfer_Dismiss(aref ui)
{
	string node = "OFFICER_EXIT_BTN";
	if (CheckAttribute(ui, "model.CANCEL_ADD_OFFICER")) node = "CANCEL_ADD_OFFICER";
	UiSend(node, "press", "");
}

// The game's exit asks about the ship we leave where it has to.
void TouchAdpTransfer_Leave(aref ui)
{
	string leave = "";
	if (CheckAttribute(ui, "model.EXIT_BTN")) leave = "EXIT_BTN";
	UiBack(leave);
}
