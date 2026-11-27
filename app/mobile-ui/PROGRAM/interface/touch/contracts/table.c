// SPDX-License-Identifier: GPL-3.0-only
// The table games' adapter (provides "adapter:table"): a round of dice or cards against a tavern's patron, as the
// game deals, counts and decides it. The screen shows the table the game lays and sends the player's move; whose turn
// it is, what a move costs and who wins are the game's. `ui` is the screen's object its events hand over.
//
// State(ui, out), once a build:
//   kind ("dice", "cards"); opponent (his name), portrait (his face picture, "" none), line (what the game says)
//   stake, money, bank (whole numbers as the game holds them: the stake of a move, our money, what lies on the table)
//   coins ("gold", "silver": the coins the game lays)
//   open (between rounds: the player may leave or ask for another), again (the game offers another round)
//   ours, theirs: rows of pieces p<k>.{kind ("die", "card", "back": a card face down), face (a die's, 1..6), suit,
//     rank (a card's, as the game names its picture: "7", "J"), lifted (a die in the cup for the throw), mark (the
//     piece's number to mark it for the throw, "" not markable)}
//   ourCount, theirCount: what the game counts the row ("" while it counts nothing shown): the dice's combination
//     (the game's type, 1..8), the cards' points
//   moves.{mark (dice may be marked now), throw (dice: how many the throw throws; cards: 1 a card may be taken; 0
//     neither), pass (keep the dice as they lie, or stand)}; deck (the game shows a cup or a deck)
extern void TouchAdpTable_State(aref ui, aref out);
extern void TouchAdpTable_Mark(aref ui, string piece); // ours p<k>.mark: into the cup for the throw
extern void TouchAdpTable_Throw(aref ui);              // throw the cup, or take a card
extern void TouchAdpTable_Pass(aref ui);               // keep the dice, or stand
extern void TouchAdpTable_Again(aref ui);              // another round
extern void TouchAdpTable_Leave(aref ui);
