// SPDX-License-Identifier: GPL-3.0-only
// The tavern's dice and cards from the interfaces boal_dice and boal_cards. Implements contracts/table.c. The game
// lays its pieces as script images (HeroDice<n>, CompDice<n>: "dice_<face>_<n>"; PCard<n>, ours below 18: "<suit>_<rank>",
// another picture face down) and keeps the round in its window's variables, which the engine gives as the context of
// ANDROID_TABLE_GAME: rate, moneyHero, bank, open, stage (bStartGame), turn (dir_i), first (dir_i_start), opponent
// (the character's index); dice: locked (bLockClick), heroDie<n>, heroMix<n> (in the cup), heroType, compType; cards:
// cardMove. A card counts what NullCharacter.Cards says of its picture.
// data: kind ("dice", "cards").

#define TOUCH_LEG_TABLE_CARDS 36
#define TOUCH_LEG_TABLE_HAND 18

string TouchLegTable_Fact(aref game, string key)
{
	if (!CheckAttribute(game, "context." + key)) UiError("table game: the game gives no " + key);
	return game.context.(key);
}

// The engine writes the window's whole numbers as they are (dir_i is -1 or 1).
int TouchLegTable_Int(aref game, string key)
{
	return sti(TouchLegTable_Fact(game, key));
}

// A script image the game shows now: its picture, "" none.
string TouchLegTable_Picture(aref model, string node)
{
	if (!CheckAttribute(model, node)) return "";
	if (model.(node).type != "image") return "";
	return model.(node).label;
}

// "dice_3_1" -> 3.
int TouchLegTable_Face(string picture)
{
	if (!TouchLib_StartsWith(picture, "dice_")) UiError("table game: " + picture + " is not a die");
	int end = findSubStr(picture, "_", 5);
	if (end < 0) end = strlen(picture);
	return UiAmount(TouchLib_Cut(picture, 5, end), "the die " + picture);
}

// What the game counts the card of `picture`.
int TouchLegTable_Points(string picture)
{
	if (CheckAttribute(&NullCharacter, "Cards"))
	{
		aref cards;
		makearef(cards, NullCharacter.Cards);
		for (int k = 0; k < GetAttributesNum(cards); k++)
		{
			aref card = GetAttributeN(cards, k);
			if (CheckAttribute(card, "pic") && CheckAttribute(card, "count"))
			{
				if (card.pic == picture) return sti(card.count);
			}
		}
	}
	UiError("table game: the game counts no points for card " + picture);
	return 0;
}

// The game's type of a row of dice, "" for none it names.
string TouchLegTable_Combination(aref game, string key)
{
	string type = TouchLegTable_Fact(game, key);
	if (type == "") return "";
	int n = sti(type);
	if (n < 1 || n > 8) return "";
	return "" + n;
}

void TouchLegTable_Dice(aref model, aref game, aref out)
{
	bool open = sti(out.open);
	bool locked = TouchLegTable_Int(game, "locked") != 0;
	int stage = TouchLegTable_Int(game, "stage");
	int first = TouchLegTable_Int(game, "first");
	int turn = TouchLegTable_Int(game, "turn");
	bool ours = !open && turn == 1 && !locked;
	// The game's own order of a round: our first throw, then our reroll after its first or before its own.
	bool start = ours && stage < 2;
	bool reroll = false;
	if (ours) reroll = (first == 1 && stage == 2) || (first == -1 && stage == 3);
	out.again = !locked;
	out.moves.mark = reroll;
	int marked = 0;
	bool whole = true;
	aref row;
	makearef(row, out.ours);
	aref piece;
	for (int n = 1; n <= 5; n++)
	{
		string picture = TouchLegTable_Picture(model, "HeroDice" + n);
		bool cup = TouchLegTable_Fact(game, "heroMix" + n) == "1";
		if (cup && reroll) marked++;
		if (picture == "")
		{
			whole = false;
			// A die marked for the reroll leaves the table: shown lifted with the face the game remembers.
			if (!reroll || !cup) continue;
			int face = TouchLegTable_Int(game, "heroDie" + n);
			if (face < 1) continue;
			piece = TouchLib_Add(row, "p");
			piece.kind = "die";
			piece.face = face;
			piece.lifted = true;
			piece.mark = "";
			continue;
		}
		piece = TouchLib_Add(row, "p");
		piece.kind = "die";
		piece.face = TouchLegTable_Face(picture);
		piece.lifted = false;
		piece.mark = "";
		if (!open && CheckAttribute(model, "B_HeroDice" + n)) piece.mark = n;
	}
	if (start) marked = 5;
	out.ourCount = "";
	if (whole) out.ourCount = TouchLegTable_Combination(game, "heroType");
	makearef(row, out.theirs);
	whole = true;
	for (n = 1; n <= 5; n++)
	{
		picture = TouchLegTable_Picture(model, "CompDice" + n);
		if (picture == "") { whole = false; continue; }
		piece = TouchLib_Add(row, "p");
		piece.kind = "die";
		piece.face = TouchLegTable_Face(picture);
		piece.lifted = false;
		piece.mark = "";
	}
	out.theirCount = "";
	if (whole) out.theirCount = TouchLegTable_Combination(game, "compType");
	out.moves.throw = 0;
	if (start || reroll) out.moves.throw = marked;
	out.moves.pass = reroll && marked == 0 && CheckAttribute(model, "B_ICON_1");
	out.deck = true;
}

void TouchLegTable_Cards(aref model, aref game, aref out)
{
	bool open = sti(out.open);
	int stage = TouchLegTable_Int(game, "stage");
	int turn = TouchLegTable_Int(game, "turn");
	bool playing = !open && turn == 1 && stage >= 2 && stage != 100;
	out.again = stage != 100;
	out.moves.mark = false;
	int ourPoints = 0;
	int theirPoints = 0;
	bool hidden = false;
	aref ours, theirs;
	makearef(ours, out.ours);
	makearef(theirs, out.theirs);
	for (int n = 0; n < TOUCH_LEG_TABLE_CARDS; n++)
	{
		string picture = TouchLegTable_Picture(model, "PCard" + n);
		if (picture == "") continue;
		aref piece;
		if (n < TOUCH_LEG_TABLE_HAND) piece = TouchLib_Add(ours, "p");
		else piece = TouchLib_Add(theirs, "p");
		piece.lifted = false;
		piece.mark = "";
		int cut = findSubStr(picture, "_", 0);
		if (cut < 0)
		{
			piece.kind = "back";
			if (n >= TOUCH_LEG_TABLE_HAND) hidden = true;
			continue;
		}
		piece.kind = "card";
		piece.suit = TouchLib_Cut(picture, 0, cut);
		piece.rank = TouchLib_Cut(picture, cut + 1, strlen(picture));
		int points = TouchLegTable_Points(picture);
		if (n < TOUCH_LEG_TABLE_HAND) ourPoints += points;
		else theirPoints += points;
	}
	out.ourCount = "";
	if (GetAttributesNum(ours) > 0) out.ourCount = ourPoints;
	out.theirCount = "";
	if (GetAttributesNum(theirs) > 0 && !hidden) out.theirCount = theirPoints;
	out.moves.throw = 0;
	if (playing && TouchLegTable_Int(game, "cardMove") == 0) out.moves.throw = 1;
	out.moves.pass = playing && CheckAttribute(model, "B_ICON_1");
	out.deck = CheckAttribute(model, "Pack");
}

void TouchAdpTable_State(aref ui, aref out)
{
	aref model = TouchLegLib_Model(ui);
	if (!CheckAttribute(model, "ANDROID_TABLE_GAME")) UiError("table game: the engine gives no ANDROID_TABLE_GAME");
	aref game;
	makearef(game, model.ANDROID_TABLE_GAME);
	if (game.reason != "") UiError("table game: " + game.reason);
	out.kind = ui.data.kind;
	ref opponent = &Characters[TouchLegTable_Int(game, "opponent")];
	out.opponent = GetFullName(opponent);
	out.portrait = "";
	if (CheckAttribute(opponent, "FaceId")) out.portrait = "INTERFACES/PORTRAITS/128/face_" + opponent.FaceId;
	out.line = TouchLegLib_Label(model, "INFO_TEXT");
	out.stake = TouchLegTable_Int(game, "rate");
	out.money = TouchLegTable_Int(game, "moneyHero");
	out.bank = TouchLegTable_Int(game, "bank");
	out.coins = "silver";
	for (int k = 0; k < GetAttributesNum(model); k++)
	{
		aref node = GetAttributeN(model, k);
		if (TouchLib_StartsWith(GetAttributeName(node), "Money_") && node.type == "image")
		{
			if (node.label == "gold") out.coins = "gold";
			break;
		}
	}
	out.open = TouchLegTable_Int(game, "open") != 0;
	if (out.kind == "dice") TouchLegTable_Dice(model, game, out);
	else TouchLegTable_Cards(model, game, out);
}

void TouchAdpTable_Mark(aref ui, string piece)
{
	UiSend("B_HeroDice" + piece, "press", "");
}

void TouchAdpTable_Throw(aref ui)
{
	UiSend("B_PACK", "press", "");
}

void TouchAdpTable_Pass(aref ui)
{
	UiSend("B_ICON_1", "press", "");
}

void TouchAdpTable_Again(aref ui)
{
	UiSend("B_PACK", "press", "");
}

void TouchAdpTable_Leave(aref ui)
{
	string leave = "";
	if (CheckAttribute(ui, "model.EXIT_BTN")) leave = "EXIT_BTN";
	UiBack(leave);
}
