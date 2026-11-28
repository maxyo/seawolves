// SPDX-License-Identifier: GPL-3.0-only
// The exchange with another ship (provides "adapter:transfer"): a boarded prize, a companion, a ship met at sea. Our
// ship and theirs side by side, the cargo between their holds, their crews, and the questions the game asks on the way
// (the defectors, the prisoners' fate, a ship left without a captain, a captain to appoint). Every amount and limit is
// the game's: a move goes to the game, which stops where it stops, and State says where.
//
// State(ui, out), once a build:
//   title ("" none); ours, theirs: .{name, type, rank (as written, "" none), room (free cwt, -1 unknown)}; theirs.{image
//     (a picture, absent none), captain (who commands it, "" none), vacant (no captain, and not ours to command)}
//   figures.f<k>.{key (the game's row), label, ours, theirs (as written; theirs "" where it has no such row)}
//   cargo: headers and rows as the trade contract's State gives them (TouchAdpTrade_State); move.{take, give} (the
//     selected row can be taken from them, given to them now)
//   takeAll (absent: none).{can, reason}; buttons.b<k>.{label, can, reason}: the ship's own (captain, swap)
//   crewTab (absent: the window has no crew).{can, reason}; crew (absent: closed).{ours, theirs (men), minOurs,
//     minTheirs (the least each ship sails with, 0 none), take, give (a move each way is possible now), morale.{ours,
//     theirs} (the game's words), ourFacts, theirFacts .f<k>.{label, value}}
//   question (absent: none).{kind ("hire": the defectors, "crew": the prisoners' fate, "captain": the surrendered
//     captain's, "ship": a ship we leave, "ask", "message"), leave ("sink", "release": what leaving the ship does),
//     title, text, image (a picture, absent none), answers.a<k>.{key ("none", "ok", "kill", "prison", "free", "cancel",
//     "yes", "no"), label, can, reason}, men (hire: taken so far), most (hire: everyone willing), crew (hire: ours,
//     the men taken counted)}
//   captains (absent: none).{title, and the captain choice as TouchLib_Captains takes it}
extern void TouchAdpTransfer_State(aref ui, aref out);
// The moves; each takes as many of the game's frames as it needs while the screen waits (UiHold).
extern void TouchAdpTransfer_Select(aref ui, int row);           // a cargo row chosen
extern void TouchAdpTransfer_Move(aref ui, int row, int amount); // our side of row k towards `amount`
extern void TouchAdpTransfer_Take(aref ui, int row);             // all of their stack of row k taken
extern void TouchAdpTransfer_TakeAll(aref ui);
extern void TouchAdpTransfer_Press(aref ui, int button);         // buttons b<k>
extern void TouchAdpTransfer_Crew(aref ui, bool open);           // the crew shown or put away
extern void TouchAdpTransfer_Men(aref ui, int men);              // our crew towards `men`
extern void TouchAdpTransfer_Answer(aref ui, int answer);        // question answers a<k>
extern void TouchAdpTransfer_Hire(aref ui, int men);             // the defectors taken towards `men`
extern void TouchAdpTransfer_Pick(aref ui, int person);          // captains candidates c<k>
extern void TouchAdpTransfer_Appoint(aref ui);
extern void TouchAdpTransfer_Dismiss(aref ui);                   // the captains put away, nobody appointed
extern void TouchAdpTransfer_Leave(aref ui);
