// SPDX-License-Identifier: GPL-3.0-only
// The character screen's adapter (provides "adapter:character"): a person's characteristics and skills, their perks
// in the game's two groups and, in the hero's own window, the officers on their posts. `ui` is the screen's object
// its events hand over; a person is a position k in out.people, a skill row a position in its table's rows, a post
// a slot of out.posts. What a skill point, a perk or a post change does, and whether it may, is the game's: the
// adapter performs it on the game's own controls and reads the answer.
//
// State(ui, out), once a build:
//   title; full (the hero's window: people, officers, sections); sections (the window has the hero sections)
//   people.p<k>.{label, meta (the captions of their slots), image (picture, "" none)}; shown (k the game shows, -1
//     none listed); waiting (the game is still to show the person picked: this build is a step)
//   card.{name, rank (caption), rankValue, portrait (texture, "" none), health.{now, max}, energy.{now, max},
//     healthWord ("" none), healthCaption, healthBad, standing.{caption, value ("" none), authority (bool)},
//     salary (0 none), facts (the game's other figures, a line each: "caption: value")}
//   actions.a<k>.{id, label, primary, can} (the game's own answer buttons of its start window; none: absent)
//   free.{skill, special, self, ship} (points to spend); next.{self, ship}.{exp, rate} (towards the next perk point)
//   groups.{self, ship} (the game's captions of its perk groups, "" none); groupShip (the game shows the ship group)
//   tables.{special, personal, ship}.{heading, rows.r<j>.{id, name, experience ("" full, else its percent), base,
//     bonus (the game's effective value minus the base), bonusOk (the game wrote a bonus it can be read from), icon,
//     added (points put in this visit)}}; specialPoints (characteristics take points: the start window)
//   perks.{complete (every perk with its links), nodes.n<k>.{id, label, text, ship, learned, officer (an officer
//     of the hero has it), table (one of the shown group's), image (picture)}, edges.e<k>.{from, to, met}}
//   learning (a perk is being learned); refused (the perk the game refused in this state, "" none)
//   posts.p<k>.{slot, post, icon (ours of the post), role, label ("" free), image, character (-1 free)}
//   passengers.p<k>.{label, rank, image}
//   window ("" none, "remove", "assign", "message") and with it remove.{slot, label, role, skill, skillValue,
//     loyalty, salary, can}, assign.{role, rows.r<k>.{label, rank, salary, skill, skillValue, image, busy}},
//     message.{title, text}
extern void TouchAdpCharacter_State(aref ui, aref out);
// The game's description of skill row k of `table` (special, personal, ship) with what its value is made of:
// out.{text, base, steps.s<k>.{kind, label, value, a1, a2, a3}}; no steps when the value is the base.
extern void TouchAdpCharacter_Help(aref ui, string table, int row, aref out);
extern void TouchAdpCharacter_Person(aref ui, int person);              // another person shown
extern void TouchAdpCharacter_Group(aref ui, bool ship);                // the game's perk group shown
extern void TouchAdpCharacter_Skill(aref ui, string table, int row, bool up); // a point put in or taken back
extern void TouchAdpCharacter_Learn(aref ui, string perk);              // the game asked to learn the perk
extern void TouchAdpCharacter_Post(aref ui, int slot);                  // a post opened: its chooser or dismissal
extern void TouchAdpCharacter_Assign(aref ui, int candidate);           // a candidate of the chooser on the post
extern void TouchAdpCharacter_Dismiss(aref ui);                         // the officer of the open post dismissed
extern void TouchAdpCharacter_Close(aref ui);                           // the game's open window closed
extern void TouchAdpCharacter_Act(aref ui, string action);              // one of out.actions
extern void TouchAdpCharacter_Section(aref ui, string section);         // another hero section (ship, journal, …)
extern void TouchAdpCharacter_Leave(aref ui);
